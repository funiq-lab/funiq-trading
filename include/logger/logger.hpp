#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/null_sink.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "core/macros.h"
#include "laserpants/dotenv/dotenv.h"

NS_FT_BEGIN

static constexpr const char *DEFAULT_LOGGER_NAME = "server";

struct LoggingConfig
{
    std::string level = "info";
    bool console = true;
    bool file_enabled = true;
    std::string file_dir = "logs";
    bool async = true;
    int max_files = 30;
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%f] [%^%l%$] [%n] [%t] [%s:%#] %v";
    
    // 默认关闭日志
    bool enabled = false;             // 默认关闭，需要显式开启
    std::string flush_level = "error"; // 刷新级别：达到此级别及以上的日志会立即刷新到文件
    
    LoggingConfig() = default;
    LoggingConfig(const LoggingConfig &base) = default;
};

// Logger状态结构体，包含所有静态成员
struct LoggerState {
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;
    LoggingConfig config;
    std::mutex loggers_mutex;
    bool initialized = false;
};

class Logger
{
public:
    static void initialize();
    static void initialize(const LoggingConfig &config);

    static std::shared_ptr<spdlog::logger> register_logger(
        const std::string &logger_name,
        const std::string &file_path = "",
        const LoggingConfig &config = {});
    static std::shared_ptr<spdlog::logger> get_logger(const std::string &logger_name);

    // 在模板函数开头检查日志是否启用
    template <typename... Args>
    static void trace(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::trace, fmt, std::forward<Args>(args)...);
    }

    static void trace(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::trace, msg);
    }

    template <typename... Args>
    static void debug(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::debug, fmt, std::forward<Args>(args)...);
    }

    static void debug(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::debug, msg);
    }

    template <typename... Args>
    static void info(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::info, fmt, std::forward<Args>(args)...);
    }

    static void info(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::info, msg);
    }

    template <typename... Args>
    static void warn(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::warn, fmt, std::forward<Args>(args)...);
    }

    static void warn(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::warn, msg);
    }

    template <typename... Args>
    static void error(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::err, fmt, std::forward<Args>(args)...);
    }

    static void error(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::err, msg);
    }

    template <typename... Args>
    static void critical(const std::string &logger_name, const char *file, int line, const char *func, const char* fmt, Args &&...args)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::critical, fmt, std::forward<Args>(args)...);
    }

    static void critical(const std::string &logger_name, const char *file, int line, const char *func, const std::string &msg)
    {
        if (!is_enabled()) return;  // 立即返回
        get_logger(logger_name)->log(spdlog::source_loc{file, line, func}, spdlog::level::critical, msg);
    }

    // 获取内部状态指针
    static LoggerState* get_internal_state_ptr();

    // Logger状态替换接口
    static void set_logger_state(LoggerState* external_state);
    static void reset_logger_state();  // 恢复使用内部状态

    static bool is_enabled();

private:
    Logger() = delete;
    ~Logger() = delete;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    static std::shared_ptr<spdlog::logger> create_logger(
        const std::string &name,
        const std::string &file_path,
        const LoggingConfig &config);
    static spdlog::level::level_enum string_to_level(const std::string &level_str);
    static std::string get_env_var(const std::string &name, const std::string &default_value = "");

    static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> _loggers;
    static LoggingConfig _config;
    static std::mutex _loggers_mutex;
    static bool _initialized;

    // 使用指针间接访问静态成员
    static LoggerState& get_logger_state();
    
    // 内部状态（如果没有外部状态则使用这些）
    static LoggerState _internal_state;
    
    // 外部状态指针（如果设置了则使用外部状态）
    static LoggerState* _external_state;
};

NS_FT_END


#define MODULE_TRACE(module_name, ...) ft::Logger::trace(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MODULE_DEBUG(module_name, ...) ft::Logger::debug(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MODULE_INFO(module_name, ...) ft::Logger::info(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MODULE_WARN(module_name, ...) ft::Logger::warn(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MODULE_ERROR(module_name, ...) ft::Logger::error(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MODULE_CRITICAL(module_name, ...) ft::Logger::critical(module_name, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// system log macros
#define LOG_TRACE(...) MODULE_TRACE(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)
#define LOG_DEBUG(...) MODULE_DEBUG(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)
#define LOG_INFO(...) MODULE_INFO(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)
#define LOG_WARN(...) MODULE_WARN(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)
#define LOG_ERROR(...) MODULE_ERROR(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)
#define LOG_CRITICAL(...) MODULE_CRITICAL(ft::DEFAULT_LOGGER_NAME, __VA_ARGS__)


// strategy log macros
#define STRATEGY_LOG_TRACE(...) MODULE_TRACE("strategy", __VA_ARGS__)
#define STRATEGY_LOG_DEBUG(...) MODULE_DEBUG("strategy", __VA_ARGS__)
#define STRATEGY_LOG_INFO(...) MODULE_INFO("strategy", __VA_ARGS__)
#define STRATEGY_LOG_WARN(...) MODULE_WARN("strategy", __VA_ARGS__)
#define STRATEGY_LOG_ERROR(...) MODULE_ERROR("strategy", __VA_ARGS__)
#define STRATEGY_LOG_CRITICAL(...) MODULE_CRITICAL("strategy", __VA_ARGS__)

#define QUOTE_LOG_TRACE(...) MODULE_TRACE("quote", __VA_ARGS__)
#define QUOTE_LOG_DEBUG(...) MODULE_DEBUG("quote", __VA_ARGS__)
#define QUOTE_LOG_INFO(...) MODULE_INFO("quote", __VA_ARGS__)
#define QUOTE_LOG_WARN(...) MODULE_WARN("quote", __VA_ARGS__)
#define QUOTE_LOG_ERROR(...) MODULE_ERROR("quote", __VA_ARGS__)
#define QUOTE_LOG_CRITICAL(...) MODULE_CRITICAL("quote", __VA_ARGS__)

#define TRADE_LOG_TRACE(...) MODULE_TRACE("trade", __VA_ARGS__)
#define TRADE_LOG_DEBUG(...) MODULE_DEBUG("trade", __VA_ARGS__)
#define TRADE_LOG_INFO(...) MODULE_INFO("trade", __VA_ARGS__)
#define TRADE_LOG_WARN(...) MODULE_WARN("trade", __VA_ARGS__)
#define TRADE_LOG_ERROR(...) MODULE_ERROR("trade", __VA_ARGS__)
#define TRADE_LOG_CRITICAL(...) MODULE_CRITICAL("trade", __VA_ARGS__)

#define NETWORK_LOG_TRACE(...) MODULE_TRACE("network", __VA_ARGS__)
#define NETWORK_LOG_DEBUG(...) MODULE_DEBUG("network", __VA_ARGS__)
#define NETWORK_LOG_INFO(...) MODULE_INFO("network", __VA_ARGS__)
#define NETWORK_LOG_WARN(...) MODULE_WARN("network", __VA_ARGS__)
#define NETWORK_LOG_ERROR(...) MODULE_ERROR("network", __VA_ARGS__)
#define NETWORK_LOG_CRITICAL(...) MODULE_CRITICAL("network", __VA_ARGS__)

#define SCHEDULER_LOG_TRACE(...) MODULE_TRACE("scheduler", __VA_ARGS__)
#define SCHEDULER_LOG_DEBUG(...) MODULE_DEBUG("scheduler", __VA_ARGS__)
#define SCHEDULER_LOG_INFO(...) MODULE_INFO("scheduler", __VA_ARGS__)
#define SCHEDULER_LOG_WARN(...) MODULE_WARN("scheduler", __VA_ARGS__)
#define SCHEDULER_LOG_ERROR(...) MODULE_ERROR("scheduler", __VA_ARGS__)
#define SCHEDULER_LOG_CRITICAL(...) MODULE_CRITICAL("scheduler", __VA_ARGS__)