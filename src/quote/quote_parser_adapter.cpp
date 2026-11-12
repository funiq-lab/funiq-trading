#include "quote/quote_parser_adapter.h"
#include "utils/config.h"
#include <cstring>
#include "logger/logger.hpp"
#include <nlohmann/json.hpp>

NS_FT_BEGIN

QuoteParserAdapter::QuoteParserAdapter()
    : _parser_api(nullptr), _create_parser_func(nullptr), _destroy_parser_func(nullptr), _running(false), _stopped(false), _parser_lib(nullptr)
{
}

QuoteParserAdapter::~QuoteParserAdapter()
{
    release();
}

bool QuoteParserAdapter::init(const char *id, const nlohmann::json *config)
{
    if (!id || !config)
    {
        _last_error = "Invalid parameters";
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }

    try
    {
        _id = id;
        _config = *config; // nlohmann::json支持直接拷贝

        // 验证基本配置
        if (!_config.contains("module") || !_config["module"].is_string())
        {
            _last_error = "Missing or invalid 'module' in config";
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        std::string module_path = _config["module"].get<std::string>();
        QUOTE_LOG_INFO("Initializing parser {} with module: {}", _id, module_path);

        // 加载解析器库
        if (!_load_parser_library(module_path))
        {
            return false;
        }

        // 创建解析器实例
        if (!_create_parser_instance())
        {
            _unload_parser_library();
            return false;
        }

        QUOTE_LOG_INFO("Parser adapter {} initialized successfully", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during initialization: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

bool QuoteParserAdapter::run()
{
    if (!_parser_api)
    {
        _last_error = "Parser not initialized";
        return false;
    }

    if (_running.load())
    {
        QUOTE_LOG_INFO("Parser {} already running", _id);
        return true;
    }

    try
    {
        // 如果之前已经初始化过，只需要重新启动
        // 如果是第一次运行，需要先初始化
        if (!_parser_api->is_initialized())
        {
            // 首次运行，需要初始化
            QuoteConfig config = _build_quote_config();
            int init_result = _parser_api->init(&config);
            _free_quote_config_memory(config);

            if (init_result != 0)
            {
                _last_error = "Failed to initialize parser: " + _parser_api->get_last_error();
                QUOTE_LOG_ERROR(_last_error);
                return false;
            }
        }

        // 启动解析器
        int start_result = _parser_api->start();
        if (start_result != 0)
        {
            _last_error = "Failed to start parser: " + _parser_api->get_last_error();
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        int connect_result = _parser_api->connect();
        if (connect_result != 0)
        {
            _last_error = "Failed to connect parser: " + _parser_api->get_last_error();
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        _running.store(true);
        QUOTE_LOG_INFO("Parser {} started successfully", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during start: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

bool QuoteParserAdapter::stop()
{
    if (!_parser_api)
    {
        _last_error = "Parser not initialized";
        return false;
    }

    if (!_running.load())
    {
        QUOTE_LOG_INFO("Parser {} already stopped", _id);
        return true;
    }

    try
    {
        // 只停止parser，不释放资源
        int stop_result = _parser_api->stop();
        if (stop_result != 0)
        {
            _last_error = "Failed to stop parser: " + _parser_api->get_last_error();
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        _running.store(false);
        QUOTE_LOG_INFO("Parser {} stopped successfully (can be restarted)", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during stop: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

bool QuoteParserAdapter::is_running() const {
    return _running.load();
}

void QuoteParserAdapter::release()
{
    _stopped = true;

    try
    {
        _full_unload();

        QUOTE_LOG_INFO("Parser {} released", _id);
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception during release: {}", e.what());
    }
}

// ===== 私有方法实现 =====

bool QuoteParserAdapter::_load_parser_library(const std::string &module_path)
{
    try
    {
        // 使用 wrap_module 包装库文件名
        std::string wrapped_path = DLLHelper::wrap_module(module_path.c_str());

        // 使用 DLLHelper 加载动态库
        _parser_lib = DLLHelper::load_library(wrapped_path.c_str());
        if (!_parser_lib)
        {
            _last_error = "Failed to load library: " + wrapped_path;
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        // 获取工厂函数
        _create_parser_func = (CreateQuoteParserFuncType)DLLHelper::get_symbol(_parser_lib, "create_quote_parser");
        _destroy_parser_func = (DeleteQuoteParserFuncType)DLLHelper::get_symbol(_parser_lib, "delete_quote_parser");

        if (!_create_parser_func || !_destroy_parser_func)
        {
            _last_error = "Required factory functions not found in library: " + wrapped_path;
            QUOTE_LOG_ERROR(_last_error);
            DLLHelper::free_library(_parser_lib);
            _parser_lib = nullptr;
            return false;
        }

        QUOTE_LOG_INFO("Loaded parser library: {}", wrapped_path);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Failed to load parser library: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

void QuoteParserAdapter::_unload_parser_library()
{
    try
    {
        if (_parser_lib)
        {
            DLLHelper::free_library(_parser_lib);
            _parser_lib = nullptr;
            _create_parser_func = nullptr;
            _destroy_parser_func = nullptr;
            QUOTE_LOG_INFO("Parser library unloaded for {}", _id);
        }
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception unloading library: {}", e.what());
    }
}


void QuoteParserAdapter::_full_unload()
{
    // 1) 停止运行
    if (_parser_api && _running.load()) {
        (void)_parser_api->stop();
        _running.store(false);
        // 等待内部线程完全退出，避免仍在执行库内代码时卸载动态库
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    // 2) 释放内部资源
    if (_parser_api) {
        _parser_api->release();
        // 再次小幅等待，确保释放完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 3) 销毁实例
    if (_destroy_parser_func && _parser_api) {
        _destroy_parser_instance();
    }
    // 4) 卸载动态库
    _unload_parser_library();
}

bool QuoteParserAdapter::_create_parser_instance()
{
    if (!_create_parser_func)
    {
        _last_error = "Parser factory function not available";
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }

    try
    {
        // ===== 传递整个Logger状态到动态库 =====
        typedef void (*SetLoggerStateFuncType)(void *);
        auto set_logger_state_func = (SetLoggerStateFuncType)DLLHelper::get_symbol(_parser_lib, "set_logger_state");

        if (set_logger_state_func)
        {
            // 获取主程序的Logger状态（通过访问Logger内部状态）
            // 这里需要Logger提供一个获取内部状态指针的方法
            auto *main_logger_state = ft::Logger::get_internal_state_ptr();
            set_logger_state_func(main_logger_state);
            QUOTE_LOG_INFO("Successfully set main logger state to parser library {}", _id);
        }
        else
        {
            QUOTE_LOG_WARN("Parser library {} doesn't support logger state setting", _id);
        }

        // 创建解析器实例
        _parser_api = _create_parser_func();
        if (!_parser_api)
        {
            _last_error = "Failed to create parser instance";
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }

        QUOTE_LOG_INFO("Parser instance created for {}", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception creating parser: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

void QuoteParserAdapter::_destroy_parser_instance()
{
    try
    {
        // 先重置logger状态
        typedef void (*ResetLoggerStateFuncType)();
        auto reset_logger_state_func = (ResetLoggerStateFuncType)DLLHelper::get_symbol(_parser_lib, "reset_logger_state");
        if (reset_logger_state_func)
        {
            reset_logger_state_func();
        }

        // 先检查函数指针是否有效
        if (!_destroy_parser_func)
        {
            QUOTE_LOG_WARN("Destroy parser function not available for {}", _id);
            return;
        }

        // 再检查解析器实例是否有效
        if (!_parser_api)
        {
            QUOTE_LOG_WARN("Parser instance already destroyed for {}", _id);
            return;
        }

        if (!_parser_lib)
        {
            QUOTE_LOG_WARN("Parser library already unloaded for {}, cannot destroy instance safely", _id);
            _parser_api = nullptr;
            return;
        }

        // 调用销毁函数
        _destroy_parser_func(_parser_api);
        _parser_api = nullptr;
        QUOTE_LOG_INFO("Parser instance destroyed for {}", _id);
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception destroying parser: {}", e.what());
    }
}

QuoteConfig QuoteParserAdapter::_build_quote_config()
{
    QuoteConfig config;
    memset(&config, 0, sizeof(config));

    try
    {
        // ===== 数据输出配置 =====
        _parse_data_config(_config, config.data_config);

        // ===== 其他配置 =====
        if (_config.contains("config") && _config["config"].is_object())
        {
            const auto &cfg = _config["config"];

            // 回调模式配置
            config.callback_mode = false;
            if (cfg.contains("callback_mode") && cfg["callback_mode"].is_boolean())
            {
                config.callback_mode = cfg["callback_mode"].get<bool>();
            }

            // 扩展配置文件路径（由具体parser使用）
            if (cfg.contains("extended_config_file") && cfg["extended_config_file"].is_string())
            {
                std::string extended_config = cfg["extended_config_file"].get<std::string>();
                strncpy(config.extended_config_file, extended_config.c_str(),
                        sizeof(config.extended_config_file) - 1);
            }

            // ===== 性能统计配置 =====
            _parse_performance_config(cfg, config.performance);

            // ===== Mock模式配置 =====
            _parse_mock_config(cfg, config.mock);

            // ===== 订阅配置 =====
            _parse_subscription_config(cfg, config.subscription);

            if (config.callback_mode)
            {
                QUOTE_LOG_INFO("Callback mode enabled");
            }

            // 输出配置信息
            if (config.performance.enabled)
            {
                QUOTE_LOG_INFO("Performance stats enabled - interval: {}ms, latency: {}",
                               config.performance.report_interval_ms,
                               config.performance.enable_latency_stats);
            }

            if (config.mock.enabled)
            {
                QUOTE_LOG_INFO("Mock mode enabled - file: {}, loop: {}",
                               config.mock.mock_data_file,
                               config.mock.loop_replay);
            }
        }
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception building quote config: {}", e.what());
        // 清理已分配的内存
        _free_quote_config_memory(config);
        memset(&config, 0, sizeof(config));
    }

    return config;
}

void QuoteParserAdapter::_parse_performance_config(const nlohmann::json &config, PerformanceConfig &perf_config)
{
    // 初始化默认值
    perf_config.enabled = false;
    perf_config.report_interval_ms = 5000;
    perf_config.enable_latency_stats = false;

    if (!config.contains("performance") || !config["performance"].is_object())
    {
        return;
    }

    const auto &perf = config["performance"];

    if (perf.contains("enabled") && perf["enabled"].is_boolean())
    {
        perf_config.enabled = perf["enabled"].get<bool>();
    }

    if (perf.contains("report_interval_ms") && perf["report_interval_ms"].is_number_unsigned())
    {
        perf_config.report_interval_ms = perf["report_interval_ms"].get<uint32_t>();
        // 限制在合理范围内
        if (perf_config.report_interval_ms < 100)
        {
            perf_config.report_interval_ms = 100;
        }
        else if (perf_config.report_interval_ms > 60000)
        {
            perf_config.report_interval_ms = 60000;
        }
    }

    if (perf.contains("enable_latency_stats") && perf["enable_latency_stats"].is_boolean())
    {
        perf_config.enable_latency_stats = perf["enable_latency_stats"].get<bool>();
    }

}

void QuoteParserAdapter::_parse_mock_config(const nlohmann::json &config, MockConfig &mock_config)
{
    // 初始化默认值
    mock_config.enabled = false;
    memset(mock_config.mock_data_file, 0, sizeof(mock_config.mock_data_file));
    mock_config.loop_replay = true; // 默认循环播放
    
    // 初始化CSV相关字段
    mock_config.csv_mode = false;
    memset(mock_config.csv_depth_file, 0, sizeof(mock_config.csv_depth_file));
    memset(mock_config.csv_index_file, 0, sizeof(mock_config.csv_index_file));
    memset(mock_config.csv_entrust_file, 0, sizeof(mock_config.csv_entrust_file));
    memset(mock_config.csv_trade_file, 0, sizeof(mock_config.csv_trade_file));
    memset(mock_config.csv_depth_fifty_file, 0, sizeof(mock_config.csv_depth_fifty_file));

    if (!config.contains("mock") || !config["mock"].is_object())
    {
        return;
    }

    const auto &mock = config["mock"];

    if (mock.contains("enabled") && mock["enabled"].is_boolean())
    {
        mock_config.enabled = mock["enabled"].get<bool>();
    }

    if (mock.contains("mock_data_file") && mock["mock_data_file"].is_string())
    {
        std::string mock_file = mock["mock_data_file"].get<std::string>();
        strncpy(mock_config.mock_data_file, mock_file.c_str(),
                sizeof(mock_config.mock_data_file) - 1);
    }

    if (mock.contains("loop_replay") && mock["loop_replay"].is_boolean())
    {
        mock_config.loop_replay = mock["loop_replay"].get<bool>();
    }
    
    // ===== 解析CSV配置 =====
    if (mock.contains("csv_mode") && mock["csv_mode"].is_boolean())
    {
        mock_config.csv_mode = mock["csv_mode"].get<bool>();
    }
    
    // 解析各种CSV文件路径
    if (mock.contains("csv_files") && mock["csv_files"].is_object())
    {
        const auto &csv_files = mock["csv_files"];
        
        if (csv_files.contains("depth") && csv_files["depth"].is_string())
        {
            std::string csv_file = csv_files["depth"].get<std::string>();
            strncpy(mock_config.csv_depth_file, csv_file.c_str(),
                    sizeof(mock_config.csv_depth_file) - 1);
        }
        
        if (csv_files.contains("index") && csv_files["index"].is_string())
        {
            std::string csv_file = csv_files["index"].get<std::string>();
            strncpy(mock_config.csv_index_file, csv_file.c_str(),
                    sizeof(mock_config.csv_index_file) - 1);
        }
        
        if (csv_files.contains("entrust") && csv_files["entrust"].is_string())
        {
            std::string csv_file = csv_files["entrust"].get<std::string>();
            strncpy(mock_config.csv_entrust_file, csv_file.c_str(),
                    sizeof(mock_config.csv_entrust_file) - 1);
        }
        
        if (csv_files.contains("trade") && csv_files["trade"].is_string())
        {
            std::string csv_file = csv_files["trade"].get<std::string>();
            strncpy(mock_config.csv_trade_file, csv_file.c_str(),
                    sizeof(mock_config.csv_trade_file) - 1);
        }
        
        if (csv_files.contains("depth_fifty") && csv_files["depth_fifty"].is_string())
        {
            std::string csv_file = csv_files["depth_fifty"].get<std::string>();
            strncpy(mock_config.csv_depth_fifty_file, csv_file.c_str(),
                    sizeof(mock_config.csv_depth_fifty_file) - 1);
        }
    }

}

void QuoteParserAdapter::_parse_subscription_config(const nlohmann::json &config, SubscriptionConfig &sub_config)
{
    // 初始化默认值
    memset(&sub_config, 0, sizeof(sub_config));

    try
    {
        // 市场配置 - 支持字符串数组
        if (config.contains("markets") && config["markets"].is_array())
        {
            const auto &markets = config["markets"];
            int count = std::min((int)markets.size(), 8);

            if (count > 0)
            {
                sub_config.markets = new MarketType[count];
                sub_config.market_count = 0; // 实际有效数量

                int i = 0;
                for (const auto &market : markets)
                {
                    if (i >= count)
                        break;

                    MarketType market_type = MARKET_UNKNOWN;

                    if (market.is_string())
                    {
                        // 使用语义化转换
                        market_type = parse_market_type(market.get<std::string>().c_str());
                    }
                    else if (market.is_number_integer())
                    {
                        // 兼容数值配置
                        market_type = (MarketType)market.get<int>();
                    }

                    if (market_type != MARKET_UNKNOWN)
                    {
                        sub_config.markets[sub_config.market_count++] = market_type;
                    }
                    else
                    {
                        QUOTE_LOG_WARN("Invalid market config at index {}", i);
                    }
                    i++;
                }
            }
        }

        // 数据类型配置 - 支持字符串数组
        if (config.contains("data_types") && config["data_types"].is_array())
        {
            const auto &data_types = config["data_types"];
            int count = std::min((int)data_types.size(), 16);

            if (count > 0)
            {
                sub_config.data_types = new FTDataType[count];
                sub_config.data_type_count = 0; // 实际有效数量

                int i = 0;
                for (const auto &data_type : data_types)
                {
                    if (i >= count)
                        break;

                    FTDataType dtype = DATA_TYPE_INDEX;

                    if (data_type.is_string())
                    {
                        // 使用语义化转换
                        dtype = parse_data_type(data_type.get<std::string>().c_str());
                    }
                    else if (data_type.is_number_integer())
                    {
                        // 兼容数值配置
                        dtype = (FTDataType)data_type.get<int>();
                    }

                    sub_config.data_types[sub_config.data_type_count++] = dtype;
                    i++;
                }
            }
        }

        // 证券配置 - code必须
        if (config.contains("securities") && config["securities"].is_array())
        {
            const auto &securities = config["securities"];
            // 最大1024个证券，如果超过1024则订阅所有
            int count = (int)securities.size();
            if (count > 1024)
            {
                QUOTE_LOG_WARN("Too many securities configured ({}), will subscribe to all", count);
            }
            else
            {
                if (count > 0)
                {
                    sub_config.securities = new SecurityInfoInConfig[count];
                    sub_config.security_count = 0; // 实际有效数量

                    for (int i = 0; i < count; i++)
                    {
                        // 初始化结构体
                        SecurityInfoInConfig &security_config = sub_config.securities[sub_config.security_count];
                        memset(&security_config, 0, sizeof(SecurityInfoInConfig));

                        if (securities[i].is_object())
                        {
                            const auto &security = securities[i];

                            // 处理证券代码（必须）
                            if (security.contains("code") && security["code"].is_string() &&
                                security["code"].get<std::string>().length() > 0)
                            {

                                strncpy(security_config.code, security["code"].get<std::string>().c_str(),
                                        sizeof(security_config.code) - 1);

                                sub_config.security_count++;
                            }
                            else
                            {
                                QUOTE_LOG_WARN("Security config missing required 'code' field at index {}", i);
                            }
                        }
                        else if (securities[i].is_string())
                        {
                            // 兼容简单字符串格式（只有code）
                            const char *code_str = securities[i].get<std::string>().c_str();
                            if (strlen(code_str) > 0)
                            {
                                strncpy(security_config.code, code_str, sizeof(security_config.code) - 1);
                                sub_config.security_count++;
                            }
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception parsing subscription config: {}", e.what());
        // 清理已分配的内存
        if (sub_config.markets)
        {
            delete[] sub_config.markets;
            sub_config.markets = nullptr;
            sub_config.market_count = 0;
        }
        if (sub_config.data_types)
        {
            delete[] sub_config.data_types;
            sub_config.data_types = nullptr;
            sub_config.data_type_count = 0;
        }
        if (sub_config.securities)
        {
            delete[] sub_config.securities;
            sub_config.securities = nullptr;
            sub_config.security_count = 0;
        }
    }
}

void QuoteParserAdapter::_parse_data_config(const nlohmann::json &config, DataConfig &data_config)
{
    // 初始化默认值
    memset(&data_config, 0, sizeof(data_config));
    data_config.output_type = DATA_OUTPUT_NONE;

    if (!config.contains("data") || !config["data"].is_array())
    {
        return;
    }

    const auto &data_configs = config["data"];

    // 查找第一个active的数据输出配置
    for (size_t i = 0; i < data_configs.size(); ++i)
    {
        const auto &data_cfg = data_configs[i];
        if (!data_cfg.is_object())
        {
            continue;
        }

        // 检查是否激活
        bool is_active = false;
        if (data_cfg.contains("active") && data_cfg["active"].is_boolean())
        {
            is_active = data_cfg["active"].get<bool>();
        }

        if (!is_active)
            continue;

        // 解析输出类型
        DataOutputType output_type = DATA_OUTPUT_NONE;
        if (data_cfg.contains("type") && data_cfg["type"].is_string())
        {
            std::string type_str = data_cfg["type"].get<std::string>();
            output_type = parse_data_output_type(type_str.c_str());
        }

        if (output_type == DATA_OUTPUT_NONE)
        {
            QUOTE_LOG_WARN("Invalid data output type in config at index {}", i);
            continue;
        }

        // 解析对应的配置
        if (!data_cfg.contains("config") || !data_cfg["config"].is_object())
        {
            QUOTE_LOG_WARN("No config found for data output at index {}", i);
            continue;
        }

        const auto &output_cfg = data_cfg["config"];
        bool config_valid = false;

        // 设置输出类型
        data_config.output_type = output_type;

        switch (output_type)
        {
        case DATA_OUTPUT_SHARED_MEMORY:
        {
            // 设置默认值
            strncpy(data_config.shm_name, ("quote_shm_" + _id).c_str(),
                    sizeof(data_config.shm_name) - 1);

            // 解析配置
            if (output_cfg.contains("name") && output_cfg["name"].is_string())
            {
                std::string shm_name = output_cfg["name"].get<std::string>();
                strncpy(data_config.shm_name, shm_name.c_str(),
                        sizeof(data_config.shm_name) - 1);
            }

            QUOTE_LOG_INFO("Configured shared memory data output: {}", data_config.shm_name);
            config_valid = true;
        }
        break;

        case DATA_OUTPUT_UDP_BROADCAST:
        {
            // 设置默认值
            data_config.udp_port = 9999;
            strncpy(data_config.udp_multicast, "224.0.0.1",
                    sizeof(data_config.udp_multicast) - 1);

            // 解析配置
            if (output_cfg.contains("port") && output_cfg["port"].is_number_integer())
            {
                data_config.udp_port = output_cfg["port"].get<int>();
            }

            if (output_cfg.contains("multicast_group") && output_cfg["multicast_group"].is_string())
            {
                std::string multicast_group = output_cfg["multicast_group"].get<std::string>();
                strncpy(data_config.udp_multicast, multicast_group.c_str(),
                        sizeof(data_config.udp_multicast) - 1);
            }

            config_valid = true;
        }
        break;

        case DATA_OUTPUT_FILE:
        {
            // 设置默认值
            strncpy(data_config.file_path, "./data/quote_data",
                    sizeof(data_config.file_path) - 1);
            strncpy(data_config.file_format, "binary",
                    sizeof(data_config.file_format) - 1);

            // 解析配置
            if (output_cfg.contains("path") && output_cfg["path"].is_string())
            {
                std::string path = output_cfg["path"].get<std::string>();
                strncpy(data_config.file_path, path.c_str(),
                        sizeof(data_config.file_path) - 1);
            }

            if (output_cfg.contains("format") && output_cfg["format"].is_string())
            {
                std::string format = output_cfg["format"].get<std::string>();
                strncpy(data_config.file_format, format.c_str(),
                        sizeof(data_config.file_format) - 1);
            }

            config_valid = true;
        }
        break;

        default:
            QUOTE_LOG_WARN("Unsupported data output type: {}", (int)output_type);
            break;
        }

        if (config_valid)
        {
            QUOTE_LOG_INFO("Successfully configured data output type: {} ({})",
                           data_output_type_to_string(output_type), (int)output_type);
            return; // 使用第一个有效的激活配置
        }
    }

    QUOTE_LOG_WARN("No valid active data configuration found, data_manager will not be initialized");
}

void QuoteParserAdapter::_free_quote_config_memory(QuoteConfig &config)
{
    // 释放订阅配置中的动态分配内存
    SubscriptionConfig &sub_config = config.subscription;

    // 释放市场列表
    if (sub_config.markets)
    {
        delete[] sub_config.markets;
        sub_config.markets = nullptr;
        sub_config.market_count = 0;
    }

    // 释放数据类型列表
    if (sub_config.data_types)
    {
        delete[] sub_config.data_types;
        sub_config.data_types = nullptr;
        sub_config.data_type_count = 0;
    }

    // 释放证券列表
    if (sub_config.securities)
    {
        delete[] sub_config.securities;
        sub_config.securities = nullptr;
        sub_config.security_count = 0;
    }
}

NS_FT_END