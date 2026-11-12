#include "logger/logger.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <cstdlib>
#include <chrono>
#include <fmt/format.h>
#include "core/macros.h"
#include "laserpants/dotenv/dotenv.h"
#include "utils/file.hpp"

NS_FT_BEGIN

LoggerState Logger::_internal_state;
LoggerState* Logger::_external_state = nullptr;

static void cleanup_logger() {
    static std::atomic<bool> cleanup_done{false};
    
    // 防止重复清理
    if (cleanup_done.exchange(true)) {
        return;
    }
    
    try {
        // 先刷新所有日志器
        spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) {
            if (l) {
                l->flush();
            }
        });
        
        // 等待异步日志完成
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 关闭日志系统
        spdlog::shutdown();
    } catch (const std::exception& e) {
        // 使用 fprintf 避免可能的递归调用
        fprintf(stderr, "Error during logger cleanup: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Unknown error during logger cleanup\n");
    }
}

LoggerState& Logger::get_logger_state() {
    return _external_state ? *_external_state : _internal_state;
}

void Logger::set_logger_state(LoggerState* external_state) {
    _external_state = external_state;
    std::cout << "[Logger] Using external logger state" << std::endl;
}

void Logger::reset_logger_state() {
    _external_state = nullptr;
    std::cout << "[Logger] Reset to internal logger state" << std::endl;
}

void Logger::initialize() {
    auto& state = get_logger_state();
    if (state.initialized) {
        return;
    }
    
    // Initialize dotenv first
    dotenv::init();
    
    LoggingConfig config;
    
    // 默认关闭日志，需要显式开启
    config.enabled = get_env_var("LOG_ENABLED", "false") == "true";
    
    // 如果日志被禁用，创建最小配置并返回
    if (!config.enabled) {
        config.console = false;
        config.file_enabled = false;
        config.async = false;
        state.config = config;
        state.initialized = true;
        return;
    }
    
    config.level = get_env_var("LOG_LEVEL", "info");
    config.console = get_env_var("LOG_CONSOLE", "true") == "true";
    config.file_enabled = get_env_var("LOG_FILE_ENABLED", "true") == "true";
    config.async = get_env_var("LOG_ASYNC", "true") == "true";
    config.max_files = std::stoi(get_env_var("LOG_MAX_FILES", "30"));
    config.file_dir = get_env_var("LOG_FILE_DIR", "logs");
    config.pattern = get_env_var("LOG_PATTERN", "[%Y-%m-%d %H:%M:%S.%f] [%^%l%$] [%n] [%t] [%s:%#] %v");
    
    // 刷新级别配置
    config.flush_level = get_env_var("LOG_FLUSH_LEVEL", "error");

    state.config = config;
    state.initialized = true;
}

void Logger::initialize(const LoggingConfig& config) {
    auto& state = get_logger_state();
    if (state.initialized) {
        return;
    }
    
    state.config = config;
    state.initialized = true;
}

std::shared_ptr<spdlog::logger> Logger::register_logger(
    const std::string& logger_name,
    const std::string& file_path,
    const LoggingConfig& config) {
    
    auto& state = get_logger_state();
    std::lock_guard<std::mutex> lock(state.loggers_mutex);
    
    // if logger already exists, return existing
    auto it = state.loggers.find(logger_name);
    if (it != state.loggers.end()) {
        return it->second;
    }
    
    // if not specified file_path, use logger_name as default path
    std::string effective_file_path = file_path.empty() ? 
        (state.config.file_dir + "/" + logger_name) : file_path;
    
    auto logger = create_logger(logger_name, effective_file_path, config);
    state.loggers[logger_name] = logger;
    
    return logger;
}

std::shared_ptr<spdlog::logger> Logger::get_logger(const std::string& logger_name) {
    auto& state = get_logger_state();
    
    if (!state.initialized) {
        initialize();
    }
    

    if (!state.config.enabled) {
        static auto null_logger = std::make_shared<spdlog::logger>("null", std::make_shared<spdlog::sinks::null_sink_mt>());
        return null_logger;
    }
    
    {
        std::lock_guard<std::mutex> lock(state.loggers_mutex);
        auto it = state.loggers.find(logger_name);
        if (it != state.loggers.end()) {
            return it->second;
        }
    }
    
    auto logger = register_logger(logger_name, state.config.file_dir + "/" + logger_name, state.config);
    return logger;
}

std::shared_ptr<spdlog::logger> Logger::create_logger(
    const std::string& name, 
    const std::string& file_path,
    const LoggingConfig& config) {
    
    // 如果日志被禁用，返回空日志器
    if (!config.enabled) {
        return std::make_shared<spdlog::logger>("null", std::make_shared<spdlog::sinks::null_sink_mt>());
    }
    
    std::vector<spdlog::sink_ptr> sinks;
    
    // Console sink
    if (config.console) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern(config.pattern);
        sinks.push_back(console_sink);
    }
    
    // File sink
    if (config.file_enabled && !file_path.empty()) {
        std::string parent_dir = get_parent_path(file_path);
        
        if (!create_directories(parent_dir)) {
            std::cerr << "Warning: Failed to create log directory: " << parent_dir << std::endl;
        }
        
        std::string full_log_path = file_path + ".log";
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            full_log_path, 0, 0, false, config.max_files);
        file_sink->set_pattern(config.pattern);
        sinks.push_back(file_sink);
    }
    
    std::shared_ptr<spdlog::logger> logger;
    
    if (sinks.empty()) {
        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        sinks.push_back(null_sink);
    }
    
    if (config.async) {
        static std::once_flag async_init_flag;
        std::call_once(async_init_flag, []() {
            spdlog::init_thread_pool(8192, 1);
            std::atexit(cleanup_logger);
        });
        
        logger = std::make_shared<spdlog::async_logger>(name, 
                                                       sinks.begin(), sinks.end(),
                                                       spdlog::thread_pool(),
                                                       spdlog::async_overflow_policy::block);
    } else {
        logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    }
    
    logger->set_level(string_to_level(config.level));
    
    // 根据配置的刷新级别设置何时立即刷新到文件
    if (!config.flush_level.empty()) {
        spdlog::level::level_enum flush_level = string_to_level(config.flush_level);
        logger->flush_on(flush_level);
    } else {
        logger->flush_on(spdlog::level::off);
    }
    
    if (config.console || config.file_enabled) {
            logger->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::info,
                       "Logger '{}' initialized - Level: {}, Async: {}, Console: {}, File: {}, FlushLevel: {}", 
                       name, config.level, config.async, config.console, config.file_enabled ? "enabled" : "disabled", config.flush_level);
        
        // 立即刷新初始化日志
        logger->flush();
    }
    
    return logger;
}

spdlog::level::level_enum Logger::string_to_level(const std::string& level_str) {
    if (level_str == "trace") return spdlog::level::trace;
    if (level_str == "debug") return spdlog::level::debug;
    if (level_str == "info") return spdlog::level::info;
    if (level_str == "warn" || level_str == "warning") return spdlog::level::warn;
    if (level_str == "error") return spdlog::level::err;
    if (level_str == "critical") return spdlog::level::critical;
    return spdlog::level::info; // default
}

std::string Logger::get_env_var(const std::string& name, const std::string& default_value) {
    return dotenv::getenv(name.c_str(), default_value);
}

LoggerState* Logger::get_internal_state_ptr() {
    return &_internal_state;
}

bool Logger::is_enabled() {
    auto& state = get_logger_state();
    
    if (!state.initialized) {
        initialize();
    }
    
    return state.config.enabled;
}

NS_FT_END