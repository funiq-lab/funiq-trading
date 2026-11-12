#include "strategy/strategy_service.h"
#include "strategy/strategy_adapter.h"
#include "quote/quote_parser.h"
#include "trade/trade_handler.h"
#include "data/data_manager.h"
#include "logger/logger.hpp"
#include "utils/config.h"
#include "laserpants/dotenv/dotenv.h"
#include <fstream>
#include <thread>
#include <chrono>

NS_FT_BEGIN

StrategyService::StrategyService()
    : _running(false), _initialized(false),
      _strategy_adapter(nullptr), _quote_parser(nullptr), _trade_handler(nullptr),
      _db_manager(nullptr), _trading_time_manager(nullptr),
      _quote_callback_mode(false), _quote_polling_mode(false),
      _trade_callback_mode(false), _trade_polling_mode(false),
      _quote_polling_thread(nullptr), _trade_polling_thread(nullptr),
      _quote_index_handle{}, _quote_depth_handle{}, _quote_depth_fifty_handle{},
      _quote_entrust_handle{}, _quote_trade_handle{},
      _trade_order_rtn_handle{}, _trade_order_query_handle{}, _trade_fill_handle{}, _trade_position_handle{},
      _trade_error_handle{}, _trade_order_rsp_handle{}, _trade_cancel_rsp_handle{},
      _trade_account_handle{}
{
    dotenv::init();
}

StrategyService::~StrategyService() noexcept
{
    try
    {
        release();
        STRATEGY_LOG_INFO("StrategyService destroyed");
    }
    catch (const std::exception &e)
    {
        STRATEGY_LOG_ERROR("Exception in destructor: {}", e.what());
    }
}

int StrategyService::init()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_initialized.load())
    {
        STRATEGY_LOG_INFO("StrategyService already initialized");
        return 0;
    }

    try
    {
        // 从环境变量获取配置文件路径
        std::string env_config_file = dotenv::getenv("STRATEGY_CONFIG_FILE", "");
        if (env_config_file.empty())
        {
            _config_file = "./strategy_config.json";
            STRATEGY_LOG_INFO("Using default strategy config file: {}", _config_file);
        }
        else
        {
            _config_file = env_config_file;
            STRATEGY_LOG_INFO("Using strategy config file from environment: {}", _config_file);
        }

        STRATEGY_LOG_INFO("Initializing StrategyService with config: {}", _config_file);

        // 加载配置文件
        nlohmann::json config;
        if (!_load_config(_config_file.c_str(), config))
        {
            _set_error("Failed to load config file: " + _config_file);
            return -1;
        }

        // 创建并初始化策略适配器（支持多策略并发）
        auto thread_count = config.value("thread_count", std::thread::hardware_concurrency());
        auto cores = config.value("cores", std::vector<int>());
        
        _strategy_adapter = std::make_unique<StrategyAdapter>(thread_count, cores);
        if (!_strategy_adapter)
        {
            _set_error("Failed to create strategy adapter");
            return -1;
        }

        // 初始化适配器
        if (_strategy_adapter->init() != 0)
        {
            _set_error("Failed to initialize strategy adapter");
            return -1;
        }

        // 从配置文件加载策略
        if (_strategy_adapter->load_strategies_from_config(config) != 0)
        {
            _set_error("Failed to load strategies from config");
            return -1;
        }

        _initialized.store(true);
        STRATEGY_LOG_INFO("StrategyService initialized successfully with multi-strategy support");
        return 0;
    }
    catch (const std::exception &e)
    {
        _set_error("Exception during initialization: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("StrategyService init failed: {}", e.what());
        return -1;
    }
}

int StrategyService::start_service()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (!_initialized.load())
    {
        _set_error("Service not initialized");
        return -1;
    }

    if (_running.load())
    {
        STRATEGY_LOG_INFO("StrategyService already running");
        return 0;
    }

    try
    {
        // 启动策略适配器
        if (_strategy_adapter && _strategy_adapter->start() != 0)
        {
            _set_error("Failed to start strategy adapter");
            return -1;
        }

        _running.store(true);

        // 启动数据处理模式
        _setup_data_processing();

        STRATEGY_LOG_INFO("StrategyService started successfully with ready data sources");
        return 0;
    }
    catch (const std::exception &e)
    {
        _set_error("Exception during service start: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("StrategyService start failed: {}", e.what());
        _running.store(false);
        return -1;
    }
}

int StrategyService::stop_service()
{
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);

        if (!_running.load())
        {
            STRATEGY_LOG_INFO("StrategyService already stopped");
            return 0;
        }

        _running.store(false);

        // 停止轮询线程
        _stop_quote_polling_thread();
        _stop_trade_polling_thread();

        // 清除回调模式标志，但不清理依赖服务引用
        _quote_callback_mode.store(false);
        _trade_callback_mode.store(false);
        _quote_polling_mode.store(false);
        _trade_polling_mode.store(false);

        // 只停止策略，不销毁
        if (_strategy_adapter)
        {
            _strategy_adapter->stop(); // 不调用release
        }
    }

    // 在锁外面清理回调
    _clear_all_callbacks();

    STRATEGY_LOG_INFO("StrategyService stopped successfully (can be restarted)");
    return 0;
}

void StrategyService::release()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    try
    {
        // 停止服务
        if (_running.load())
        {
            _stop_quote_polling_thread();
            _stop_trade_polling_thread();
            _running.store(false);
        }

        // 清理所有回调句柄
        _clear_all_callbacks();

        // 完全清理策略适配器
        if (_strategy_adapter)
        {
            _strategy_adapter->release();
            _strategy_adapter.reset();
        }

        // 重置状态
        _initialized.store(false);
        _quote_callback_mode.store(false);
        _quote_polling_mode.store(false);
        _trade_callback_mode.store(false);
        _trade_polling_mode.store(false);

        // 清理依赖服务引用
        _quote_parser = nullptr;
        _trade_handler = nullptr;
        _db_manager = nullptr;
        _trading_time_manager = nullptr;

        STRATEGY_LOG_INFO("StrategyService released successfully");
    }
    catch (const std::exception &e)
    {
        STRATEGY_LOG_ERROR("Exception during StrategyService release: {}", e.what());
    }
}

// ===== 多策略管理接口 =====

int StrategyService::create_strategy_instance(const char *strategy_id, const char *config)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_initialized.load())
    {
        _set_error("Service not initialized");
        return -1;
    }

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return -1;
    }

    return _strategy_adapter->create_strategy_instance(strategy_id, config);
}

int StrategyService::start_strategy_instance(const char *strategy_id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_running.load())
    {
        _set_error("Service not running");
        return -1;
    }

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return -1;
    }

    return _strategy_adapter->start_strategy_instance(strategy_id);
}

int StrategyService::stop_strategy_instance(const char *strategy_id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return -1;
    }

    return _strategy_adapter->stop_strategy_instance(strategy_id);
}

int StrategyService::destroy_strategy_instance(const char *strategy_id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return -1;
    }

    return _strategy_adapter->destroy_strategy_instance(strategy_id);
}

bool StrategyService::strategy_instance_exists(const char *strategy_id) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        return false;
    }

    return _strategy_adapter->get_strategy_instance(strategy_id) != nullptr;
}

bool StrategyService::strategy_instance_is_active(const char *strategy_id) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        return false;
    }

    return _strategy_adapter->is_strategy_instance_active(strategy_id);
}

int StrategyService::get_strategy_instance_status(const char *strategy_id) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        return -1;
    }

    BaseStrategy *strategy = _strategy_adapter->get_strategy_instance(strategy_id);
    if (!strategy)
    {
        return -1;
    }

    return static_cast<int>(strategy->get_state());
}

// ===== 批量策略操作 =====

int StrategyService::start_all_strategy_instances()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_running.load())
    {
        _set_error("Service not running");
        return 0;
    }

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return 0;
    }

    return _strategy_adapter->start_all_strategies();
}

int StrategyService::stop_all_strategy_instances()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return 0;
    }

    return _strategy_adapter->stop_all_strategies();
}

int StrategyService::destroy_all_strategy_instances()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_strategy_adapter)
    {
        _set_error("No strategy adapter available");
        return 0;
    }

    return _strategy_adapter->destroy_all_strategies();
}

// ===== 状态查询接口 =====

std::vector<std::string> StrategyService::get_registered_strategy_ids() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_strategy_adapter)
    {
        return _strategy_adapter->get_registered_strategy_ids();
    }

    return std::vector<std::string>();
}

std::vector<std::string> StrategyService::get_active_strategy_ids() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_strategy_adapter)
    {
        return _strategy_adapter->get_active_strategy_ids();
    }

    return std::vector<std::string>();
}

size_t StrategyService::get_active_strategy_count() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_strategy_adapter)
    {
        return _strategy_adapter->get_active_strategy_count();
    }

    return 0;
}

std::string StrategyService::get_thread_pool_status() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_strategy_adapter)
    {
        return _strategy_adapter->get_thread_pool_status();
    }

    return "{}";
}

// ===== 依赖服务管理接口 =====

StrategyAdapter *StrategyService::get_strategy_adapter() const
{
    return _strategy_adapter.get();
}

void StrategyService::set_quote_parser(IQuoteParser *quote_parser)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    // 清理旧的回调
    _clear_quote_callbacks();

    _quote_parser = quote_parser;
    if (_strategy_adapter)
    {
        _strategy_adapter->set_quote_parser(quote_parser);
    }

    // 如果服务正在运行，重新设置数据处理
    if (_running.load())
    {
        _setup_data_processing();
    }

    STRATEGY_LOG_INFO("Quote parser set successfully for multi-strategy service");
}

void StrategyService::set_trade_handler(ITradeHandler *trade_handler)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    // 清理旧的回调
    _clear_trade_callbacks();

    _trade_handler = trade_handler;
    if (_strategy_adapter)
    {
        _strategy_adapter->set_trade_handler(trade_handler);
    }

    // 如果服务正在运行，重新设置数据处理
    if (_running.load())
    {
        _setup_data_processing();
    }

    STRATEGY_LOG_INFO("Trade handler set successfully for multi-strategy service");
}

void StrategyService::set_db_manager(DatabaseManager *db_manager)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _db_manager = db_manager;

    if (_strategy_adapter)
    {
        _strategy_adapter->set_db_manager(db_manager);
    }

    STRATEGY_LOG_INFO("Database manager set for strategy service");
}

DatabaseManager *StrategyService::get_db_manager() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _db_manager;
}

IQuoteParser *StrategyService::get_quote_parser() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _quote_parser;
}

ITradeHandler *StrategyService::get_trade_handler() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _trade_handler;
}

bool StrategyService::is_running() const
{
    return _running.load();
}

bool StrategyService::is_initialized() const
{
    return _initialized.load();
}

const char *StrategyService::get_last_error() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _last_error.c_str();
}

DataMode StrategyService::get_data_mode() const
{
    if (_quote_callback_mode.load() || _trade_callback_mode.load())
    {
        return DataMode::CALLBACK;
    }
    return DataMode::POLLING;
}

void StrategyService::set_trading_time_manager(TradingTimeManager* trading_time_manager)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _trading_time_manager = trading_time_manager;

    if (_strategy_adapter)
    {
        _strategy_adapter->set_trading_time_manager(trading_time_manager);
    }

    STRATEGY_LOG_INFO("Trading time manager set successfully for strategy service");
}

TradingTimeManager* StrategyService::get_trading_time_manager() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _trading_time_manager;
}

// ===== 私有方法实现 =====

void StrategyService::_setup_data_processing()
{
    if (_quote_parser)
    {
        // 优先使用回调模式，如果失败则使用轮询模式
        if (_setup_quote_callback_mode() != 0)
        {
            STRATEGY_LOG_WARN("Failed to setup quote callback mode, falling back to polling mode");
            _setup_quote_polling_mode();
        }
    }
    else
    {
        STRATEGY_LOG_WARN("No quote parser available for data processing");
    }

    if (_trade_handler)
    {
        // 优先使用回调模式，如果失败则使用轮询模式
        if (_setup_trade_callback_mode() != 0)
        {
            STRATEGY_LOG_WARN("Failed to setup trade callback mode, falling back to polling mode");
            _setup_trade_polling_mode();
        }
    }
    else
    {
        STRATEGY_LOG_WARN("No trade handler available for data processing");
    }

    // 输出当前数据模式状态
    STRATEGY_LOG_INFO("Multi-strategy data processing modes - Quote: {}, Trade: {}",
                      _quote_callback_mode.load() ? "CALLBACK" : (_quote_polling_mode.load() ? "POLLING" : "NONE"),
                      _trade_callback_mode.load() ? "CALLBACK" : (_trade_polling_mode.load() ? "POLLING" : "NONE"));
}

bool StrategyService::_load_config(const char *config_file, nlohmann::json &config)
{
    try
    {
        std::ifstream file(config_file);
        if (!file.is_open())
        {
            STRATEGY_LOG_ERROR("Cannot open config file: {}", config_file);
            return false;
        }

        file >> config;
        file.close();

        STRATEGY_LOG_INFO("Config loaded successfully from: {}", config_file);
        return true;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        _last_error = "JSON parse error: " + std::string(e.what());
        STRATEGY_LOG_ERROR(_last_error);
        return false;
    }
    catch (const std::exception &e)
    {
        _last_error = "Exception loading config: " + std::string(e.what());
        STRATEGY_LOG_ERROR(_last_error);
        return false;
    }
}

int StrategyService::_setup_quote_callback_mode()
{
    if (!_quote_parser)
    {
        return -1;
    }

    try
    {
        // 先清理旧的回调句柄
        _clear_quote_callbacks();

        // 注册回调函数并保存句柄
        _quote_index_handle = _quote_parser->set_index_data_callback(_on_index_data_callback);
        _quote_depth_handle = _quote_parser->set_depth_data_callback(_on_depth_data_callback);
        _quote_depth_fifty_handle = _quote_parser->set_depth_fifty_data_callback(_on_depth_fifty_data_callback);
        _quote_entrust_handle = _quote_parser->set_entrust_data_callback(_on_entrust_data_callback);
        _quote_trade_handle = _quote_parser->set_trade_data_callback(_on_trade_data_callback);

        // 检查句柄是否有效
        if (!_quote_index_handle || !_quote_depth_handle || !_quote_depth_fifty_handle ||
            !_quote_entrust_handle || !_quote_trade_handle)
        {
            STRATEGY_LOG_WARN("Some quote callbacks failed to register");
            _clear_quote_callbacks();
            return -1;
        }

        _quote_callback_mode.store(true);
        _quote_polling_mode.store(false);

        STRATEGY_LOG_INFO("Quote callback mode setup successfully for multi-strategy");
        return 0;
    }
    catch (const std::exception &e)
    {
        STRATEGY_LOG_ERROR("Failed to setup quote callback mode: {}", e.what());
        _clear_quote_callbacks();
        return -1;
    }
}

int StrategyService::_setup_quote_polling_mode()
{
    _quote_callback_mode.store(false);
    _quote_polling_mode.store(true);

    _start_quote_polling_thread();

    STRATEGY_LOG_INFO("Quote polling mode setup successfully for multi-strategy");
    return 0;
}

void StrategyService::_start_quote_polling_thread()
{
    if (_quote_polling_thread)
    {
        return;
    }

    _quote_polling_thread = std::make_unique<std::thread>([this]()
                                                          { _quote_polling_loop(); });

    STRATEGY_LOG_INFO("Quote polling thread started for multi-strategy");
}

void StrategyService::_stop_quote_polling_thread()
{
    if (_quote_polling_thread)
    {
        _quote_polling_mode.store(false);

        if (_quote_polling_thread->joinable())
        {
            _quote_polling_thread->join();
        }

        _quote_polling_thread.reset();
        STRATEGY_LOG_INFO("Quote polling thread stopped");
    }
}

void StrategyService::_quote_polling_loop()
{
    STRATEGY_LOG_INFO("Multi-strategy quote polling loop started");

    while (_quote_polling_mode.load() && _running.load())
    {
        try
        {
            if (_quote_parser && _strategy_adapter)
            {
                // 轮询各种数据类型并分发到所有活跃策略
                IndexData index_data;
                if (_quote_parser->get_index_data(&index_data) == 1)
                {
                    _strategy_adapter->dispatch_index_data(&index_data);
                }

                DepthData depth_data;
                if (_quote_parser->get_depth_data(&depth_data) == 1)
                {
                    _strategy_adapter->dispatch_depth_data(&depth_data);
                }

                DepthFiftyData depth_fifty_data;
                if (_quote_parser->get_depth_fifty_data(&depth_fifty_data) == 1)
                {
                    _strategy_adapter->dispatch_depth_fifty_data(&depth_fifty_data);
                }

                EntrustData entrust_data;
                if (_quote_parser->get_entrust_data(&entrust_data) == 1)
                {
                    _strategy_adapter->dispatch_entrust_data(&entrust_data);
                }

                TradeData trade_data;
                if (_quote_parser->get_trade_data(&trade_data) == 1)
                {
                    _strategy_adapter->dispatch_trade_data(&trade_data);
                }
            }
        }
        catch (const std::exception &e)
        {
            STRATEGY_LOG_ERROR("Exception in quote polling loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    STRATEGY_LOG_INFO("Multi-strategy quote polling loop finished");
}

int StrategyService::_setup_trade_callback_mode()
{
    if (!_trade_handler)
    {
        return -1;
    }

    try
    {
        // 先清理旧的回调句柄
        _clear_trade_callbacks();

        // 注册回调函数并保存句柄
        _trade_order_rtn_handle = _trade_handler->set_order_rtn_callback(_on_order_rtn_callback);
        _trade_order_query_handle = _trade_handler->set_order_query_callback(_on_order_query_callback);
        _trade_fill_handle = _trade_handler->set_fill_callback(_on_fill_data_callback);
        _trade_position_handle = _trade_handler->set_position_callback(_on_position_data_callback);
        _trade_account_handle = _trade_handler->set_account_callback(_on_account_data_callback);
        _trade_error_handle = _trade_handler->set_error_callback(_on_trade_error_callback);
        _trade_order_rsp_handle = _trade_handler->set_order_rsp_callback(_on_order_rsp_callback);
        _trade_cancel_rsp_handle = _trade_handler->set_cancel_rsp_callback(_on_cancel_rsp_callback);

        // 检查句柄是否有效
        if (!_trade_order_rtn_handle || !_trade_order_query_handle || !_trade_fill_handle ||
            !_trade_position_handle || !_trade_account_handle || !_trade_error_handle ||
            !_trade_order_rsp_handle || !_trade_cancel_rsp_handle)
        {
            STRATEGY_LOG_WARN("Some trade callbacks failed to register");
            _clear_trade_callbacks();
            return -1;
        }

        _trade_callback_mode.store(true);
        _trade_polling_mode.store(false);

        STRATEGY_LOG_INFO("Trade callback mode setup successfully for multi-strategy with enhanced callbacks");
        return 0;
    }
    catch (const std::exception &e)
    {
        STRATEGY_LOG_ERROR("Failed to setup trade callback mode: {}", e.what());
        _clear_trade_callbacks();
        return -1;
    }
}

int StrategyService::_setup_trade_polling_mode()
{
    _trade_callback_mode.store(false);
    _trade_polling_mode.store(true);

    _start_trade_polling_thread();

    STRATEGY_LOG_INFO("Trade polling mode setup successfully for multi-strategy");
    return 0;
}

void StrategyService::_start_trade_polling_thread()
{
    if (_trade_polling_thread)
    {
        return;
    }

    _trade_polling_thread = std::make_unique<std::thread>([this]()
                                                          { _trade_polling_loop(); });

    STRATEGY_LOG_INFO("Trade polling thread started for multi-strategy");
}

void StrategyService::_stop_trade_polling_thread()
{
    if (_trade_polling_thread)
    {
        _trade_polling_mode.store(false);

        if (_trade_polling_thread->joinable())
        {
            _trade_polling_thread->join();
        }

        _trade_polling_thread.reset();
        STRATEGY_LOG_INFO("Trade polling thread stopped");
    }
}

void StrategyService::_trade_polling_loop()
{
    STRATEGY_LOG_INFO("Multi-strategy trade polling loop started with enhanced data types");

    while (_trade_polling_mode.load() && _running.load())
    {
        try
        {
            if (_trade_handler && _strategy_adapter)
            {
                // 轮询各种交易数据并分发到所有活跃策略

                // 订单主推数据
                TradeOrderData order_rtn_data;
                if (_trade_handler->get_order_rtn_data(&order_rtn_data) == 1)
                {
                    _strategy_adapter->dispatch_order_rtn_data(&order_rtn_data);
                }

                // 订单查询数据
                TradeOrderData order_query_data;
                if (_trade_handler->get_order_query_data(&order_query_data) == 1)
                {
                    _strategy_adapter->dispatch_order_query_data(&order_query_data);
                }

                // 现有的数据类型保持不变
                TradeFillData fill_data;
                if (_trade_handler->get_fill_data(&fill_data) == 1)
                {
                    _strategy_adapter->dispatch_fill_data(&fill_data);
                }

                PositionData position_data;
                if (_trade_handler->get_position_data(&position_data) == 1)
                {
                    _strategy_adapter->dispatch_position_data(&position_data);
                }

                // 现有的响应数据保持不变
                TradeOrderRspData order_rsp_data;
                if (_trade_handler->get_order_rsp_data(&order_rsp_data) == 1)
                {
                    _strategy_adapter->dispatch_order_rsp_data(&order_rsp_data);
                }

                TradeOrderCancelRsqData cancel_rsp_data;
                if (_trade_handler->get_cancel_rsp_data(&cancel_rsp_data) == 1)
                {
                    _strategy_adapter->dispatch_cancel_rsp_data(&cancel_rsp_data);
                }

                // 账户数据轮询保持现有逻辑（已经正确实现）
                std::vector<std::string> account_ids = _trade_handler->get_all_login_ids();

                for (const auto &login_id : account_ids)
                {
                    AccountInfo account_info;
                    if (_trade_handler->get_account_info_by_id(login_id.c_str(), &account_info) == 0)
                    {
                        if (strlen(account_info.account_id_sh) > 0)
                        {
                            AccountData sh_account_data;
                            if (_trade_handler->get_account_data_by_id(
                                    login_id.c_str(),
                                    account_info.fund_account_id,
                                    account_info.account_id_sh,
                                    &sh_account_data) == 1)
                            {
                                _strategy_adapter->dispatch_account_data(&sh_account_data);
                            }
                        }

                        if (strlen(account_info.account_id_sz) > 0)
                        {
                            AccountData sz_account_data;
                            if (_trade_handler->get_account_data_by_id(
                                    login_id.c_str(),
                                    account_info.fund_account_id,
                                    account_info.account_id_sz,
                                    &sz_account_data) == 1)
                            {
                                _strategy_adapter->dispatch_account_data(&sz_account_data);
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            STRATEGY_LOG_ERROR("Exception in trade polling loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    STRATEGY_LOG_INFO("Multi-strategy trade polling loop finished");
}

// 清理回调句柄的方法保持不变，但更新为多策略分发

void StrategyService::_clear_quote_callbacks()
{
    if (_quote_parser)
    {
        if (_quote_index_handle)
        {
            _quote_parser->remove_index_data_callback(_quote_index_handle);
            _quote_index_handle = IndexDataCallbackHandle{};
        }
        if (_quote_depth_handle)
        {
            _quote_parser->remove_depth_data_callback(_quote_depth_handle);
            _quote_depth_handle = DepthDataCallbackHandle{};
        }
        if (_quote_depth_fifty_handle)
        {
            _quote_parser->remove_depth_fifty_data_callback(_quote_depth_fifty_handle);
            _quote_depth_fifty_handle = DepthFiftyDataCallbackHandle{};
        }
        if (_quote_entrust_handle)
        {
            _quote_parser->remove_entrust_data_callback(_quote_entrust_handle);
            _quote_entrust_handle = EntrustDataCallbackHandle{};
        }
        if (_quote_trade_handle)
        {
            _quote_parser->remove_trade_data_callback(_quote_trade_handle);
            _quote_trade_handle = TradeDataCallbackHandle{};
        }
    }
}

void StrategyService::_clear_trade_callbacks()
{
    if (_trade_handler)
    {
        if (_trade_order_rtn_handle)
        {
            _trade_handler->remove_order_rtn_callback(_trade_order_rtn_handle);
            _trade_order_rtn_handle = TradeOrderCallbackHandle{};
        }
        if (_trade_order_query_handle)
        {
            _trade_handler->remove_order_query_callback(_trade_order_query_handle);
            _trade_order_query_handle = TradeOrderQueryCallbackHandle{};
        }
        if (_trade_fill_handle)
        {
            _trade_handler->remove_fill_callback(_trade_fill_handle);
            _trade_fill_handle = TradeFillCallbackHandle{};
        }
        if (_trade_position_handle)
        {
            _trade_handler->remove_position_callback(_trade_position_handle);
            _trade_position_handle = PositionCallbackHandle{};
        }
        if (_trade_account_handle)
        {
            _trade_handler->remove_account_callback(_trade_account_handle);
            _trade_account_handle = AccountCallbackHandle{};
        }
        if (_trade_error_handle)
        {
            _trade_handler->remove_error_callback(_trade_error_handle);
            _trade_error_handle = TradeErrorCallbackHandle{};
        }
        if (_trade_order_rsp_handle)
        {
            _trade_handler->remove_order_rsp_callback(_trade_order_rsp_handle);
            _trade_order_rsp_handle = TradeOrderRspCallbackHandle{};
        }
        if (_trade_cancel_rsp_handle)
        {
            _trade_handler->remove_cancel_rsp_callback(_trade_cancel_rsp_handle);
            _trade_cancel_rsp_handle = TradeOrderCancelRspCallbackHandle{};
        }
    }
}

void StrategyService::_clear_all_callbacks()
{
    _clear_quote_callbacks();
    _clear_trade_callbacks();
    STRATEGY_LOG_INFO("All callbacks cleared");
}

// ===== 全局服务实例 =====
static std::unique_ptr<StrategyService> g_strategy_service;
static std::shared_mutex g_strategy_service_mutex;

// ===== 回调函数更新为多策略分发 =====

void StrategyService::_on_index_data_callback(const IndexData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_index_data(data);
    }
}

void StrategyService::_on_depth_data_callback(const DepthData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_depth_data(data);
    }
}

void StrategyService::_on_depth_fifty_data_callback(const DepthFiftyData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_depth_fifty_data(data);
    }
}

void StrategyService::_on_entrust_data_callback(const EntrustData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_entrust_data(data);
    }
}

void StrategyService::_on_trade_data_callback(const TradeData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_trade_data(data);
    }
}

void StrategyService::_on_order_rtn_callback(const TradeOrderData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_order_rtn_data(data);
    }
}

void StrategyService::_on_order_query_callback(const TradeOrderData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_order_query_data(data);
    }
}

void StrategyService::_on_fill_data_callback(const TradeFillData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_fill_data(data);
    }
}

void StrategyService::_on_position_data_callback(const PositionData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_position_data(data);
    }
}

void StrategyService::_on_account_data_callback(const AccountData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_account_data(data);
    }
}

void StrategyService::_on_trade_error_callback(int32_t error_code, const char *error_msg, int64_t request_id)
{
    if (!error_msg || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_trade_error(error_code, error_msg);
    }
}

void StrategyService::_on_order_rsp_callback(const TradeOrderRspData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_order_rsp_data(data);
    }
}

void StrategyService::_on_cancel_rsp_callback(const TradeOrderCancelRsqData *data)
{
    if (!data || !g_strategy_service || !g_strategy_service->is_running())
    {
        return;
    }

    auto *adapter = g_strategy_service->get_strategy_adapter();
    if (adapter)
    {
        adapter->dispatch_cancel_rsp_data(data);
    }
}

void StrategyService::_set_error(const std::string &error)
{
    _last_error = error;
    STRATEGY_LOG_ERROR("StrategyService error: {}", error);
}

NS_FT_END

// ===== C接口实现（更新为多策略模式） =====
extern "C"
{
    // ===== 服务生命周期管理接口 =====
    int strategy_init(void)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            ft::g_strategy_service = std::make_unique<ft::StrategyService>();
        }

        return ft::g_strategy_service->init();
    }

    int strategy_init_with_config(const char *config_file)
    {
        if (!config_file)
        {
            return -1;
        }

        setenv("STRATEGY_CONFIG_FILE", config_file, 1);
        return strategy_init();
    }

    int strategy_start_service(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->start_service();
    }

    int strategy_stop_service(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->stop_service();
    }

    void strategy_release(void)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (ft::g_strategy_service)
        {
            ft::g_strategy_service->release();
            ft::g_strategy_service.reset();
        }
    }

    void *strategy_get_service_instance(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);
        return ft::g_strategy_service ? ft::g_strategy_service.get() : nullptr;
    }

    // ===== 策略实例管理接口 =====

    int strategy_create_instance(const char *strategy_id, const char *config)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->create_strategy_instance(strategy_id, config);
    }

    int strategy_start_instance(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->start_strategy_instance(strategy_id);
    }

    int strategy_stop_instance(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->stop_strategy_instance(strategy_id);
    }

    int strategy_destroy_instance(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->destroy_strategy_instance(strategy_id);
    }

    int strategy_instance_exists(const char *strategy_id)
    {
        if (!strategy_id)
            return 0;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->strategy_instance_exists(strategy_id) ? 1 : 0;
    }

    int strategy_instance_is_active(const char *strategy_id)
    {
        if (!strategy_id)
            return 0;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->strategy_instance_is_active(strategy_id) ? 1 : 0;
    }

    int strategy_get_instance_status(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        return ft::g_strategy_service->get_strategy_instance_status(strategy_id);
    }

    // ===== 批量策略操作接口 =====

    int strategy_start_all_instances(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->start_all_strategy_instances();
    }

    int strategy_stop_all_instances(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->stop_all_strategy_instances();
    }

    int strategy_destroy_all_instances(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->destroy_all_strategy_instances();
    }

    // ===== 状态查询接口 =====

    int strategy_is_service_running(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->is_running() ? 1 : 0;
    }

    int strategy_is_service_initialized(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->is_initialized() ? 1 : 0;
    }

    size_t strategy_get_active_instance_count(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        return ft::g_strategy_service->get_active_strategy_count();
    }

    const char *strategy_get_last_error(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "Service not initialized";
        }

        return ft::g_strategy_service->get_last_error();
    }

    const char *strategy_get_registered_strategy_ids(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "[]";
        }

        auto ids = ft::g_strategy_service->get_registered_strategy_ids();
        static std::string json_result;

        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &id : ids)
        {
            json_array.push_back(id);
        }

        json_result = json_array.dump();
        return json_result.c_str();
    }

    const char *strategy_get_active_strategy_ids(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "[]";
        }

        auto ids = ft::g_strategy_service->get_active_strategy_ids();
        static std::string json_result;

        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &id : ids)
        {
            json_array.push_back(id);
        }

        json_result = json_array.dump();
        return json_result.c_str();
    }

    const char *strategy_get_thread_pool_status(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "{}";
        }

        static std::string status_result = ft::g_strategy_service->get_thread_pool_status();
        return status_result.c_str();
    }

    // ===== 策略配置管理接口 =====

    int strategy_update_instance_config(const char *strategy_id, const char *config)
    {
        if (!strategy_id || !config)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->update_config(config);
    }

    const char *strategy_get_instance_config(const char *strategy_id)
    {
        if (!strategy_id)
            return "{}";

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "{}";
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return "{}";
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return "{}";
        }

        return strategy->get_config();
    }

    // ===== 挂单队列管理接口 =====

    int strategy_add_order_queue(const char *strategy_id, const char *order)
    {
        if (!strategy_id || !order)
        {
            STRATEGY_LOG_ERROR("strategy_id or order is null");
            return -1;
        }

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            STRATEGY_LOG_ERROR("StrategyService is not initialized");
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {

            STRATEGY_LOG_ERROR("StrategyService get_strategy_adapter failed");
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            STRATEGY_LOG_ERROR("StrategyService get_strategy_instance failed: strategy_id={}", strategy_id);
            return -1;
        }

        return strategy->add_order_queue(order);
    }

    int strategy_update_order_queue(const char *strategy_id, int64_t order_id, const char *order)
    {
        if (!strategy_id || !order)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->update_order_queue(order_id, order);
    }

    int strategy_cancel_order_queue(const char *strategy_id, int64_t order_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->cancel_order_queue(order_id);
    }

    int strategy_remove_order_queue(const char *strategy_id, int64_t order_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->remove_order_queue(order_id);
    }

    int strategy_batch_add_order_queue(const char *strategy_id, const char *orders)
    {
        if (!strategy_id || !orders)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->batch_add_order_queue(orders);
    }

    int strategy_cancel_all_active_order_queue(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->cancel_all_active_order_queue();
    }

    int strategy_remove_all_active_order_queue(const char *strategy_id)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->remove_all_active_order_queue();
    }

    const char *strategy_get_order_queue(const char *strategy_id)
    {
        if (!strategy_id)
            return "[]";

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "[]";
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return "[]";
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return "[]";
        }

        return strategy->get_order_queue();
    }

    // ===== 策略数据获取接口 =====

    const char *strategy_get_instance_data(const char *strategy_id, const char *data_type)
    {
        if (!strategy_id || !data_type)
            return nullptr;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return nullptr;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return nullptr;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return nullptr;
        }

        return strategy->get_strategy_data(data_type);
    }

    const char *strategy_get_available_data_types(const char *strategy_id)
    {
        if (!strategy_id)
            return "[]";

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "[]";
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return "[]";
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return "[]";
        }

        return strategy->get_available_data_types();
    }

    StrategyDataCallbackHandle strategy_set_data_change_callback(const char *strategy_id, StrategyDataCallbackType callback)
    {
        if (!strategy_id || !callback)
            return StrategyDataCallbackHandle{};

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return StrategyDataCallbackHandle{};
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return StrategyDataCallbackHandle{};
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return StrategyDataCallbackHandle{};
        }

        return strategy->set_data_change_callback(callback);
    }

    int strategy_remove_data_change_callback(const char *strategy_id, StrategyDataCallbackHandle handle)
    {
        if (!strategy_id)
            return -1;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return -1;
        }

        ft::BaseStrategy *strategy = adapter->get_strategy_instance(strategy_id);
        if (!strategy)
        {
            return -1;
        }

        return strategy->remove_data_change_callback(handle);
    }

    // ===== 策略元信息查询接口 =====

    const char *strategy_get_strategy_name(const char *strategy_id)
    {
        if (!strategy_id)
            return "";

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "";
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return "";
        }

        static std::string name_result = adapter->get_strategy_name(strategy_id);
        return name_result.c_str();
    }

    const char *strategy_get_strategy_description(const char *strategy_id)
    {
        if (!strategy_id)
            return "";

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return "";
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return "";
        }

        static std::string desc_result = adapter->get_strategy_description(strategy_id);
        return desc_result.c_str();
    }

    int strategy_is_strategy_registered(const char *strategy_id)
    {
        if (!strategy_id)
            return 0;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return 0;
        }

        return adapter->is_strategy_registered(strategy_id) ? 1 : 0;
    }

    // ===== 依赖服务管理接口 =====

    int strategy_set_quote_parser(void *quote_parser)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::g_strategy_service->set_quote_parser(static_cast<ft::IQuoteParser *>(quote_parser));
        return 0;
    }

    int strategy_set_trade_handler(void *trade_handler)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::g_strategy_service->set_trade_handler(static_cast<ft::ITradeHandler *>(trade_handler));
        return 0;
    }

    int strategy_set_db_manager(void *db_manager)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);
        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::g_strategy_service->set_db_manager(static_cast<ft::DatabaseManager *>(db_manager));
        return 0;
    }

    void *strategy_get_db_manager(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);
        if (!ft::g_strategy_service)
        {
            return nullptr;
        }

        return ft::g_strategy_service->get_db_manager();
    }

    void *strategy_get_quote_parser(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return nullptr;
        }

        return ft::g_strategy_service->get_quote_parser();
    }

    void *strategy_get_trade_handler(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return nullptr;
        }

        return ft::g_strategy_service->get_trade_handler();
    }

    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_set_trading_time_manager(void *trading_time_manager)
    {
        std::unique_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return -1;
        }

        ft::g_strategy_service->set_trading_time_manager(static_cast<ft::TradingTimeManager *>(trading_time_manager));
        return 0;
    }

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    EXPORT_FLAG void *strategy_get_trading_time_manager(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return nullptr;
        }

        return ft::g_strategy_service->get_trading_time_manager();
    }

    // ===== 数据分发接口 =====

    void strategy_dispatch_index_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_index_data(static_cast<const IndexData *>(data));
        }
    }

    void strategy_dispatch_depth_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_depth_data(static_cast<const DepthData *>(data));
        }
    }

    void strategy_dispatch_depth_fifty_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_depth_fifty_data(static_cast<const DepthFiftyData *>(data));
        }
    }

    void strategy_dispatch_entrust_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_entrust_data(static_cast<const EntrustData *>(data));
        }
    }

    void strategy_dispatch_trade_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_trade_data(static_cast<const TradeData *>(data));
        }
    }

    void strategy_dispatch_fill_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_fill_data(static_cast<const TradeFillData *>(data));
        }
    }

    void strategy_dispatch_position_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_position_data(static_cast<const PositionData *>(data));
        }
    }

    void strategy_dispatch_account_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_account_data(static_cast<const AccountData *>(data));
        }
    }

    void strategy_dispatch_trade_error(int error_code, const char *error_msg)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !error_msg)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_trade_error(error_code, error_msg);
        }
    }

    void strategy_dispatch_order_rsp_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_order_rsp_data(static_cast<const TradeOrderRspData *>(data));
        }
    }

    void strategy_dispatch_cancel_rsp_data(const void *data)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service || !data)
        {
            return;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (adapter)
        {
            adapter->dispatch_cancel_rsp_data(static_cast<const TradeOrderCancelRsqData *>(data));
        }
    }

    // ===== 全局策略模块管理接口 =====

    const char *strategy_get_registered_modules(void)
    {
        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            static std::string empty_result = "[]";
            return empty_result.c_str();
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            static std::string empty_result = "[]";
            return empty_result.c_str();
        }

        // 获取内置策略模块
        auto modules = adapter->get_builtin_strategy_modules();
        static std::string json_result;

        nlohmann::json json_array = nlohmann::json::array();
        for (const auto &module : modules)
        {
            json_array.push_back(module);
        }

        json_result = json_array.dump();
        return json_result.c_str();
    }

    int strategy_is_module_registered(const char *module_name)
    {
        if (!module_name)
            return 0;

        std::shared_lock<std::shared_mutex> lock(ft::g_strategy_service_mutex);

        if (!ft::g_strategy_service)
        {
            return 0;
        }

        ft::StrategyAdapter *adapter = ft::g_strategy_service->get_strategy_adapter();
        if (!adapter)
        {
            return 0;
        }

        // 检查是否是内置策略模块
        auto modules = adapter->get_builtin_strategy_modules();
        return std::find(modules.begin(), modules.end(), module_name) != modules.end() ? 1 : 0;
    }

} // extern "C"