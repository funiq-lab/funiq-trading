#include "trade/trade_handler_adapter.h"
#include "utils/config.h"
#include <cstring>
#include "logger/logger.hpp"
#include <nlohmann/json.hpp>
#include <set>

NS_FT_BEGIN

TradeHandlerAdapter::TradeHandlerAdapter()
    : _handler_api(nullptr), _create_handler_func(nullptr), _destroy_handler_func(nullptr), _running(false), _stopped(false), _handler_lib(nullptr)
{
}

TradeHandlerAdapter::~TradeHandlerAdapter()
{
    release();
}

bool TradeHandlerAdapter::init(const char *id, const nlohmann::json *config)
{
    if (!id || !config)
    {
        _last_error = "Invalid parameters";
        TRADE_LOG_ERROR(_last_error);
        return false;
    }

    try
    {
        _id = id;
        _config = *config;

        // 验证基本配置
        if (!_config.contains("module") || !_config["module"].is_string())
        {
            _last_error = "Missing or invalid 'module' in config";
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        std::string module_path = _config["module"].get<std::string>();
        TRADE_LOG_INFO("Initializing trade handler {} with module: {}", _id, module_path);

        // 加载处理器库
        if (!_load_handler_library(module_path))
        {
            return false;
        }

        // 创建处理器实例
        if (!_create_handler_instance())
        {
            _unload_handler_library();
            return false;
        }

        TRADE_LOG_INFO("Trade handler adapter {} initialized successfully", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during initialization: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

bool TradeHandlerAdapter::run()
{
    if (!_handler_api)
    {
        _last_error = "Handler not initialized";
        return false;
    }

    if (_running.load())
    {
        TRADE_LOG_INFO("Handler {} already running", _id);
        return true;
    }

    try
    {
        if (!_handler_api->is_initialized())
        {
            // 首先初始化处理器（包含账户配置）
            TradeConfig config = _build_trade_config();
            int init_result = _handler_api->init(&config);

            // 记住要释放配置内存
            _free_trade_config_memory(config);

            if (init_result != 0)
            {
                _last_error = "Failed to initialize handler: " + _handler_api->get_last_error();
                TRADE_LOG_ERROR(_last_error);
                return false;
            }
        }

        // 然后启动处理器
        int start_result = _handler_api->start();
        if (start_result != 0)
        {
            _last_error = "Failed to start handler: " + _handler_api->get_last_error();
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        // 连接交易服务器
        int connect_result = _handler_api->connect();
        if (connect_result != 0)
        {
            _last_error = "Failed to connect handler: " + _handler_api->get_last_error();
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        _running.store(true);
        TRADE_LOG_INFO("Trade handler {} started successfully", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during start: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

bool TradeHandlerAdapter::stop()
{
    if (!_handler_api)
    {
        _last_error = "Handler not initialized";
        return false;
    }

    if (!_running.load())
    {
        TRADE_LOG_INFO("Handler {} already stopped", _id);
        return true;
    }

    try
    {
        // 1. 先断开连接
        int disconnect_result = _handler_api->disconnect();
        if (disconnect_result != 0)
        {
            TRADE_LOG_WARN("Failed to disconnect handler {}: {}", _id, _handler_api->get_last_error());
            // 继续执行stop，即使disconnect失败
        }

        // 2. 再停止处理器
        int stop_result = _handler_api->stop();
        if (stop_result != 0)
        {
            _last_error = "Failed to stop handler: " + _handler_api->get_last_error();
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        _running.store(false);
        TRADE_LOG_INFO("Trade handler {} stopped successfully", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception during stop: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

bool TradeHandlerAdapter::is_running() const
{
    return _running.load();
}

void TradeHandlerAdapter::release()
{
    _stopped = true;

    try
    {
        // 1. 先停止处理器
        if (_handler_api && _running.load())
        {
            _handler_api->stop();
            _running.store(false);
        }

        // 2. 再释放处理器资源
        if (_handler_api)
        {
            _handler_api->release();
        }

        // 3. 销毁处理器实例
        if (_destroy_handler_func && _handler_api)
        {
            _destroy_handler_instance();
        }

        // 4. 最后卸载库
        _unload_handler_library();

        TRADE_LOG_INFO("Trade handler {} released", _id);
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception during release: {}", e.what());
    }
}

bool TradeHandlerAdapter::is_connected() const
{
    return _handler_api ? _handler_api->is_connected() : false;
}

// ===== 私有方法实现 =====

bool TradeHandlerAdapter::_load_handler_library(const std::string &module_path)
{
    try
    {
        // 使用 wrap_module 包装库文件名
        std::string wrapped_path = DLLHelper::wrap_module(module_path.c_str());

        // 使用 DLLHelper 加载动态库
        _handler_lib = DLLHelper::load_library(wrapped_path.c_str());
        if (!_handler_lib)
        {
            _last_error = "Failed to load library: " + wrapped_path;
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        // 获取工厂函数
        _create_handler_func = (CreateTradeHandlerFuncType)DLLHelper::get_symbol(_handler_lib, "create_trade_handler");
        _destroy_handler_func = (DeleteTradeHandlerFuncType)DLLHelper::get_symbol(_handler_lib, "delete_trade_handler");

        if (!_create_handler_func || !_destroy_handler_func)
        {
            _last_error = "Required factory functions not found in library: " + wrapped_path;
            TRADE_LOG_ERROR(_last_error);
            DLLHelper::free_library(_handler_lib);
            _handler_lib = nullptr;
            return false;
        }

        TRADE_LOG_INFO("Loaded trade handler library: {}", wrapped_path);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Failed to load handler library: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

void TradeHandlerAdapter::_unload_handler_library()
{
    try
    {
        if (_handler_lib)
        {
            DLLHelper::free_library(_handler_lib);
            _handler_lib = nullptr;
            _create_handler_func = nullptr;
            _destroy_handler_func = nullptr;
            TRADE_LOG_INFO("Trade handler library unloaded for {}", _id);
        }
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception unloading library: {}", e.what());
    }
}

bool TradeHandlerAdapter::_create_handler_instance()
{
    if (!_create_handler_func)
    {
        _last_error = "Handler factory function not available";
        TRADE_LOG_ERROR(_last_error);
        return false;
    }

    try
    {
        // ===== 传递整个Logger状态到动态库 =====
        typedef void (*SetLoggerStateFuncType)(void *);
        auto set_logger_state_func = (SetLoggerStateFuncType)DLLHelper::get_symbol(_handler_lib, "set_logger_state");

        if (set_logger_state_func)
        {
            // 获取主程序的Logger状态（通过访问Logger内部状态）
            // 这里需要Logger提供一个获取内部状态指针的方法
            auto *main_logger_state = ft::Logger::get_internal_state_ptr();
            set_logger_state_func(main_logger_state);
            TRADE_LOG_INFO("Successfully set main logger state to trade handler library {}", _id);
        }
        else
        {
            TRADE_LOG_WARN("Trade handler library {} doesn't support logger state setting", _id);
        }

        // 创建处理器实例
        _handler_api = _create_handler_func();
        if (!_handler_api)
        {
            _last_error = "Failed to create handler instance";
            TRADE_LOG_ERROR(_last_error);
            return false;
        }

        TRADE_LOG_INFO("Trade handler instance created successfully for {}", _id);
        return true;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception creating handler instance: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

void TradeHandlerAdapter::_destroy_handler_instance()
{
    try
    {
        // 先重置logger状态
        typedef void (*ResetLoggerStateFuncType)();
        auto reset_logger_state_func = (ResetLoggerStateFuncType)DLLHelper::get_symbol(_handler_lib, "reset_logger_state");
        if (reset_logger_state_func)
        {
            reset_logger_state_func();
        }

        // 先检查函数指针是否有效
        if (!_destroy_handler_func)
        {
            TRADE_LOG_WARN("Destroy handler function not available for {}", _id);
            return;
        }

        // 再检查处理器实例是否有效
        if (!_handler_api)
        {
            TRADE_LOG_WARN("Handler instance already destroyed for {}", _id);
            return;
        }

        if (!_handler_lib)
        {
            TRADE_LOG_WARN("Handler library already unloaded for {}, cannot destroy instance safely", _id);
            _handler_api = nullptr;
            return;
        }

        // 调用销毁函数
        _destroy_handler_func(_handler_api);
        _handler_api = nullptr;
        TRADE_LOG_INFO("Trade handler instance destroyed for {}", _id);
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception destroying handler instance: {}", e.what());
    }
}

TradeConfig TradeHandlerAdapter::_build_trade_config()
{
    TradeConfig config;
    memset(&config, 0, sizeof(config));

    try
    {
        // 基本配置
        if (_config.contains("config"))
        {
            const auto &config_obj = _config["config"];

            // 扩展配置文件路径
            if (config_obj.contains("extended_config_file"))
            {
                std::string extended_file = config_obj["extended_config_file"].get<std::string>();
                strncpy(config.extended_config_file, extended_file.c_str(),
                        std::min(extended_file.length(), sizeof(config.extended_config_file) - 1));
            }

            // 回调模式（交易系统通常使用回调模式）
            config.callback_mode = true;
            if (config_obj.contains("callback_mode"))
            {
                config.callback_mode = config_obj["callback_mode"].get<bool>();
            }

            // 登陆方式
            config.login_mode = LOGIN_MODE_CUST_ID;
            if (config_obj.contains("login_mode"))
            {
                config.login_mode = (LoginMode)config_obj["login_mode"].get<int>();
            }

            // 解析账户配置
            if (config_obj.contains("accounts"))
            {
                if (!_parse_account_list_config(config_obj["accounts"], config.account_list))
                {
                    TRADE_LOG_ERROR("Failed to parse account list configuration");
                    return config;
                }
            }

            // 解析性能配置
            if (config_obj.contains("performance"))
            {
                _parse_performance_config(config_obj["performance"], config.performance);
            }
        }

        // 解析数据配置
        if (_config.contains("data"))
        {
            _parse_data_config(_config["data"], config.data_config);
        }

        _log_configuration_summary();
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception building trade config: {}", e.what());
        // 清理已分配的内存
        _free_trade_config_memory(config);
        memset(&config, 0, sizeof(config));
    }

    return config;
}

void TradeHandlerAdapter::_parse_data_config(const nlohmann::json &config, DataConfig &data_config)
{
    // 默认值
    data_config.output_type = DATA_OUTPUT_NONE;
    memset(data_config.shm_name, 0, sizeof(data_config.shm_name));
    memset(data_config.file_path, 0, sizeof(data_config.file_path));
    memset(data_config.file_format, 0, sizeof(data_config.file_format));
    data_config.udp_port = 0;
    memset(data_config.udp_multicast, 0, sizeof(data_config.udp_multicast));

    if (!config.is_array())
    {
        return;
    }

    for (const auto &data_item : config)
    {
        if (!data_item.contains("active") || !data_item["active"].get<bool>())
        {
            continue;
        }

        if (data_item.contains("type"))
        {
            std::string type = data_item["type"].get<std::string>();

            if (type == "shared_memory")
            {
                data_config.output_type = DATA_OUTPUT_SHARED_MEMORY;
                if (data_item.contains("config") && data_item["config"].contains("name"))
                {
                    std::string name = data_item["config"]["name"].get<std::string>();
                    strncpy(data_config.shm_name, name.c_str(),
                            std::min(name.length(), sizeof(data_config.shm_name) - 1));
                }
            }
            else if (type == "file")
            {
                data_config.output_type = DATA_OUTPUT_FILE;
                if (data_item.contains("config"))
                {
                    const auto &file_config = data_item["config"];
                    if (file_config.contains("path"))
                    {
                        std::string path = file_config["path"].get<std::string>();
                        strncpy(data_config.file_path, path.c_str(),
                                std::min(path.length(), sizeof(data_config.file_path) - 1));
                    }
                    if (file_config.contains("format"))
                    {
                        std::string format = file_config["format"].get<std::string>();
                        strncpy(data_config.file_format, format.c_str(),
                                std::min(format.length(), sizeof(data_config.file_format) - 1));
                    }
                }
            }
            else if (type == "udp")
            {
                data_config.output_type = DATA_OUTPUT_UDP_BROADCAST;
                if (data_item.contains("config"))
                {
                    const auto &udp_config = data_item["config"];
                    if (udp_config.contains("port"))
                    {
                        data_config.udp_port = udp_config["port"].get<int>();
                    }
                    if (udp_config.contains("multicast"))
                    {
                        std::string multicast = udp_config["multicast"].get<std::string>();
                        strncpy(data_config.udp_multicast, multicast.c_str(),
                                std::min(multicast.length(), sizeof(data_config.udp_multicast) - 1));
                    }
                }
            }
        }

        // 只处理第一个活跃的数据配置
        break;
    }
}

void TradeHandlerAdapter::_parse_performance_config(const nlohmann::json &config, PerformanceConfig &perf_config)
{
    // 默认值
    perf_config.enabled = false;
    perf_config.report_interval_ms = 1000;
    perf_config.enable_latency_stats = false;

    if (config.contains("enabled"))
    {
        perf_config.enabled = config["enabled"].get<bool>();
    }

    if (config.contains("report_interval_ms"))
    {
        perf_config.report_interval_ms = config["report_interval_ms"].get<uint32_t>();
    }

    if (config.contains("enable_latency_stats"))
    {
        perf_config.enable_latency_stats = config["enable_latency_stats"].get<bool>();
    }

    if (!_validate_performance_config(perf_config))
    {
        TRADE_LOG_WARN("Invalid performance config, using defaults");
        perf_config.enabled = false;
        perf_config.report_interval_ms = 1000;
        perf_config.enable_latency_stats = false;
    }
}

void TradeHandlerAdapter::_parse_account_config(const nlohmann::json &config, AccountInfo &account)
{
    memset(&account, 0, sizeof(account));

    if (config.contains("login_id"))
    {
        std::string login_id = config["login_id"].get<std::string>();
        strncpy(account.login_id, login_id.c_str(),
                std::min(login_id.length(), sizeof(account.login_id) - 1));
    }

    if (config.contains("cust_id"))
    {
        std::string cust_id = config["cust_id"].get<std::string>();
        strncpy(account.cust_id, cust_id.c_str(),
                std::min(cust_id.length(), sizeof(account.cust_id) - 1));
    }

    if (config.contains("fund_account_id"))
    {
        std::string fund_account_id = config["fund_account_id"].get<std::string>();
        strncpy(account.fund_account_id, fund_account_id.c_str(),
                std::min(fund_account_id.length(), sizeof(account.fund_account_id) - 1));
    }

    // 解析上海证券账户ID
    if (config.contains("account_id_sh"))
    {
        std::string account_id_sh = config["account_id_sh"].get<std::string>();
        strncpy(account.account_id_sh, account_id_sh.c_str(),
                std::min(account_id_sh.length(), sizeof(account.account_id_sh) - 1));
    }

    // 解析深圳证券账户ID
    if (config.contains("account_id_sz"))
    {
        std::string account_id_sz = config["account_id_sz"].get<std::string>();
        strncpy(account.account_id_sz, account_id_sz.c_str(),
                std::min(account_id_sz.length(), sizeof(account.account_id_sz) - 1));
    }

    if (config.contains("branch_id"))
    {
        std::string branch_id = config["branch_id"].get<std::string>();
        strncpy(account.branch_id, branch_id.c_str(),
                std::min(branch_id.length(), sizeof(account.branch_id) - 1));
    }

    if (config.contains("password"))
    {
        std::string password = config["password"].get<std::string>();
        strncpy(account.password, password.c_str(),
                std::min(password.length(), sizeof(account.password) - 1));
    }
}

bool TradeHandlerAdapter::_validate_account_config(const AccountInfo &account)
{
    // 登陆账号是必须的
    if (strlen(account.login_id) == 0)
    {
        TRADE_LOG_ERROR("Account login_id is required");
        return false;
    }

    // 资金账户ID是必须的（用于生成资金数据键）
    if (strlen(account.fund_account_id) == 0)
    {
        TRADE_LOG_ERROR("Account fund_account_id is required");
        return false;
    }

    // 至少需要一个证券账户ID
    if (strlen(account.account_id_sh) == 0 && strlen(account.account_id_sz) == 0)
    {
        TRADE_LOG_ERROR("Account must have at least one security account (SH or SZ)");
        return false;
    }

    return true;
}

bool TradeHandlerAdapter::_parse_account_list_config(const nlohmann::json &config, AccountList &account_list)
{
    // 初始化账户列表
    account_list.accounts = nullptr;
    account_list.account_count = 0;

    if (!config.is_array())
    {
        TRADE_LOG_ERROR("Account list config is not an array");
        return false;
    }

    int account_count = static_cast<int>(config.size());
    if (account_count == 0)
    {
        TRADE_LOG_WARN("No accounts configured");
        return true;
    }

    // 动态分配账户数组
    account_list.accounts = (AccountInfo *)calloc(account_count, sizeof(AccountInfo));
    if (!account_list.accounts)
    {
        TRADE_LOG_ERROR("Failed to allocate memory for accounts");
        return false;
    }
    account_list.account_count = account_count;

    // 解析每个账户配置
    int valid_account_count = 0;
    for (int i = 0; i < account_count; ++i)
    {
        try
        {
            AccountInfo account;
            _parse_account_config(config[i], account);

            // 验证账户配置
            if (_validate_account_config(account))
            {
                account_list.accounts[valid_account_count] = account;
                valid_account_count++;
                TRADE_LOG_INFO("Parsed account {}: login_id={}, cust_id={}, fund_account_id={}, account_id_sh={}, account_id_sz={}",
                               i + 1, account.login_id, account.cust_id, account.fund_account_id, account.account_id_sh, account.account_id_sz);
            }
            else
            {
                TRADE_LOG_WARN("Invalid account config at index {}, skipping", i);
            }
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception parsing account config at index {}: {}", i, e.what());
        }
    }

    // 更新实际有效的账户数量
    account_list.account_count = valid_account_count;

    if (valid_account_count == 0)
    {
        TRADE_LOG_ERROR("No valid accounts found in configuration");
        free(account_list.accounts);
        account_list.accounts = nullptr;
        account_list.account_count = 0;
        return false;
    }

    TRADE_LOG_INFO("Successfully parsed {} valid accounts", valid_account_count);
    return true;
}

void TradeHandlerAdapter::_free_trade_config_memory(TradeConfig &config)
{
    // 释放账户列表内存
    if (config.account_list.accounts)
    {
        free(config.account_list.accounts);
        config.account_list.accounts = nullptr;
        config.account_list.account_count = 0;
    }

    // 如果有其他动态分配的内存，也在这里释放
}

bool TradeHandlerAdapter::_validate_performance_config(const PerformanceConfig &config)
{
    if (config.report_interval_ms < 100 || config.report_interval_ms > 60000)
    {
        return false;
    }
    return true;
}

void TradeHandlerAdapter::_log_configuration_summary()
{
    TRADE_LOG_INFO("=== Trade Handler Configuration Summary ===");
    TRADE_LOG_INFO("Handler ID: {}", _id);

    if (_config.contains("config") && _config["config"].contains("extended_config_file"))
    {
        TRADE_LOG_INFO("Extended config file: {}", _config["config"]["extended_config_file"].get<std::string>());
    }

    // 记录回调模式配置
    bool callback_mode = true;
    if (_config.contains("config") && _config["config"].contains("callback_mode"))
    {
        callback_mode = _config["config"]["callback_mode"].get<bool>();
    }
    TRADE_LOG_INFO("Callback mode: {}", callback_mode ? "enabled" : "disabled");

    // 记录账户配置信息
    if (_config.contains("config") && _config["config"].contains("accounts"))
    {
        const auto &accounts = _config["config"]["accounts"];
        if (accounts.is_array())
        {
            TRADE_LOG_INFO("Configured accounts: {}", accounts.size());

            // 统计客户数和资金账户数
            std::set<std::string> unique_customers;
            std::set<std::string> unique_fund_accounts;

            for (size_t i = 0; i < accounts.size() && i < 5; ++i)
            {
                const auto &account = accounts[i];
                std::string account_summary;
                if (account.contains("login_id") && !account["login_id"].get<std::string>().empty())
                {
                    std::string login_id = account["login_id"].get<std::string>();
                    unique_customers.insert(login_id);

                    account_summary = "login_id=" + login_id;

                    if (account.contains("fund_account_id") && !account["fund_account_id"].get<std::string>().empty())
                    {
                        std::string fund_id = account["fund_account_id"].get<std::string>();
                        unique_fund_accounts.insert(login_id + "_" + fund_id);
                        account_summary += ", fund=" + fund_id;
                    }

                    // 显示证券账户信息
                    if (account.contains("account_id_sh") && !account["account_id_sh"].get<std::string>().empty())
                    {
                        account_summary += ", SH=" + account["account_id_sh"].get<std::string>();
                    }
                    if (account.contains("account_id_sz") && !account["account_id_sz"].get<std::string>().empty())
                    {
                        account_summary += ", SZ=" + account["account_id_sz"].get<std::string>();
                    }
                }
                else
                {
                    account_summary = "Unknown";
                }
                TRADE_LOG_INFO("  Account {}: {}", i + 1, account_summary);
            }

            if (accounts.size() > 5)
            {
                TRADE_LOG_INFO("  ... and {} more accounts", accounts.size() - 5);
            }

            TRADE_LOG_INFO("Unique customers: {}, Unique fund accounts: {}",
                           unique_customers.size(), unique_fund_accounts.size());
        }
    }
    else
    {
        TRADE_LOG_WARN("No accounts configured");
    }

    // 记录数据输出配置
    if (_config.contains("data") && _config["data"].is_array())
    {
        bool has_active_data_output = false;
        for (const auto &data_item : _config["data"])
        {
            if (data_item.contains("active") && data_item["active"].get<bool>())
            {
                if (data_item.contains("type"))
                {
                    TRADE_LOG_INFO("Data output: {} (active)", data_item["type"].get<std::string>());
                    has_active_data_output = true;
                }
            }
        }
        if (!has_active_data_output)
        {
            TRADE_LOG_INFO("Data output: none (memory cache only)");
        }
    }
    else
    {
        TRADE_LOG_INFO("Data output: none (memory cache only)");
    }

    TRADE_LOG_INFO("============================================");
}

NS_FT_END