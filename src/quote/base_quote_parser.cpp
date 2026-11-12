#include "quote/base_quote_parser.h"
#include "quote/quote_interface.h"
#include "utils/config.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include "logger/logger.hpp"
#include "data/data_manager.h"
#include "utils/time.h"
#include "utils/mock.h"

NS_FT_BEGIN

BaseQuoteParser::BaseQuoteParser()
    : _running(false), 
    _initialized(false), 
    _connected(false), 
    _released(false),
     _current_status(STATUS_DISCONNECTED), 
     _performance_enabled(false), 
     _latency_stats_enabled(false), 
     _report_interval_ms(5000), 
     _total_messages(0), 
     _dropped_messages(0),
     _messages_per_second(0), 
     _last_stats_time(std::chrono::steady_clock::now()), 
     _total_latency_us(0), 
     _max_latency_us(0), 
     _min_latency_us(UINT64_MAX), 
     _last_performance_report(std::chrono::steady_clock::now()), 
     _mock_mode_enabled(false), 
     _mock_loop_replay(true), 
     _mock_data_loaded(false), 
     _mock_replay_running(false), 
     _mock_current_index(0), 
     _mock_use_synthetic_data(false), 
     _mock_generator(std::make_unique<MockDataGenerator>()), 
     _mock_generator_setup(false), 
     _has_subscription(false), 
     _index_callbacks(), 
     _depth_callbacks(), 
     _depth_fifty_callbacks(), 
     _entrust_callbacks(), 
     _trade_callbacks(), 
     _status_callbacks(), 
     _data_manager(nullptr),
     _callback_mode_enabled(true)
{

    memset(_mock_data_file, 0, sizeof(_mock_data_file));
    memset(&_latest_index_data, 0, sizeof(_latest_index_data));
    memset(&_latest_depth_data, 0, sizeof(_latest_depth_data));
    memset(&_latest_depth_fifty_data, 0, sizeof(_latest_depth_fifty_data));
    memset(&_latest_entrust_data, 0, sizeof(_latest_entrust_data));
    memset(&_latest_trade_data, 0, sizeof(_latest_trade_data));
}

BaseQuoteParser::~BaseQuoteParser()
{
    release();
}

// ===== 基类实现的通用接口 =====
int BaseQuoteParser::init(const QuoteConfig *config)
{
    QUOTE_LOG_INFO("BaseQuoteParser::init");
    std::lock_guard<std::mutex> lock(_mutex);

    if (_initialized.load())
    {
        set_error("Parser already initialized");
        return -1;
    }

    if (!config)
    {
        set_error("Invalid configuration");
        return -1;
    }

    // ===== 初始化订阅配置 =====
    _initialize_subscription_config(&(config->subscription));

    // 设置回调模式
    _callback_mode_enabled.store(config->callback_mode);
    QUOTE_LOG_INFO("Callback mode: {}", config->callback_mode ? "enabled" : "disabled");

    configure_performance_stats(config->performance);
    configure_mock_mode(config->mock);

    // 如果是Mock模式，加载Mock数据
    if (_mock_mode_enabled)
    {
        QUOTE_LOG_INFO("Running in Mock mode, loading mock data");
        if (!_load_mock_data())
        {
            return -1;
        }
    }

    if (config->data_config.output_type != DATA_OUTPUT_NONE)
    {
        if (!_initialize_data_manager(config->data_config))
        {
            QUOTE_LOG_ERROR("Failed to initialize data manager");
            return -1;
        }
        QUOTE_LOG_INFO("Data manager initialized");
    }
    else
    {
        QUOTE_LOG_INFO("No data output configured, using memory cache");
    }

    if (config->callback_mode)
    {
        QUOTE_LOG_INFO("Callback mode enabled, data manager will not be used");
    }

    if (_performance_enabled)
    {
        _start_performance_reporting();
    }

    // 调用子类特定的初始化
    int result = init_impl(config);
    if (result != 0)
    {
        return result;
    }

    _initialized.store(true);
    _current_status.store(STATUS_INITIALIZED);
    return 0;
}

void BaseQuoteParser::release()
{
    if (_released.load())
    {
        return;
    }

    // 停止Mock回放线程
    if (_mock_replay_running)
    {
        stop_mock_replay();
    }

    // 停止性能统计线程
    if (_performance_report_thread.joinable())
    {
        _performance_enabled = false;
        _performance_report_thread.join();
    }

    // 释放数据管理器
    if (_data_manager)
    {
        _data_manager->stop();
        _data_manager->release_data_source();
        _data_manager.reset();
    }

    {
        std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
        _index_data_cache.clear();
        _depth_data_cache.clear();
        _depth_fifty_data_cache.clear();
        _entrust_data_cache.clear();
        _trade_data_cache.clear();
    }

    // 清理类型化Mock数据
    _typed_mock_data_buffers.clear();
    _typed_mock_indexes.clear();
    _close_typed_mock_files();

    // 调用子类的清理
    release_impl();

    std::lock_guard<std::mutex> lock(_mutex);
    _initialized.store(false);
    _last_error.clear();

    // 清空最新数据
    memset(&_latest_index_data, 0, sizeof(_latest_index_data));
    memset(&_latest_depth_data, 0, sizeof(_latest_depth_data));
    memset(&_latest_depth_fifty_data, 0, sizeof(_latest_depth_fifty_data));
    memset(&_latest_entrust_data, 0, sizeof(_latest_entrust_data));
    memset(&_latest_trade_data, 0, sizeof(_latest_trade_data));

    // 重置Mock模式状态
    _mock_mode_enabled = false;
    _mock_data_loaded = false;
    _mock_replay_running = false;
    _mock_current_index = 0;
    memset(_mock_data_file, 0, sizeof(_mock_data_file));
    _released.store(true);
}

int BaseQuoteParser::start()
{
    if (!_initialized.load())
    {
        set_error("Parser not initialized");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    if (_running.load())
    {
        set_error("Parser already running");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    if (is_mock_mode())
    {
        // Mock模式启动
        if (start_mock_replay() != 0)
        {
            return -1;
        }
        _running.store(true);
        _connected.store(true);
        _current_status.store(STATUS_CONNECTED);
    }
    else
    {
        // 真实模式启动
        if (start_impl() != 0)
        {
            return -1;
        }

        _current_status.store(STATUS_CONNECTING);
    }

    return 0;
}

int BaseQuoteParser::connect()
{
    if (is_mock_mode())
    {
        _connected.store(true);
        return 0;
    }

    if (connect_impl() != 0)
    {
        set_error("Failed to connect parser");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    _running.store(true);
    return 0;
}

int BaseQuoteParser::disconnect()
{
    if (is_mock_mode())
    {
        _connected.store(false);
        _current_status.store(STATUS_DISCONNECTED);
        return 0;
    }

    if (disconnect_impl() != 0)
    {
        set_error("Failed to disconnect parser");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    _connected.store(false);
    _current_status.store(STATUS_DISCONNECTED);
    return 0;
}

int BaseQuoteParser::stop()
{
    if (!_running.load())
    {
        return 0; // Already stopped
    }

    // 停止性能统计线程
    if (_performance_report_thread.joinable())
    {
        _performance_enabled = false;
        _performance_report_thread.join();
        QUOTE_LOG_INFO("Performance reporting thread stopped");
    }

    if (disconnect_impl() != 0)
    {
        set_error("Failed to disconnect parser");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    if (stop_impl() != 0)
    {
        set_error("Failed to stop Parser");
        _current_status.store(STATUS_ERROR);
        return -1;
    }

    _running.store(false);
    _connected.store(false);
    _current_status.store(STATUS_DISCONNECTED);
    QUOTE_LOG_INFO("QuoteParser stopped");
    return 0;
}

bool BaseQuoteParser::is_connected() const
{
    return _connected.load();
}

bool BaseQuoteParser::is_running() const
{
    return _running.load();
}

bool BaseQuoteParser::is_initialized() const
{
    return _initialized.load();
}

Status BaseQuoteParser::get_status()
{
    return _current_status.load();
}

std::string BaseQuoteParser::get_last_error() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_error;
}

// ===== 性能统计实现 =====
void BaseQuoteParser::get_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (avg_latency_us)
    {
        uint64_t total_msgs = _total_messages.load();
        *avg_latency_us = total_msgs > 0 ? static_cast<double>(_total_latency_us) / total_msgs : 0.0;
    }
    if (max_latency_us)
        *max_latency_us = static_cast<double>(_max_latency_us);
    if (min_latency_us)
        *min_latency_us = _min_latency_us == UINT64_MAX ? 0.0 : static_cast<double>(_min_latency_us);
}

void BaseQuoteParser::reset_performance_stats()
{
    _total_messages.store(0);
    _dropped_messages.store(0);
    _messages_per_second.store(0);
    _total_latency_us = 0;
    _max_latency_us = 0;
    _min_latency_us = UINT64_MAX;
    _last_stats_time = std::chrono::steady_clock::now();
    _last_performance_report = std::chrono::steady_clock::now();
}

// ===== Mock模式实现 =====
bool BaseQuoteParser::is_mock_mode() const
{
    return _mock_mode_enabled;
}

int BaseQuoteParser::load_mock_data(const char *file_path)
{
    if (!file_path)
    {
        set_error("Invalid mock data file path");
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    strncpy(_mock_data_file, file_path, sizeof(_mock_data_file) - 1);
    _mock_data_file[sizeof(_mock_data_file) - 1] = '\0';

    return _load_mock_data() ? 0 : -1;
}

int BaseQuoteParser::start_mock_replay()
{
    if (!_mock_mode_enabled)
    {
        set_error("Not in mock mode");
        return -1;
    }

    return _start_mock_replay();
}

int BaseQuoteParser::stop_mock_replay()
{
    if (!_mock_mode_enabled)
    {
        set_error("Not in mock mode");
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _mock_replay_running = false;
    _mock_current_index = 0;

    if (_mock_replay_thread.joinable())
    {
        _mock_replay_thread.join();
    }

    QUOTE_LOG_INFO("Mock replay stopped");
    return 0;
}

// ===== 订阅管理实现 =====
int BaseQuoteParser::subscribe(const SubscriptionConfig *config)
{
    if (!config)
    {
        set_error("Invalid subscription config");
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _do_subscription_update(config);
    QUOTE_LOG_INFO("Subscription updated - markets: {}, data_types: {}, securities: {}",
                   config->market_count, config->data_type_count, config->security_count);
    return 0;
}

int BaseQuoteParser::unsubscribe()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 清空内部容器
    _subscribed_markets.clear();
    _subscribed_data_types.clear();
    _subscribed_codes.clear();

    // 设置为无订阅状态
    _has_subscription.store(false);

    QUOTE_LOG_INFO("All subscriptions cleared - no data will be processed");
    return 0;
}

int BaseQuoteParser::add_markets(MarketType *markets, int count)
{
    if (!markets || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int added_count = 0;

    for (int i = 0; i < count; ++i)
    {
        auto result = _subscribed_markets.insert(markets[i]);
        if (result.second)
        {
            added_count++;
        }
    }

    return added_count;
}

int BaseQuoteParser::add_data_types(FTDataType *data_types, int count)
{
    if (!data_types || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int added_count = 0;

    for (int i = 0; i < count; ++i)
    {
        auto result = _subscribed_data_types.insert(data_types[i]);
        if (result.second)
        {
            added_count++;
        }
    }

    return added_count;
}

int BaseQuoteParser::add_securities(SecurityInfo *securities, int count)
{
    if (!securities || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int added_count = 0;

    for (int i = 0; i < count; ++i)
    {
        std::string code = std::string(securities[i].code);
        auto result = _subscribed_codes.insert(code);
        if (result.second)
        {
            added_count++;
        }
    }

    return added_count;
}

int BaseQuoteParser::remove_markets(MarketType *markets, int count)
{
    if (!markets || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int removed_count = 0;

    for (int i = 0; i < count; ++i)
    {
        size_t erased = _subscribed_markets.erase(markets[i]);
        removed_count += erased;
    }

    return removed_count;
}

int BaseQuoteParser::remove_data_types(FTDataType *data_types, int count)
{
    if (!data_types || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int removed_count = 0;

    for (int i = 0; i < count; ++i)
    {
        size_t erased = _subscribed_data_types.erase(data_types[i]);
        removed_count += erased;
    }

    return removed_count;
}

int BaseQuoteParser::remove_securities(SecurityInfo *securities, int count)
{
    if (!securities || count <= 0)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    int removed_count = 0;

    for (int i = 0; i < count; ++i)
    {
        std::string code = std::string(securities[i].code);
        size_t erased = _subscribed_codes.erase(code);
        removed_count += erased;
    }

    return removed_count;
}

int BaseQuoteParser::update_subscription(const SubscriptionConfig *config)
{
    return subscribe(config);
}

bool BaseQuoteParser::is_market_subscribed(MarketType market) const
{
    // 如果没有订阅配置，则不订阅任何东西
    if (!_has_subscription.load())
    {
        return false;
    }

    // 如果有订阅配置，但市场列表为空，则订阅所有市场
    // 如果有订阅配置，且市场列表不为空，则只订阅指定市场
    return _subscribed_markets.empty() ||
           _subscribed_markets.find(market) != _subscribed_markets.end();
}

bool BaseQuoteParser::is_data_type_subscribed(FTDataType data_type) const
{
    // 如果没有订阅配置，则不订阅任何东西
    if (!_has_subscription.load())
    {
        return false;
    }

    // 如果设置了订阅配置，但数据类型列表为空，则订阅所有数据类型
    // 如果设置了订阅配置，且数据类型列表不为空，则只订阅指定数据类型
    return _subscribed_data_types.empty() ||
           _subscribed_data_types.find(data_type) != _subscribed_data_types.end();
}

bool BaseQuoteParser::is_code_subscribed(const char *code) const
{
    if (!code)
        return false;

    // 如果没有订阅配置，则不订阅任何东西
    if (!_has_subscription.load())
    {
        return false;
    }

    // 如果设置了订阅配置，但证券列表为空，则订阅所有证券
    // 如果设置了订阅配置，且证券列表不为空，则只订阅指定证券
    return _subscribed_codes.empty() ||
           _subscribed_codes.find(std::string(code)) != _subscribed_codes.end();
}

bool BaseQuoteParser::should_process_data(MarketType market, FTDataType data_type, const char *code) const
{
    return is_market_subscribed(market) &&
           is_data_type_subscribed(data_type) &&
           is_code_subscribed(code);
}

// ===== 回调函数注册 =====
IndexDataCallbackHandle BaseQuoteParser::set_index_data_callback(IndexDataCallbackType callback)
{
    if (!callback)
    {
        return IndexDataCallbackHandle{}; // 返回无效句柄
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _index_callbacks.append([callback](const IndexData *data)
                                       {
            try {
                callback(data);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in index data callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in index data callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register index data callback: {}", e.what());
        return IndexDataCallbackHandle{}; // 返回无效句柄
    }
}

int BaseQuoteParser::remove_index_data_callback(IndexDataCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _index_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove index data callback: {}", e.what());
        return -1;
    }
}

DepthDataCallbackHandle BaseQuoteParser::set_depth_data_callback(DepthDataCallbackType callback)
{
    if (!callback)
    {
        return DepthDataCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _depth_callbacks.append([callback](const DepthData *data)
                                       {
            try {
                callback(data);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in depth data callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in depth data callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register depth data callback: {}", e.what());
        return DepthDataCallbackHandle{};
    }
}

int BaseQuoteParser::remove_depth_data_callback(DepthDataCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _depth_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove depth data callback: {}", e.what());
        return -1;
    }
}

DepthFiftyDataCallbackHandle BaseQuoteParser::set_depth_fifty_data_callback(DepthFiftyDataCallbackType callback)
{
    if (!callback)
    {
        return DepthFiftyDataCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _depth_fifty_callbacks.append([callback](const DepthFiftyData *data)
                                             {
            try {
                callback(data);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in depth fifty data callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in depth fifty data callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register depth fifty data callback: {}", e.what());
        return DepthFiftyDataCallbackHandle{};
    }
}

int BaseQuoteParser::remove_depth_fifty_data_callback(DepthFiftyDataCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _depth_fifty_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove depth fifty data callback: {}", e.what());
        return -1;
    }
}

EntrustDataCallbackHandle BaseQuoteParser::set_entrust_data_callback(EntrustDataCallbackType callback)
{
    if (!callback)
    {
        return EntrustDataCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _entrust_callbacks.append([callback](const EntrustData *data)
                                         {
            try {
                callback(data);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in entrust data callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in entrust data callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register entrust data callback: {}", e.what());
        return EntrustDataCallbackHandle{};
    }
}

int BaseQuoteParser::remove_entrust_data_callback(EntrustDataCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _entrust_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove entrust data callback: {}", e.what());
        return -1;
    }
}

TradeDataCallbackHandle BaseQuoteParser::set_trade_data_callback(TradeDataCallbackType callback)
{
    if (!callback)
    {
        return TradeDataCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _trade_callbacks.append([callback](const TradeData *data)
                                       {
            try {
                callback(data);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in trade data callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in trade data callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register trade data callback: {}", e.what());
        return TradeDataCallbackHandle{};
    }
}

int BaseQuoteParser::remove_trade_data_callback(TradeDataCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _trade_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove trade data callback: {}", e.what());
        return -1;
    }
}

StatusCallbackHandle BaseQuoteParser::set_status_callback(QuoteStatusCallbackType callback)
{
    if (!callback)
    {
        return StatusCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _status_callbacks.append([callback](Status status, const char *message)
                                        {
            try {
                callback(status, message);
            } catch (const std::exception& e) {
                QUOTE_LOG_ERROR("Exception in status callback: {}", e.what());
            } catch (...) {
                QUOTE_LOG_ERROR("Unknown exception in status callback");
            } });
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to register status callback: {}", e.what());
        return StatusCallbackHandle{};
    }
}

int BaseQuoteParser::remove_status_callback(StatusCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _status_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Failed to remove status callback: {}", e.what());
        return -1;
    }
}

// ===== 数据获取接口 =====
int BaseQuoteParser::get_index_data(IndexData *data)
{
    if (!data)
    {
        set_error("Invalid data pointer");
        return -1;
    }

    if (_data_manager)
    {
        return _get_data_from_manager(DATA_TYPE_INDEX, data, sizeof(IndexData));
    }
    else
    {
        return _get_data_from_memory(DATA_TYPE_INDEX, data, sizeof(IndexData));
    }
}

int BaseQuoteParser::get_depth_data(DepthData *data)
{
    if (!data)
    {
        set_error("Invalid data pointer");
        return -1;
    }

    if (_data_manager)
    {
        return _get_data_from_manager(DATA_TYPE_DEPTH, data, sizeof(DepthData));
    }
    else
    {
        return _get_data_from_memory(DATA_TYPE_DEPTH, data, sizeof(DepthData));
    }
}

int BaseQuoteParser::get_depth_fifty_data(DepthFiftyData *data)
{
    if (!data)
    {
        set_error("Invalid data pointer");
        return -1;
    }

    if (_data_manager)
    {
        return _get_data_from_manager(DATA_TYPE_DEPTH_FIFTY, data, sizeof(DepthFiftyData));
    }
    else
    {
        return _get_data_from_memory(DATA_TYPE_DEPTH_FIFTY, data, sizeof(DepthFiftyData));
    }
}

int BaseQuoteParser::get_entrust_data(EntrustData *data)
{
    if (!data)
    {
        set_error("Invalid data pointer");
        return -1;
    }

    if (_data_manager)
    {
        return _get_data_from_manager(DATA_TYPE_ENTRUST, data, sizeof(EntrustData));
    }
    else
    {
        return _get_data_from_memory(DATA_TYPE_ENTRUST, data, sizeof(EntrustData));
    }
}

int BaseQuoteParser::get_trade_data(TradeData *data)
{
    if (!data)
    {
        set_error("Invalid data pointer");
        return -1;
    }

    if (_data_manager)
    {
        return _get_data_from_manager(DATA_TYPE_TRADE, data, sizeof(TradeData));
    }
    else
    {
        return _get_data_from_memory(DATA_TYPE_TRADE, data, sizeof(TradeData));
    }
}

int BaseQuoteParser::get_index_data_by_code(const char *code, IndexData *data)
{
    if (!code || !data)
    {
        set_error("Invalid parameters");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_code_cache_mutex);

    auto it = _index_data_cache.find(std::string(code));
    if (it == _index_data_cache.end())
    {
        return -1; // 未找到该证券的数据
    }

    *data = it->second;
    return 0;
}

int BaseQuoteParser::get_depth_data_by_code(const char *code, DepthData *data)
{
    if (!code || !data)
    {
        set_error("Invalid parameters");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_code_cache_mutex);

    auto it = _depth_data_cache.find(std::string(code));
    if (it == _depth_data_cache.end())
    {
        return -1;
    }

    *data = it->second;
    return 0;
}

int BaseQuoteParser::get_depth_fifty_data_by_code(const char *code, DepthFiftyData *data)
{
    if (!code || !data)
    {
        set_error("Invalid parameters");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_code_cache_mutex);

    auto it = _depth_fifty_data_cache.find(std::string(code));
    if (it == _depth_fifty_data_cache.end())
    {
        return -1;
    }

    *data = it->second;
    return 0;
}

int BaseQuoteParser::get_entrust_data_by_code(const char *code, EntrustData *data)
{
    if (!code || !data)
    {
        set_error("Invalid parameters");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_code_cache_mutex);

    auto it = _entrust_data_cache.find(std::string(code));
    if (it == _entrust_data_cache.end())
    {
        return -1;
    }

    *data = it->second;
    return 0;
}

int BaseQuoteParser::get_trade_data_by_code(const char *code, TradeData *data)
{
    if (!code || !data)
    {
        set_error("Invalid parameters");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_code_cache_mutex);

    auto it = _trade_data_cache.find(std::string(code));
    if (it == _trade_data_cache.end())
    {
        return -1;
    }

    *data = it->second;
    return 0;
}

void BaseQuoteParser::on_index_data_received(const IndexData &data)
{
    std::chrono::high_resolution_clock::time_point start_time;
    if (_performance_enabled)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::string code = data.security_info.code;
    _update_index_cache(data.security_info.code, data);

    // 数据存储
    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_INDEX, &data, sizeof(IndexData)))
        {
            QUOTE_LOG_WARN("Failed to write index data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_index_data = data;
    }

    // 触发所有回调（eventpp会处理并发安全）
    if (_callback_mode_enabled.load())
    {
        try
        {
            _index_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception while dispatching index data callbacks: {}", e.what());
        }
    }

    if (_performance_enabled)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        update_performance_stats(processing_time_ns);
    }
}

void BaseQuoteParser::on_depth_data_received(const DepthData &data)
{
    std::chrono::high_resolution_clock::time_point start_time;
    if (_performance_enabled)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::string code = data.security_info.code;
    _update_depth_cache(data.security_info.code, data);

    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_DEPTH, &data, sizeof(DepthData)))
        {
            QUOTE_LOG_WARN("Failed to write depth data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_depth_data = data;
    }

    if (_callback_mode_enabled.load())
    {
        try
        {
            _depth_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception while dispatching depth data callbacks: {}", e.what());
        }
    }

    if (_performance_enabled)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        update_performance_stats(processing_time_ns);
    }
}

void BaseQuoteParser::on_depth_fifty_data_received(const DepthFiftyData &data)
{
    std::chrono::high_resolution_clock::time_point start_time;
    if (_performance_enabled)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::string code = data.security_info.code;
    _update_depth_fifty_cache(data.security_info.code, data);

    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_DEPTH_FIFTY, &data, sizeof(DepthFiftyData)))
        {
            QUOTE_LOG_WARN("Failed to write depth fifty data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_depth_fifty_data = data;
    }

    if (_callback_mode_enabled.load())
    {
        try
        {
            _depth_fifty_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception while dispatching depth fifty data callbacks: {}", e.what());
        }
    }

    if (_performance_enabled)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        update_performance_stats(processing_time_ns);
    }
}

void BaseQuoteParser::on_entrust_data_received(const EntrustData &data)
{
    std::chrono::high_resolution_clock::time_point start_time;
    if (_performance_enabled)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::string code = data.security_info.code;
    _update_entrust_cache(data.security_info.code, data);

    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_ENTRUST, &data, sizeof(EntrustData)))
        {
            QUOTE_LOG_WARN("Failed to write entrust data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_entrust_data = data;
    }

    if (_callback_mode_enabled.load())
    {
        try
        {
            _entrust_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception while dispatching entrust data callbacks: {}", e.what());
        }
    }

    if (_performance_enabled)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        update_performance_stats(processing_time_ns);
    }
}

void BaseQuoteParser::on_trade_data_received(const TradeData &data)
{
    std::chrono::high_resolution_clock::time_point start_time;
    if (_performance_enabled)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::string code = data.security_info.code;
    _update_trade_cache(data.security_info.code, data);

    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_TRADE, &data, sizeof(TradeData)))
        {
            QUOTE_LOG_WARN("Failed to write trade data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_trade_data = data;
    }

    if (_callback_mode_enabled.load())
    {
        try
        {
            _trade_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception while dispatching trade data callbacks: {}", e.what());
        }
    }

    if (_performance_enabled)
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        update_performance_stats(processing_time_ns);
    }
}

void BaseQuoteParser::on_quote_status_received(Status status, const char *message)
{
    _current_status.store(status);

    if (status == STATUS_CONNECTED)
    {
        _connected.store(true);
    }
    else if (status == STATUS_DISCONNECTED || status == STATUS_ERROR)
    {
        _connected.store(false);
    }

    // 触发状态回调
    try
    {
        _status_callbacks(status, message);
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception while dispatching status callbacks: {}", e.what());
    }

    // 记录状态变化日志
    if (message)
    {
        QUOTE_LOG_INFO("Quote status changed to {}: {}", static_cast<int>(status), message);
    }
    else
    {
        QUOTE_LOG_INFO("Quote status changed to {}", static_cast<int>(status));
    }
}

// ===== 辅助方法 =====
void BaseQuoteParser::set_error(const std::string &error)
{
    _last_error = error;
    QUOTE_LOG_ERROR("BaseQuoteParser Error: {}", error);
}

void BaseQuoteParser::configure_performance_stats(const PerformanceConfig &config)
{
    _performance_enabled = config.enabled;
    _latency_stats_enabled = config.enable_latency_stats;
    _report_interval_ms = config.report_interval_ms;

    QUOTE_LOG_INFO("Performance config - enabled: {}, latency: {}, interval: {}ms",
                   _performance_enabled, _latency_stats_enabled, _report_interval_ms);

    if (_performance_enabled)
    {
        reset_performance_stats();
        if (_running.load())
        {
            _start_performance_reporting();
        }
    }
}

void BaseQuoteParser::configure_mock_mode(const MockConfig &config)
{
    _mock_mode_enabled = config.enabled;
    _mock_loop_replay = config.loop_replay;

    if (config.enabled && strlen(config.mock_data_file) > 0)
    {
        strncpy(_mock_data_file, config.mock_data_file, sizeof(_mock_data_file) - 1);
        _mock_data_file[sizeof(_mock_data_file) - 1] = '\0';

        QUOTE_LOG_INFO("Mock mode enabled - file: {}, loop: {}",
                       _mock_data_file, _mock_loop_replay);
    }
    
    // ===== 配置CSV模式 =====
    if (config.enabled && config.csv_mode)
    {
        if (_mock_generator)
        {
            _mock_generator->enable_csv_mode(true);
            
            // 设置CSV文件路径
            if (strlen(config.csv_depth_file) > 0)
            {
                _mock_generator->set_csv_file("depth", std::string(config.csv_depth_file));
                QUOTE_LOG_INFO("CSV depth file configured: {}", config.csv_depth_file);
            }
            
            if (strlen(config.csv_index_file) > 0)
            {
                _mock_generator->set_csv_file("index", std::string(config.csv_index_file));
                QUOTE_LOG_INFO("CSV index file configured: {}", config.csv_index_file);
            }
            
            if (strlen(config.csv_entrust_file) > 0)
            {
                _mock_generator->set_csv_file("entrust", std::string(config.csv_entrust_file));
                QUOTE_LOG_INFO("CSV entrust file configured: {}", config.csv_entrust_file);
            }
            
            if (strlen(config.csv_trade_file) > 0)
            {
                _mock_generator->set_csv_file("trade", std::string(config.csv_trade_file));
                QUOTE_LOG_INFO("CSV trade file configured: {}", config.csv_trade_file);
            }
            
            if (strlen(config.csv_depth_fifty_file) > 0)
            {
                _mock_generator->set_csv_file("depth_fifty", std::string(config.csv_depth_fifty_file));
                QUOTE_LOG_INFO("CSV depth_fifty file configured: {}", config.csv_depth_fifty_file);
            }
            
            // 加载CSV数据
            if (!_mock_generator->load_csv_data())
            {
                QUOTE_LOG_ERROR("Failed to load CSV data files");
            }
            else
            {
                QUOTE_LOG_INFO("CSV mode enabled successfully");
            }
        }
        else
        {
            QUOTE_LOG_ERROR("Mock generator not available for CSV mode");
        }
    }
}

void BaseQuoteParser::update_performance_stats(uint64_t processing_time_ns)
{
    if (!_performance_enabled)
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    _total_messages.fetch_add(1);

    if (_latency_stats_enabled)
    {
        // 转换为微秒进行统计
        uint64_t processing_latency_us = processing_time_ns / 1000;

        std::lock_guard<std::mutex> lock(_mutex);
        _total_latency_us += processing_latency_us;
        _max_latency_us = std::max(_max_latency_us, processing_latency_us);
        _min_latency_us = std::min(_min_latency_us, processing_latency_us);
    }
}

void BaseQuoteParser::_start_performance_reporting()
{
    if (_performance_report_thread.joinable())
    {
        _performance_report_thread.join();
    }

    if (!_performance_enabled)
    {
        return;
    }

    _performance_report_thread = std::thread([this]()
                                             {
        QUOTE_LOG_INFO("Performance reporting thread started, interval: {}ms", _report_interval_ms);
        
        // 同时检查性能开关和解析器运行状态
        while (_performance_enabled && _running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(_report_interval_ms));
            
            // 双重检查：性能开关和解析器状态
            if (!_performance_enabled || !_running.load()) break;
            
            _report_performance_stats();
        }
        
        QUOTE_LOG_INFO("Performance reporting thread stopped"); });
}

void BaseQuoteParser::_report_performance_stats()
{
    auto now = std::chrono::steady_clock::now();

    // 计算自上次报告以来的时间间隔和消息数量
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_performance_report);
    double seconds = duration.count() / 1000.0;

    uint64_t total_msgs = _total_messages.load();
    static uint64_t last_total_messages = 0;
    uint64_t current_period_messages = total_msgs - last_total_messages;
    last_total_messages = total_msgs;

    // 计算当前周期的TPS
    uint64_t current_tps = seconds > 0 ? static_cast<uint64_t>(current_period_messages / seconds) : 0;
    _messages_per_second.store(current_tps);

    // 更新报告时间
    _last_performance_report = now;

    uint64_t dropped_msgs = _dropped_messages.load();

    double avg_latency_us, max_latency_us, min_latency_us;
    get_latency_stats(&avg_latency_us, &max_latency_us, &min_latency_us);

    // 提供多种时间单位的显示
    QUOTE_LOG_INFO("Performance Stats - TPS: {} (period: {:.1f}s, msgs: {}), Total: {}, Dropped: {}",
                   current_tps, seconds, current_period_messages, total_msgs, dropped_msgs);

    if (_latency_stats_enabled && total_msgs > 0)
    {
        QUOTE_LOG_INFO("Latency Stats - Avg: {:.2f}μs ({:.0f}ns), Max: {:.2f}μs ({:.0f}ns), Min: {:.2f}μs ({:.0f}ns)",
                       avg_latency_us, avg_latency_us * 1000,
                       max_latency_us, max_latency_us * 1000,
                       min_latency_us, min_latency_us * 1000);

        // 根据延迟大小选择合适的单位显示
        if (avg_latency_us < 1.0)
        {
            QUOTE_LOG_INFO("Processing time per message: ~{:.0f} nanoseconds", avg_latency_us * 1000);
        }
        else if (avg_latency_us < 1000.0)
        {
            QUOTE_LOG_INFO("Processing time per message: ~{:.2f} microseconds", avg_latency_us);
        }
        else
        {
            QUOTE_LOG_INFO("Processing time per message: ~{:.2f} milliseconds", avg_latency_us / 1000.0);
        }
    }
}

bool BaseQuoteParser::_load_mock_data()
{
    if (strlen(_mock_data_file) == 0)
    {
        QUOTE_LOG_ERROR("Mock data file path not specified");
        set_error("Mock data file path not specified");
        return false;
    }

    // 清理之前的数据
    _typed_mock_data_buffers.clear();
    _typed_mock_indexes.clear();
    _close_typed_mock_files();

    std::string base_path = std::string(_mock_data_file);
    QUOTE_LOG_INFO("Loading typed mock data from base path: {}", base_path);

    // 获取需要加载的数据类型
    std::vector<FTDataType> data_types_to_load;
    if (_has_subscription.load() && !_subscribed_data_types.empty())
    {
        for (FTDataType data_type : _subscribed_data_types)
        {
            data_types_to_load.push_back(data_type);
        }
    }
    else
    {
        // 默认加载所有基本数据类型
        data_types_to_load = {DATA_TYPE_INDEX, DATA_TYPE_DEPTH, DATA_TYPE_DEPTH_FIFTY,
                              DATA_TYPE_ENTRUST, DATA_TYPE_TRADE};
    }

    bool any_file_loaded = false;

    // 为每种数据类型加载对应的文件
    for (FTDataType data_type : data_types_to_load)
    {
        std::string typed_file_path = _get_typed_mock_file_path(base_path, data_type);

        if (_load_typed_mock_file(data_type, typed_file_path))
        {
            any_file_loaded = true;
            QUOTE_LOG_INFO("Loaded mock data for type {} from: {}",
                           static_cast<int>(data_type), typed_file_path);
        }
        else
        {
            QUOTE_LOG_WARN("Failed to load mock data for type {} from: {}",
                           static_cast<int>(data_type), typed_file_path);
        }
    }

    if (!any_file_loaded)
    {
        QUOTE_LOG_WARN("No mock data files found, will generate synthetic data");
        _mock_use_synthetic_data = true;
    }
    else
    {
        _mock_use_synthetic_data = false;
        QUOTE_LOG_INFO("Loaded typed mock data for {} data types", _typed_mock_data_buffers.size());
    }

    _mock_data_loaded = true;
    _mock_current_index = 0;

    // 重置所有数据类型的索引
    for (auto &pair : _typed_mock_data_buffers)
    {
        _typed_mock_indexes[pair.first] = 0;
    }

    return true;
}

// 获取数据类型对应的Mock文件路径
std::string BaseQuoteParser::_get_typed_mock_file_path(const std::string &base_path, FTDataType data_type)
{
    // 使用与TypedFileDataSource相同的文件后缀映射
    static const std::unordered_map<FTDataType, std::string> type_suffixes = {
        {DATA_TYPE_INDEX, "_index.dat"},
        {DATA_TYPE_DEPTH, "_depth.dat"},
        {DATA_TYPE_DEPTH_FIFTY, "_depth50.dat"},
        {DATA_TYPE_ENTRUST, "_entrust.dat"},
        {DATA_TYPE_TRADE, "_trade.dat"},
    };

    auto it = type_suffixes.find(data_type);
    if (it != type_suffixes.end())
    {
        return base_path + it->second;
    }

    // 未知数据类型，使用通用后缀
    return base_path + "_type" + std::to_string(static_cast<int>(data_type)) + ".dat";
}

// 加载指定数据类型的Mock文件
bool BaseQuoteParser::_load_typed_mock_file(FTDataType data_type, const std::string &file_path)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (file_size == 0)
    {
        file.close();
        return false;
    }

    std::vector<TypedMockData> data_buffer;
    int record_count = 0;

    while (file.good() && !file.eof())
    {
        TypedMockData mock_data;

        // 记录当前文件位置
        std::streampos record_start = file.tellg();

        // 读取记录头（时间戳 + 数据类型 + 数据大小）
        file.read(reinterpret_cast<char *>(&mock_data.timestamp), sizeof(mock_data.timestamp));
        if (file.gcount() != sizeof(mock_data.timestamp))
        {
            break; // 文件结尾
        }

        file.read(reinterpret_cast<char *>(&mock_data.data_type), sizeof(mock_data.data_type));
        if (file.gcount() != sizeof(mock_data.data_type))
        {
            QUOTE_LOG_WARN("Incomplete record in typed mock file: {}, incomplete data_type", file_path);
            break;
        }

        file.read(reinterpret_cast<char *>(&mock_data.data_size), sizeof(mock_data.data_size));
        if (file.gcount() != sizeof(mock_data.data_size))
        {
            QUOTE_LOG_WARN("Incomplete record in typed mock file: {}, incomplete data_size", file_path);
            break;
        }

        // 验证数据完整性
        if (mock_data.data_size == 0 || mock_data.data_size > sizeof(mock_data.data))
        {
            QUOTE_LOG_WARN("Invalid data size in typed mock file: {}, size: {}, max allowed: {}",
                           file_path, mock_data.data_size, sizeof(mock_data.data));

            // 尝试跳过这条记录
            if (mock_data.data_size <= 1024 * 1024)
            { // 合理的大小限制
                file.seekg(record_start + static_cast<std::streamoff>(
                                              sizeof(uint64_t) + sizeof(FTDataType) + sizeof(uint32_t) + mock_data.data_size));
            }
            else
            {
                QUOTE_LOG_ERROR("Record size too large, stopping file read: {}", file_path);
                break;
            }
            continue;
        }

        // 读取实际数据
        file.read(reinterpret_cast<char *>(mock_data.data), mock_data.data_size);
        if (file.gcount() != static_cast<std::streamsize>(mock_data.data_size))
        {
            QUOTE_LOG_WARN("Incomplete data in typed mock file: {}, expected: {}, got: {}",
                           file_path, mock_data.data_size, file.gcount());
            break;
        }

        // 只保存匹配当前数据类型的记录
        if (mock_data.data_type == data_type)
        {
            data_buffer.push_back(mock_data);
        }

        record_count++;
    }

    file.close();

    if (data_buffer.empty())
    {
        return false;
    }

    _typed_mock_data_buffers[data_type] = std::move(data_buffer);
    _typed_mock_indexes[data_type] = 0;

    QUOTE_LOG_INFO("Loaded {} records for data type {} from file: {}",
                   _typed_mock_data_buffers[data_type].size(), static_cast<int>(data_type), file_path);

    return true;
}

// 修改Mock回放循环逻辑
void BaseQuoteParser::_mock_replay_loop()
{
    QUOTE_LOG_INFO("Typed mock replay loop started with high precision timing");

    const auto synthetic_interval = std::chrono::microseconds(1); // 1微秒 - 生成数据
    const auto replay_interval = std::chrono::nanoseconds(500);   // 500纳秒 - 重放数据

    auto last_synthetic_time = std::chrono::high_resolution_clock::now();
    auto last_replay_time = std::chrono::high_resolution_clock::now();

    while (_mock_replay_running)
    {
        try
        {
            auto now = std::chrono::high_resolution_clock::now();

            if (_mock_use_synthetic_data)
            {
                if (now - last_synthetic_time >= synthetic_interval)
                {
                    _generate_mock_data();
                    last_synthetic_time = now;

                    if (!_mock_loop_replay && _mock_current_index >= 1000)
                    {
                        QUOTE_LOG_INFO("Mock replay finished (synthetic data, no loop)");
                        break;
                    }
                }
            }
            else
            {
                if (now - last_replay_time >= replay_interval)
                {
                    bool any_data_available = _replay_typed_mock_data();

                    if (!any_data_available)
                    {
                        if (_mock_loop_replay)
                        {
                            _reset_typed_mock_indexes();
                        }
                        else
                        {
                            QUOTE_LOG_INFO("Mock replay finished (typed file data, no loop)");
                            break;
                        }
                    }

                    last_replay_time = now;
                }
            }

            // 非常短的sleep，避免100%占用CPU
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }
        catch (const std::exception &e)
        {
            QUOTE_LOG_ERROR("Exception in typed mock replay loop: {}", e.what());
            break;
        }
    }

    _mock_replay_running = false;
    QUOTE_LOG_INFO("Typed mock replay loop finished");
}

// 回放类型化Mock数据
bool BaseQuoteParser::_replay_typed_mock_data()
{
    bool any_data_replayed = false;

    // 遍历所有数据类型，按时间顺序回放
    for (auto &buffer_pair : _typed_mock_data_buffers)
    {
        FTDataType data_type = buffer_pair.first;
        const auto &data_buffer = buffer_pair.second;
        size_t &current_index = _typed_mock_indexes[data_type];

        if (current_index >= data_buffer.size())
        {
            continue; // 这种数据类型已经播放完毕
        }

        const TypedMockData &mock_data = data_buffer[current_index];

        // 根据数据类型回放数据
        if (_replay_typed_mock_record(data_type, mock_data))
        {
            current_index++;
            any_data_replayed = true;
        }
    }

    return any_data_replayed;
}

// 回放单条类型化Mock记录
bool BaseQuoteParser::_replay_typed_mock_record(FTDataType data_type, const TypedMockData &mock_data)
{
    switch (data_type)
    {
    case DATA_TYPE_INDEX:
    {
        if (mock_data.data_size == sizeof(IndexData))
        { // 精确匹配
            const IndexData *index_data = reinterpret_cast<const IndexData *>(mock_data.data);
            if (should_process_data(index_data->security_info.market, DATA_TYPE_INDEX, index_data->security_info.code))
            {
                on_index_data_received(*index_data);
                return true;
            }
        }
        else
        {
            QUOTE_LOG_WARN("Size mismatch for IndexData: expected {}, got {}",
                           sizeof(IndexData), mock_data.data_size);
        }
        break;
    }

    case DATA_TYPE_DEPTH:
    {
        if (mock_data.data_size == sizeof(DepthData))
        {
            const DepthData *depth_data = reinterpret_cast<const DepthData *>(mock_data.data);
            if (should_process_data(depth_data->security_info.market, DATA_TYPE_DEPTH, depth_data->security_info.code))
            {
                on_depth_data_received(*depth_data);
                return true;
            }
        }
        else
        {
            QUOTE_LOG_WARN("Size mismatch for DepthData: expected {}, got {}",
                           sizeof(DepthData), mock_data.data_size);
        }
        break;
    }

    case DATA_TYPE_DEPTH_FIFTY:
    {
        if (mock_data.data_size == sizeof(DepthFiftyData))
        {
            const DepthFiftyData *depth_fifty_data = reinterpret_cast<const DepthFiftyData *>(mock_data.data);
            if (should_process_data(depth_fifty_data->security_info.market, DATA_TYPE_DEPTH_FIFTY, depth_fifty_data->security_info.code))
            {
                on_depth_fifty_data_received(*depth_fifty_data);
                return true;
            }
        }
        else
        {
            QUOTE_LOG_WARN("Size mismatch for DepthFiftyData: expected {}, got {}",
                           sizeof(DepthFiftyData), mock_data.data_size);
        }
        break;
    }

    case DATA_TYPE_ENTRUST:
    {
        if (mock_data.data_size == sizeof(EntrustData))
        {
            const EntrustData *entrust_data = reinterpret_cast<const EntrustData *>(mock_data.data);
            if (should_process_data(entrust_data->security_info.market, DATA_TYPE_ENTRUST, entrust_data->security_info.code))
            {
                on_entrust_data_received(*entrust_data);
                return true;
            }
        }
        else
        {
            QUOTE_LOG_WARN("Size mismatch for EntrustData: expected {}, got {}",
                           sizeof(EntrustData), mock_data.data_size);
        }
        break;
    }

    case DATA_TYPE_TRADE:
    {
        if (mock_data.data_size == sizeof(TradeData))
        {
            const TradeData *trade_data = reinterpret_cast<const TradeData *>(mock_data.data);
            if (should_process_data(trade_data->security_info.market, DATA_TYPE_TRADE, trade_data->security_info.code))
            {
                on_trade_data_received(*trade_data);
                return true;
            }
        }
        else
        {
            QUOTE_LOG_WARN("Size mismatch for TradeData: expected {}, got {}",
                           sizeof(TradeData), mock_data.data_size);
        }
        break;
    }

    default:
        QUOTE_LOG_WARN("Unknown data type in typed mock replay: {}", static_cast<int>(data_type));
        break;
    }

    return false;
}

// 重置所有数据类型的Mock索引
void BaseQuoteParser::_reset_typed_mock_indexes()
{
    for (auto &pair : _typed_mock_indexes)
    {
        pair.second = 0;
    }
    _mock_current_index = 0;
}

// 关闭所有类型化Mock文件流
void BaseQuoteParser::_close_typed_mock_files()
{
    for (auto &pair : _typed_mock_file_streams)
    {
        if (pair.second.is_open())
        {
            pair.second.close();
        }
    }
    _typed_mock_file_streams.clear();
}

int BaseQuoteParser::_start_mock_replay()
{
    if (!_mock_data_loaded)
    {
        set_error("Mock data not loaded");
        return -1;
    }

    if (_mock_replay_running)
    {
        QUOTE_LOG_WARN("Mock replay already running");
        return 0;
    }

    // 在这里设置mock生成器
    if (!_mock_generator_setup)
    {
        _setup_mock_generator();
        _mock_generator_setup = true;
    }

    _mock_replay_running = true;
    _mock_current_index = 0;

    try
    {
        _mock_replay_thread = std::thread([this]()
                                          { _mock_replay_loop(); });
    }
    catch (const std::exception &e)
    {
        _mock_replay_running = false;
        set_error("Failed to start mock replay thread: " + std::string(e.what()));
        return -1;
    }

    QUOTE_LOG_INFO("Mock replay started ({})",
                   _mock_use_synthetic_data ? "synthetic data" : "file data");
    return 0;
}

void BaseQuoteParser::_generate_mock_data()
{
    if (!_running.load() || !_mock_generator)
    {
        return;
    }

    _mock_generator->generate_mock_data_round();

    _mock_current_index++;

    if (_mock_loop_replay && _mock_current_index >= 1000)
    {
        _mock_current_index = 0;
        _mock_generator->reset_sequence();
    }
}

// ===== 私有方法：设置mock生成器回调 =====
void BaseQuoteParser::_setup_mock_generator()
{
    if (!_mock_generator)
    {
        return;
    }

    // 设置各种数据类型的回调，在回调中进行过滤
    _mock_generator->set_index_callback([this](const IndexData &data)
                                        { this->_on_mock_index_data(data); });

    _mock_generator->set_depth_callback([this](const DepthData &data)
                                        { this->_on_mock_depth_data(data); });

    _mock_generator->set_depth_fifty_callback([this](const DepthFiftyData &data)
                                              { this->_on_mock_depth_fifty_data(data); });

    _mock_generator->set_entrust_callback([this](const EntrustData &data)
                                          { this->_on_mock_entrust_data(data); });

    _mock_generator->set_trade_callback([this](const TradeData &data)
                                        { this->_on_mock_trade_data(data); });
}

void BaseQuoteParser::_on_mock_index_data(const IndexData &data)
{
    if (should_process_data(data.security_info.market, DATA_TYPE_INDEX, data.security_info.code))
    {
        this->on_index_data_received(data);
    }
}

void BaseQuoteParser::_on_mock_depth_data(const DepthData &data)
{
    if (should_process_data(data.security_info.market, DATA_TYPE_DEPTH, data.security_info.code))
    {
        this->on_depth_data_received(data);
    }
}

void BaseQuoteParser::_on_mock_depth_fifty_data(const DepthFiftyData &data)
{
    if (should_process_data(data.security_info.market, DATA_TYPE_DEPTH_FIFTY, data.security_info.code))
    {
        this->on_depth_fifty_data_received(data);
    }
}

void BaseQuoteParser::_on_mock_entrust_data(const EntrustData &data)
{
    if (should_process_data(data.security_info.market, DATA_TYPE_ENTRUST, data.security_info.code))
    {
        this->on_entrust_data_received(data);
    }
}

void BaseQuoteParser::_on_mock_trade_data(const TradeData &data)
{
    if (should_process_data(data.security_info.market, DATA_TYPE_TRADE, data.security_info.code))
    {
        this->on_trade_data_received(data);
    }
}

void BaseQuoteParser::_update_index_cache(const std::string &code, const IndexData &data)
{
    std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
    _index_data_cache[code] = data;
}

void BaseQuoteParser::_update_depth_cache(const std::string &code, const DepthData &data)
{
    std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
    _depth_data_cache[code] = data;
}

void BaseQuoteParser::_update_depth_fifty_cache(const std::string &code, const DepthFiftyData &data)
{
    std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
    _depth_fifty_data_cache[code] = data;
}

void BaseQuoteParser::_update_entrust_cache(const std::string &code, const EntrustData &data)
{
    std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
    _entrust_data_cache[code] = data;
}

void BaseQuoteParser::_update_trade_cache(const std::string &code, const TradeData &data)
{
    std::unique_lock<std::shared_mutex> lock(_code_cache_mutex);
    _trade_data_cache[code] = data;
}

// ===== 通用数据解析辅助方法 =====
// TODO 需要柜台确认字段值
TradingPhase BaseQuoteParser::parse_sse_trading_phase(const char *phase_code)
{
    if (!phase_code)
        return TRADING_PHASE_UNKNOWN;

    // 上交所交易阶段代码第1位表示交易阶段
    char first_char = phase_code[0];

    switch (first_char)
    {
    case 'S':
        return TRADING_PHASE_BEFORE_OPEN; // 启动（开市前）
    case 'O':
        return TRADING_PHASE_OPEN_AUCTION; // 开盘集合竞价
    case 'T':
        return TRADING_PHASE_CONTINUOUS; // 连续竞价
    case 'B':
        return TRADING_PHASE_SUSPENDED; // 休市
    case 'C':
    case 'U':
        return TRADING_PHASE_CLOSE_AUCTION; // 收盘集合竞价
    case 'E':
        return TRADING_PHASE_AFTER_CLOSE; // 已闭市
    case 'H':
    case 'P':
        return TRADING_PHASE_SUSPENDED; // 临时停牌
    case 'A':
        return TRADING_PHASE_AFTER_CLOSE; // 盘后交易
    case 'V':
        return TRADING_PHASE_SUSPENDED; // 波动性中断
    case 'M':
        return TRADING_PHASE_OPEN_AUCTION; // 可恢复交易的熔断（盘中集合竞价）
    case 'N':
        return TRADING_PHASE_SUSPENDED; // 不可恢复交易的熔断
    default:
        return TRADING_PHASE_UNKNOWN;
    }
}

// TODO 需要柜台确认字段值
TradingPhase BaseQuoteParser::parse_szse_trading_phase(const char *phase_code)
{
    if (!phase_code)
        return TRADING_PHASE_UNKNOWN;

    // 深交所交易阶段代码第0位表示交易阶段，第1位表示状态
    char first_char = phase_code[0];

    switch (first_char)
    {
    case 'S':
        return TRADING_PHASE_BEFORE_OPEN; // 启动（开市前）
    case 'O':
        return TRADING_PHASE_OPEN_AUCTION; // 开盘集合竞价
    case 'T':
        return TRADING_PHASE_CONTINUOUS; // 连续竞价
    case 'B':
        return TRADING_PHASE_SUSPENDED; // 休市
    case 'C':
        return TRADING_PHASE_CLOSE_AUCTION; // 收盘集合竞价
    case 'E':
        return TRADING_PHASE_AFTER_CLOSE; // 已闭市
    case 'H':
        return TRADING_PHASE_SUSPENDED; // 临时停牌
    case 'A':
        return TRADING_PHASE_AFTER_CLOSE; // 盘后交易
    case 'V':
        return TRADING_PHASE_SUSPENDED; // 波动性中断
    default:
        return TRADING_PHASE_UNKNOWN;
    }
}

bool BaseQuoteParser::_initialize_data_manager(const DataConfig &config)
{
    try
    {
        _data_manager = std::make_unique<DataManager>("quote_parser");

        // 需要从订阅配置中获取数据类型
        std::vector<FTDataType> enabled_data_types;
        if (_has_subscription.load() && !_subscribed_data_types.empty())
        {
            for (FTDataType data_type : _subscribed_data_types)
            {
                enabled_data_types.push_back(data_type);
            }
        }
        else
        {
            // 如果没有订阅配置，启用所有基本数据类型
            enabled_data_types = {DATA_TYPE_INDEX, DATA_TYPE_DEPTH, DATA_TYPE_DEPTH_FIFTY,
                                  DATA_TYPE_ENTRUST, DATA_TYPE_TRADE};
        }

        // 注册数据源时传递数据类型和大小
        size_t size_per_type = 64 * 1024 * 1024; // 64MB per data type
        if (!_data_manager->register_data_source(config, enabled_data_types, size_per_type))
        {
            set_error("Failed to register data source: " + _data_manager->get_last_error());
            return false;
        }

        // 验证数据源是否可用
        if (!_data_manager->is_available())
        {
            set_error("Data manager is not available after initialization");
            return false;
        }

        QUOTE_LOG_INFO("Data manager initialized successfully, type: {}, enabled {} data types",
                       static_cast<int>(_data_manager->get_current_source_type()), enabled_data_types.size());
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Exception initializing data manager: " + std::string(e.what()));
        return false;
    }
}

bool BaseQuoteParser::write_to_data_manager(FTDataType data_type, const void *data, size_t size)
{
    if (!_data_manager || !_data_manager->is_available())
    {
        return false;
    }

    try
    {
        ssize_t result = _data_manager->write(data_type, data, size);
        if (result < 0)
        {
            _dropped_messages.fetch_add(1);
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception writing to data manager: {}", e.what());
        _dropped_messages.fetch_add(1);
        return false;
    }
}

int BaseQuoteParser::_get_data_from_manager(FTDataType data_type, void *data, size_t data_size)
{
    if (!_data_manager || !_data_manager->is_available())
    {
        set_error("Data manager not available");
        return -1;
    }

    ssize_t bytes_read = _data_manager->read(data_type, data, data_size);
    if (bytes_read < 0)
    {
        set_error("Failed to read from data manager: " + _data_manager->get_last_error());
        return -1;
    }

    if (bytes_read > 0 && static_cast<size_t>(bytes_read) != data_size)
    {
        set_error("Data size mismatch in data manager read");
        return -1;
    }

    return bytes_read;
}

int BaseQuoteParser::_get_data_from_memory(FTDataType data_type, void *data, size_t data_size)
{
    std::lock_guard<std::mutex> lock(_mutex);

    switch (data_type)
    {
    case DATA_TYPE_INDEX:
        if (data_size != sizeof(IndexData))
        {
            set_error("Invalid data size for index data");
            return -1;
        }
        memcpy(data, &_latest_index_data, sizeof(IndexData));
        break;

    case DATA_TYPE_DEPTH:
        if (data_size != sizeof(DepthData))
        {
            set_error("Invalid data size for depth data");
            return -1;
        }
        memcpy(data, &_latest_depth_data, sizeof(DepthData));
        break;

    case DATA_TYPE_DEPTH_FIFTY:
        if (data_size != sizeof(DepthFiftyData))
        {
            set_error("Invalid data size for depth fifty data");
            return -1;
        }
        memcpy(data, &_latest_depth_fifty_data, sizeof(DepthFiftyData));
        break;

    case DATA_TYPE_ENTRUST:
        if (data_size != sizeof(EntrustData))
        {
            set_error("Invalid data size for entrust data");
            return -1;
        }
        memcpy(data, &_latest_entrust_data, sizeof(EntrustData));
        break;

    case DATA_TYPE_TRADE:
        if (data_size != sizeof(TradeData))
        {
            set_error("Invalid data size for trade data");
            return -1;
        }
        memcpy(data, &_latest_trade_data, sizeof(TradeData));
        break;

    default:
        set_error("Unsupported data type: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    return 0;
}

// ===== 私有方法实现 =====
void BaseQuoteParser::_initialize_subscription_config(const SubscriptionConfig *config)
{
    _do_subscription_update(config);
    QUOTE_LOG_INFO("Subscription initialized - markets: {}, data_types: {}, securities: {}",
                   config->market_count, config->data_type_count, config->security_count);
}

void BaseQuoteParser::_do_subscription_update(const SubscriptionConfig *config)
{
    // 清空现有订阅
    _subscribed_markets.clear();
    _subscribed_data_types.clear();
    _subscribed_codes.clear();

    // 直接填充内部容器，不需要保存原始配置
    for (int i = 0; i < config->market_count; ++i)
    {
        _subscribed_markets.insert(config->markets[i]);
    }

    for (int i = 0; i < config->data_type_count; ++i)
    {
        _subscribed_data_types.insert(config->data_types[i]);
    }

    for (int i = 0; i < config->security_count; ++i)
    {
        std::string code = std::string(config->securities[i].code);
        _subscribed_codes.insert(code);
    }

    // 设置订阅状态
    _has_subscription.store(true);
}

NS_FT_END
