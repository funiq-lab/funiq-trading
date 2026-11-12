#include "trade/base_trade_handler.h"
#include "logger/logger.hpp"
#include "utils/time.h"
#include <cstring>
#include <algorithm>

NS_FT_BEGIN

BaseTradeHandler::BaseTradeHandler()
    : _running(false),
      _initialized(false),
      _connected(false),
      _login_mode(LOGIN_MODE_CUST_ID),
      _current_status(STATUS_DISCONNECTED),
      _released(false),
      _callback_mode_enabled(true),
      _next_request_id(1),
      _request_id_start_time(std::chrono::steady_clock::now()),
      _performance_enabled(false),
      _latency_stats_enabled(false),
      _report_interval_ms(1000),
      _total_orders_sent(0),
      _total_orders_responded(0),
      _total_cancels_sent(0),
      _total_cancels_responded(0),
      _total_order_latency_us(0),
      _total_cancel_latency_us(0),
      _max_order_latency_us(0),
      _max_cancel_latency_us(0),
      _min_order_latency_us(UINT64_MAX),
      _min_cancel_latency_us(UINT64_MAX),
      _last_performance_report(std::chrono::steady_clock::now()),
      _order_rtn_callbacks(),
      _order_query_callbacks(),
      _fill_callbacks(),
      _position_callbacks(),
      _account_callbacks(),
      _error_callbacks(),
      _bank_securities_transfer_callbacks(),
      _fund_transfer_callbacks(),
      _order_rsp_callbacks(),
      _cancel_rsp_callbacks(),
      _data_manager(nullptr),
      _logged_in_accounts()
{

    memset(&_latest_order_rtn_data, 0, sizeof(_latest_order_rtn_data));
    memset(&_latest_order_query_data, 0, sizeof(_latest_order_query_data));
    memset(&_latest_fill_data, 0, sizeof(_latest_fill_data));
    memset(&_latest_position_data, 0, sizeof(_latest_position_data));
    memset(&_latest_bank_securities_transfer_data, 0, sizeof(_latest_bank_securities_transfer_data));
    memset(&_latest_fund_transfer_data, 0, sizeof(_latest_fund_transfer_data));
    memset(&_latest_order_rsp_data, 0, sizeof(_latest_order_rsp_data));
    memset(&_latest_cancel_rsp_data, 0, sizeof(_latest_cancel_rsp_data));
}

BaseTradeHandler::~BaseTradeHandler()
{
    release();
}

int BaseTradeHandler::init(const TradeConfig *config)
{
    TRADE_LOG_INFO("BaseTradeHandler::init");
    std::lock_guard<std::mutex> lock(_mutex);

    if (_initialized.load())
    {
        set_error("Handler already initialized");
        return -1;
    }

    if (!config)
    {
        set_error("Invalid configuration");
        return -1;
    }

    // 设置登陆方式
    _login_mode.store(config->login_mode);
    TRADE_LOG_INFO("Login mode: {}", config->login_mode == LOGIN_MODE_CUST_ID ? "CUST_ID" : "FUND_ACCOUNT_ID");

    // 设置回调模式
    _callback_mode_enabled.store(config->callback_mode);
    TRADE_LOG_INFO("Callback mode: {}", config->callback_mode ? "enabled" : "disabled");

    // 加载账户配置
    _load_accounts_from_config(config);

    configure_performance_stats(config->performance);

    if (config->data_config.output_type != DATA_OUTPUT_NONE)
    {
        if (!_initialize_data_manager(config->data_config))
        {
            TRADE_LOG_ERROR("Failed to initialize data manager");
            return -1;
        }
        TRADE_LOG_INFO("Data manager initialized");
    }
    else
    {
        TRADE_LOG_INFO("No data output configured, using memory cache");
    }

    if (config->callback_mode)
    {
        TRADE_LOG_INFO("Callback mode enabled, data manager will not be used for callbacks");
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

void BaseTradeHandler::release()
{
    if (_released.load())
    {
        return;
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

    // 调用子类的清理
    release_impl();

    std::lock_guard<std::mutex> lock(_mutex);
    _initialized.store(false);
    _running.store(false);
    _connected.store(false);
    _last_error.clear();

    // 清空最新数据
    memset(&_latest_order_rtn_data, 0, sizeof(_latest_order_rtn_data));
    memset(&_latest_fill_data, 0, sizeof(_latest_fill_data));
    memset(&_latest_position_data, 0, sizeof(_latest_position_data));
    memset(&_latest_bank_securities_transfer_data, 0, sizeof(_latest_bank_securities_transfer_data));
    memset(&_latest_fund_transfer_data, 0, sizeof(_latest_fund_transfer_data));
    memset(&_latest_order_rsp_data, 0, sizeof(_latest_order_rsp_data));
    memset(&_latest_cancel_rsp_data, 0, sizeof(_latest_cancel_rsp_data));

    // 清空账户数据
    _accounts.clear();
    _latest_account_data.clear();

    _released.store(true);
}

bool BaseTradeHandler::is_connected() const
{
    return _connected.load();
}

bool BaseTradeHandler::is_running() const
{
    return _running.load();
}

bool BaseTradeHandler::is_initialized() const
{
    return _initialized.load();
}

Status BaseTradeHandler::get_status()
{
    return _current_status.load();
}

std::string BaseTradeHandler::get_last_error() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_error;
}

int BaseTradeHandler::start()
{
    if (!_initialized.load())
    {
        set_error("Parser not initialized");
        return -1;
    }

    if (_running.load())
    {
        set_error("Parser already running");
        return -1;
    }

    if (start_impl() != 0)
    {
        return -1;
    }

    _current_status.store(STATUS_CONNECTING);
    return 0;
}

int BaseTradeHandler::connect()
{
    if (connect_impl() != 0)
    {
        set_error("Failed to connect handler");
        return -1;
    }

    _running.store(true);
    _connected.store(true);
    _current_status.store(STATUS_CONNECTED);
    return 0;
}

int BaseTradeHandler::disconnect()
{
    if (disconnect_impl() != 0)
    {
        set_error("Failed to disconnect handler");
        return -1;
    }

    _connected.store(false);
    _current_status.store(STATUS_DISCONNECTED);
    return 0;
}

int BaseTradeHandler::stop()
{
    if (!_running.load())
    {
        return 0; // Already stopped
    }

    if (stop_impl() != 0)
    {
        set_error("Failed to stop handler");
        return -1;
    }

    _running.store(false);
    _connected.store(false);
    _current_status.store(STATUS_DISCONNECTED);
    return 0;
}

// ===== 性能统计实现 =====
void BaseTradeHandler::get_order_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (avg_latency_us)
    {
        uint64_t total_orders = _total_orders_responded.load();
        *avg_latency_us = total_orders > 0 ? static_cast<double>(_total_order_latency_us) / total_orders : 0.0;
    }
    if (max_latency_us)
        *max_latency_us = static_cast<double>(_max_order_latency_us);
    if (min_latency_us)
        *min_latency_us = _min_order_latency_us == UINT64_MAX ? 0.0 : static_cast<double>(_min_order_latency_us);
}

void BaseTradeHandler::get_cancel_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (avg_latency_us)
    {
        uint64_t total_cancels = _total_cancels_responded.load();
        *avg_latency_us = total_cancels > 0 ? static_cast<double>(_total_cancel_latency_us) / total_cancels : 0.0;
    }
    if (max_latency_us)
        *max_latency_us = static_cast<double>(_max_cancel_latency_us);
    if (min_latency_us)
        *min_latency_us = _min_cancel_latency_us == UINT64_MAX ? 0.0 : static_cast<double>(_min_cancel_latency_us);
}

void BaseTradeHandler::reset_performance_stats()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _total_orders_sent.store(0);
    _total_orders_responded.store(0);
    _total_cancels_sent.store(0);
    _total_cancels_responded.store(0);
    _total_order_latency_us = 0;
    _total_cancel_latency_us = 0;
    _max_order_latency_us = 0;
    _max_cancel_latency_us = 0;
    _min_order_latency_us = UINT64_MAX;
    _min_cancel_latency_us = UINT64_MAX;
    _last_performance_report = std::chrono::steady_clock::now();
    _pending_order_times.clear();
    _pending_cancel_times.clear();
}

int BaseTradeHandler::get_account_info_by_id(const char *login_id, AccountInfo *account)
{
    if (!login_id || !account)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _accounts.find(login_id);
    if (it == _accounts.end())
    {
        return -1;
    }

    *account = it->second;
    return 0;
}

std::vector<std::string> BaseTradeHandler::get_all_login_ids() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    std::vector<std::string> account_ids;
    account_ids.reserve(_accounts.size());

    for (const auto &pair : _accounts)
    {
        account_ids.push_back(pair.first);
    }

    return account_ids;
}

int BaseTradeHandler::get_account_count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<int>(_accounts.size());
}

bool BaseTradeHandler::has_account(const char *login_id) const
{
    if (!login_id)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    return _accounts.find(login_id) != _accounts.end();
}

// ===== 回调函数注册 =====

TradeOrderCallbackHandle BaseTradeHandler::set_order_rtn_callback(TradeOrderCallbackType callback)
{
    if (!callback)
    {
        return TradeOrderCallbackHandle{}; // 返回无效句柄
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _order_rtn_callbacks.append([callback](const TradeOrderData *data)
                                           {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in order callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in order callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register order callback: {}", e.what());
        return TradeOrderCallbackHandle{}; // 返回无效句柄
    }
}

int BaseTradeHandler::remove_order_rtn_callback(TradeOrderCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _order_rtn_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove order callback: {}", e.what());
        return -1;
    }
}

TradeOrderQueryCallbackHandle BaseTradeHandler::set_order_query_callback(TradeOrderQueryCallbackType callback)
{
    if (!callback)
    {
        return TradeOrderQueryCallbackHandle{}; // 返回无效句柄
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _order_query_callbacks.append([callback](const TradeOrderData *data)
                                             {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in order query callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in order query callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register order query callback: {}", e.what());
        return TradeOrderCallbackHandle{}; // 返回无效句柄
    }
}

int BaseTradeHandler::remove_order_query_callback(TradeOrderQueryCallbackHandle handle)
{
    if (!handle)
    { // 利用operator bool()检查有效性
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _order_query_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove order query callback: {}", e.what());
        return -1;
    }
}

TradeFillCallbackHandle BaseTradeHandler::set_fill_callback(TradeFillCallbackType callback)
{
    if (!callback)
    {
        return TradeFillCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _fill_callbacks.append([callback](const TradeFillData *data)
                                      {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in fill callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in fill callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register fill callback: {}", e.what());
        return TradeFillCallbackHandle{};
    }
}

int BaseTradeHandler::remove_fill_callback(TradeFillCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _fill_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove fill callback: {}", e.what());
        return -1;
    }
}

PositionCallbackHandle BaseTradeHandler::set_position_callback(PositionCallbackType callback)
{
    if (!callback)
    {
        return PositionCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _position_callbacks.append([callback](const PositionData *data)
                                          {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in position callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in position callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register position callback: {}", e.what());
        return PositionCallbackHandle{};
    }
}

int BaseTradeHandler::remove_position_callback(PositionCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _position_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove position callback: {}", e.what());
        return -1;
    }
}

AccountCallbackHandle BaseTradeHandler::set_account_callback(AccountCallbackType callback)
{
    if (!callback)
    {
        return AccountCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _account_callbacks.append([callback](const AccountData *data)
                                         {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in account callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in account callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register account callback: {}", e.what());
        return AccountCallbackHandle{};
    }
}

int BaseTradeHandler::remove_account_callback(AccountCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _account_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove account callback: {}", e.what());
        return -1;
    }
}

TradeErrorCallbackHandle BaseTradeHandler::set_error_callback(TradeErrorCallbackType callback)
{
    if (!callback)
    {
        return TradeErrorCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _error_callbacks.append([callback](int32_t error_code, const char *error_msg, int64_t request_id)
                                       {
            try {
                callback(error_code, error_msg, request_id);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in error callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in error callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register error callback: {}", e.what());
        return TradeErrorCallbackHandle{};
    }
}

int BaseTradeHandler::remove_error_callback(TradeErrorCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _error_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove error callback: {}", e.what());
        return -1;
    }
}

BankSecuritiesTransferCallbackHandle BaseTradeHandler::set_bank_securities_transfer_callback(BankSecuritiesTransferCallbackType callback)
{
    if (!callback)
    {
        return BankSecuritiesTransferCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _bank_securities_transfer_callbacks.append([callback](const BankSecuritiesTransferData *data)
                                                          {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in bank securities transfer callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in bank securities transfer callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register bank securities transfer callback: {}", e.what());
        return BankSecuritiesTransferCallbackHandle{};
    }
}

int BaseTradeHandler::remove_bank_securities_transfer_callback(BankSecuritiesTransferCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _bank_securities_transfer_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove bank securities transfer callback: {}", e.what());
        return -1;
    }
}

FundTransferCallbackHandle BaseTradeHandler::set_fund_transfer_callback(FundTransferCallbackType callback)
{
    if (!callback)
    {
        return FundTransferCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _fund_transfer_callbacks.append([callback](const FundTransferData *data)
                                               {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in fund transfer callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in fund transfer callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register fund transfer callback: {}", e.what());
        return FundTransferCallbackHandle{};
    }
}

int BaseTradeHandler::remove_fund_transfer_callback(FundTransferCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _fund_transfer_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove fund transfer callback: {}", e.what());
        return -1;
    }
}

TradeOrderRspCallbackHandle BaseTradeHandler::set_order_rsp_callback(TradeOrderRspCallbackType callback)
{
    if (!callback)
    {
        return TradeOrderRspCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _order_rsp_callbacks.append([callback](const TradeOrderRspData *data)
                                           {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in order rsp callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in order rsp callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register order rsp callback: {}", e.what());
        return TradeOrderRspCallbackHandle{};
    }
}

int BaseTradeHandler::remove_order_rsp_callback(TradeOrderRspCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _order_rsp_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove order rsp callback: {}", e.what());
        return -1;
    }
}

TradeOrderCancelRspCallbackHandle BaseTradeHandler::set_cancel_rsp_callback(TradeOrderCancelRspCallbackType callback)
{
    if (!callback)
    {
        return TradeOrderCancelRspCallbackHandle{};
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        return _cancel_rsp_callbacks.append([callback](const TradeOrderCancelRsqData *data)
                                            {
            try {
                callback(data);
            } catch (const std::exception& e) {
                TRADE_LOG_ERROR("Exception in cancel rsp callback: {}", e.what());
            } catch (...) {
                TRADE_LOG_ERROR("Unknown exception in cancel rsp callback");
            } });
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to register cancel rsp callback: {}", e.what());
        return TradeOrderCancelRspCallbackHandle{};
    }
}

int BaseTradeHandler::remove_cancel_rsp_callback(TradeOrderCancelRspCallbackHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    try
    {
        bool success = _cancel_rsp_callbacks.remove(handle);
        return success ? 0 : -1;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Failed to remove cancel rsp callback: {}", e.what());
        return -1;
    }
}

// ===== 账户管理接口 =====
int BaseTradeHandler::_add_account(const AccountInfo &account)
{
    TRADE_LOG_INFO("add_account called for login_id: {}", account.login_id);

    if (strlen(account.login_id) == 0)
    {
        TRADE_LOG_ERROR("Invalid login_id - empty string");
        set_error("Invalid login_id");
        return -1;
    }

    // 检查账户是否已存在
    if (_accounts.find(account.login_id) != _accounts.end())
    {
        TRADE_LOG_ERROR("Account already exists: {}", account.login_id);
        set_error(std::string("Account already exists: ") + account.login_id);
        return -1;
    }

    _accounts[account.login_id] = account;

    // 初始化该账户的沪深两市资金数据
    // 上海市场账户
    if (strlen(account.account_id_sh) > 0)
    {
        std::string account_key_sh = _generate_account_key(account.login_id, account.fund_account_id, account.account_id_sh);

        AccountData account_data_sh;
        memset(&account_data_sh, 0, sizeof(account_data_sh));
        account_data_sh.account = account;

        // 设置当前数据对应的具体证券账户ID和市场
        strncpy(account_data_sh.current_account_id, account.account_id_sh, sizeof(account_data_sh.current_account_id) - 1);
        account_data_sh.current_market = MARKET_SSE;

        _latest_account_data[account_key_sh] = account_data_sh;
        TRADE_LOG_INFO("Initialized SH account data: {}", account_key_sh);
    }

    // 深圳市场账户
    if (strlen(account.account_id_sz) > 0)
    {
        std::string account_key_sz = _generate_account_key(account.login_id, account.fund_account_id, account.account_id_sz);

        AccountData account_data_sz;
        memset(&account_data_sz, 0, sizeof(account_data_sz));
        account_data_sz.account = account;

        // 设置当前数据对应的具体证券账户ID和市场
        strncpy(account_data_sz.current_account_id, account.account_id_sz, sizeof(account_data_sz.current_account_id) - 1);
        account_data_sz.current_market = MARKET_SZSE;

        _latest_account_data[account_key_sz] = account_data_sz;
        TRADE_LOG_INFO("Initialized SZ account data: {}", account_key_sz);
    }

    TRADE_LOG_INFO("Added account: {}, total accounts: {}", account.login_id, _accounts.size());
    return 0;
}

// ===== 数据获取接口 =====
int BaseTradeHandler::get_order_rtn_data(TradeOrderData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        // 回调模式下从内存缓存获取
        return _get_data_from_memory(DATA_TYPE_TRADE_ORDER_RTN, data, sizeof(TradeOrderData));
    }
    else
    {
        // 轮询模式下从数据管理器获取
        return _get_data_from_manager(DATA_TYPE_TRADE_ORDER_RTN, data, sizeof(TradeOrderData));
    }
}

int BaseTradeHandler::get_order_query_data(TradeOrderData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        // 回调模式下从内存缓存获取
        return _get_data_from_memory(DATA_TYPE_TRADE_ORDER_QUERY, data, sizeof(TradeOrderData));
    }
    else
    {
        // 轮询模式下从数据管理器获取
        return _get_data_from_manager(DATA_TYPE_TRADE_ORDER_QUERY, data, sizeof(TradeOrderData));
    }
}

int BaseTradeHandler::get_fill_data(TradeFillData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_TRADE_FILL, data, sizeof(TradeFillData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_TRADE_FILL, data, sizeof(TradeFillData));
    }
}

int BaseTradeHandler::get_position_data(PositionData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_TRADE_POSITION, data, sizeof(PositionData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_TRADE_POSITION, data, sizeof(PositionData));
    }
}

int BaseTradeHandler::get_account_data_by_id(const char *login_id, const char *fund_account_id, const char *account_id, AccountData *data)
{
    if (!login_id || !fund_account_id || !account_id || !data)
    {
        return -1;
    }

    // 从内存获取指定账户的数据
    std::lock_guard<std::mutex> lock(_mutex);
    std::string account_key = _generate_account_key(login_id, fund_account_id, account_id);

    auto it = _latest_account_data.find(account_key);
    if (it != _latest_account_data.end())
    {
        *data = it->second;

        // 确保current_account_id和current_market字段正确设置
        strncpy(data->current_account_id, account_id, sizeof(data->current_account_id) - 1);

        // 根据account_id判断市场类型
        for (const auto &account_pair : _accounts)
        {
            const AccountInfo &account = account_pair.second;
            if (strcmp(account.login_id, login_id) == 0 && strcmp(account.fund_account_id, fund_account_id) == 0)
            {
                if (strcmp(account.account_id_sh, account_id) == 0)
                {
                    data->current_market = MARKET_SSE;
                }
                else if (strcmp(account.account_id_sz, account_id) == 0)
                {
                    data->current_market = MARKET_SZSE;
                }
                else
                {
                    data->current_market = MARKET_UNKNOWN;
                }
                break;
            }
        }

        return 1;
    }

    return 0; // 未找到数据
}

int BaseTradeHandler::get_all_account_data_by_fund_id(const char *login_id, const char *fund_account_id, AccountData *sh_data, AccountData *sz_data)
{
    if (!login_id || !fund_account_id)
    {
        return -1;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    bool found_sh = false, found_sz = false;

    // 查找对应的账户信息
    std::string account_key_prefix = std::string(login_id) + "|" + fund_account_id;
    auto account_it = _accounts.find(account_key_prefix);

    if (account_it != _accounts.end())
    {
        const AccountInfo &account = account_it->second;

        // 查询上海市场数据
        if (sh_data && strlen(account.account_id_sh) > 0)
        {
            std::string sh_key = _generate_account_key(login_id, fund_account_id, account.account_id_sh);
            auto sh_it = _latest_account_data.find(sh_key);
            if (sh_it != _latest_account_data.end())
            {
                *sh_data = sh_it->second;
                found_sh = true;
            }
        }

        // 查询深圳市场数据
        if (sz_data && strlen(account.account_id_sz) > 0)
        {
            std::string sz_key = _generate_account_key(login_id, fund_account_id, account.account_id_sz);
            auto sz_it = _latest_account_data.find(sz_key);
            if (sz_it != _latest_account_data.end())
            {
                *sz_data = sz_it->second;
                found_sz = true;
            }
        }
    }

    return (found_sh || found_sz) ? 1 : 0;
}

int BaseTradeHandler::get_order_rsp_data(TradeOrderRspData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_ORDER_RSP, data, sizeof(TradeOrderRspData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_ORDER_RSP, data, sizeof(TradeOrderRspData));
    }
}

int BaseTradeHandler::get_cancel_rsp_data(TradeOrderCancelRsqData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_CANCEL_RSP, data, sizeof(TradeOrderCancelRsqData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_CANCEL_RSP, data, sizeof(TradeOrderCancelRsqData));
    }
}

int BaseTradeHandler::get_bank_securities_transfer_data(BankSecuritiesTransferData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_BANK_SECURITIES_TRANSFER, data, sizeof(BankSecuritiesTransferData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_BANK_SECURITIES_TRANSFER, data, sizeof(BankSecuritiesTransferData));
    }
}

int BaseTradeHandler::get_fund_transfer_data(FundTransferData *data)
{
    if (!data)
    {
        return -1;
    }

    if (_callback_mode_enabled.load())
    {
        return _get_data_from_memory(DATA_TYPE_FUND_TRANSFER, data, sizeof(FundTransferData));
    }
    else
    {
        return _get_data_from_manager(DATA_TYPE_FUND_TRANSFER, data, sizeof(FundTransferData));
    }
}

void BaseTradeHandler::set_db_manager(DatabaseManager *db_manager)
{
    _db_manager = db_manager;
}
DatabaseManager *BaseTradeHandler::get_db_manager() const
{
    return _db_manager;
}

LoginMode BaseTradeHandler::get_login_mode() const
{
    return _login_mode.load();
}

void BaseTradeHandler::set_quote_parser(IQuoteParser *quote_parser)
{
    _quote_parser = quote_parser;
}

IQuoteParser *BaseTradeHandler::get_quote_parser() const
{
    return _quote_parser;
}

// ===== 基类实现的统一交易接口 =====
int64_t BaseTradeHandler::send_order(const TradeOrderRequest *order)
{
    if (!order)
    {
        set_error("Invalid order data");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(order->account.login_id))
    {
        set_error(std::string("Account ") + order->account.login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    // 输出详细的下单日志
    TRADE_LOG_INFO("Sending Order - request_id: {}, login_id: {}, fund_account: {}, "
                   "security_code: {}, market: {}, side: {}, order_type: {}, "
                   "price: {:.4f}, quantity: {}, batch_id: {}",
                   request_id,
                   order->account.login_id,
                   order->account.fund_account_id,
                   order->security_code,
                   static_cast<int>(order->market),
                   order->side == OrderSide::SIDE_BUY ? "BUY" : (order->side == OrderSide::SIDE_SELL ? "SELL" : "UNKNOWN"),
                   static_cast<int>(order->order_type),
                   order->price,
                   order->qty,
                   order->batch_id);

    // 先调用子类实现发送订单
    int result = send_order_impl(order, request_id);

    // 只有在成功发起请求后才记录性能统计和返回request_id
    if (result == 0)
    {
        if (_performance_enabled)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            // 使用request_id作为映射键记录发送时间
            _pending_order_times[request_id] = std::chrono::high_resolution_clock::now();
            _total_orders_sent.fetch_add(1);
        }
        return request_id; // 返回生成的request_id
    }
    else
    {
        return -1; // 发送失败
    }
}

int64_t BaseTradeHandler::cancel_order(const AccountInfo *account, const int64_t orig_order_id,
                                       uint64_t orig_batch_id, MarketType market)
{
    if (!account || !orig_order_id)
    {
        set_error("Invalid cancel order parameters");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    // 输出详细的撤单日志
    TRADE_LOG_INFO("Canceling Order - request_id: {}, login_id: {}, fund_account: {}, "
                   "orig_order_id: {}, orig_batch_id: {}, market: {}",
                   request_id,
                   account->login_id,
                   account->fund_account_id,
                   orig_order_id,
                   orig_batch_id,
                   static_cast<int>(market));

    // 先调用子类实现发送撤单
    int result = cancel_order_impl(account, orig_order_id, orig_batch_id, market, request_id);

    // 只有在成功发起请求后才记录性能统计和返回request_id
    if (result == 0)
    {
        if (_performance_enabled)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            // 使用request_id作为映射键记录发送时间
            _pending_cancel_times[request_id] = std::chrono::high_resolution_clock::now();
            _total_cancels_sent.fetch_add(1);
        }
        return request_id; // 返回生成的request_id
    }
    else
    {
        return -1; // 撤单失败
    }
}

int64_t BaseTradeHandler::query_orders(const AccountInfo *account, MarketType market, const char *security_code,
                                       const int64_t order_id, const QueryRequestParams *params)
{
    if (!account)
    {
        set_error("Invalid account");
        return -1;
    }

    if (market == MARKET_UNKNOWN)
    {
        set_error("Invalid market type");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = query_orders_impl(account, market, security_code, order_id, params, request_id);
    return result == 0 ? request_id : -1;
}

int64_t BaseTradeHandler::query_positions(const AccountInfo *account, MarketType market, const char *security_code, const QueryRequestParams *params)
{
    if (!account)
    {
        set_error("Invalid account");
        return -1;
    }

    if (market == MARKET_UNKNOWN)
    {
        set_error("Invalid market type");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = query_positions_impl(account, market, security_code, params, request_id);
    return result == 0 ? request_id : -1;
}

int64_t BaseTradeHandler::query_funds(const AccountInfo *account, MarketType market, const char *currency, const QueryRequestParams *params)
{
    if (!account)
    {
        set_error("Invalid account");
        return -1;
    }

    if (market == MARKET_UNKNOWN)
    {
        set_error("Invalid market type");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = query_funds_impl(account, market, currency, params, request_id);
    return result == 0 ? request_id : -1;
}

int64_t BaseTradeHandler::query_trades(const AccountInfo *account, MarketType market, const char *security_code,
                                       const int64_t order_id, const QueryRequestParams *params)
{
    if (!account)
    {
        set_error("Invalid account");
        return -1;
    }

    if (market == MARKET_UNKNOWN)
    {
        set_error("Invalid market type");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = query_trades_impl(account, market, security_code, order_id, params, request_id);
    return result == 0 ? request_id : -1;
}

int64_t BaseTradeHandler::bank_securities_transfer(const AccountInfo *account,
                                                   BankSecuritiesTransferDirection direction,
                                                   MarketType market,
                                                   double transfer_amount,
                                                   const char *currency,
                                                   const char *bank_password,
                                                   const char *fund_password,
                                                   const char *remark)
{
    if (!account)
    {
        set_error("Invalid account");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = bank_securities_transfer_impl(account, direction, market, transfer_amount,
                                               currency, bank_password, fund_password, remark, request_id);
    return result == 0 ? request_id : -1;
}

int64_t BaseTradeHandler::fund_transfer(const AccountInfo *account,
                                        const char *fund_out_account_id,
                                        MarketType fund_out_market,
                                        const char *fund_in_account_id,
                                        MarketType fund_in_market,
                                        double transfer_amount)
{
    if (!account || !fund_out_account_id || !fund_in_account_id)
    {
        set_error("Invalid parameters");
        return -1;
    }

    // 检查指定账户是否已登录
    if (!is_account_login(account->login_id))
    {
        set_error(std::string("Account ") + account->login_id + " not logged in");
        return -1;
    }

    // 生成唯一的request_id
    int64_t request_id = generate_request_id();
    if (request_id <= 0)
    {
        set_error("Failed to generate request_id");
        return -1;
    }

    int result = fund_transfer_impl(account, fund_out_account_id, fund_out_market,
                                    fund_in_account_id, fund_in_market, transfer_amount, request_id);
    return result == 0 ? request_id : -1;
}

void BaseTradeHandler::set_error(const std::string &error)
{
    _last_error = error;
    TRADE_LOG_ERROR("Trade Handler Error: {}", error);
}

void BaseTradeHandler::configure_performance_stats(const PerformanceConfig &config)
{
    _performance_enabled = config.enabled;
    _latency_stats_enabled = config.enable_latency_stats;
    _report_interval_ms = config.report_interval_ms;

    TRADE_LOG_INFO("Performance stats configured - enabled: {}, interval: {}ms",
                   _performance_enabled, _report_interval_ms);
}

// ===== 多账户辅助方法 =====
AccountInfo *BaseTradeHandler::get_account_by_login_id_internal(const char *login_id)
{
    if (!login_id)
    {
        return nullptr;
    }

    auto it = _accounts.find(login_id);
    return (it != _accounts.end()) ? &it->second : nullptr;
}

void BaseTradeHandler::update_account_data(const char *login_id, const char *fund_account_id, const char *account_id, const AccountData &data)
{
    if (!login_id || !fund_account_id || !account_id)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    std::string account_key = _generate_account_key(login_id, fund_account_id, account_id);
    _latest_account_data[account_key] = data;
}

// ===== 数据推送接口的基类实现 =====
void BaseTradeHandler::on_order_update(const TradeOrderData &data)
{
    if (_data_manager)
    {
        // 数据管理器模式：直接写入数据管理器
        if (!write_to_data_manager(DATA_TYPE_TRADE_ORDER_RTN, &data, sizeof(TradeOrderData)))
        {
            TRADE_LOG_WARN("Failed to write order data to data manager");
        }
    }
    else
    {
        // 内存缓存模式：只更新内存缓存
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_order_rtn_data = data;
    }

    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            _order_rtn_sync_mgr.set_success(data.request_id, data);
        }
        else
        {
            _order_rtn_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 使用eventpp回调列表
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _order_rtn_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in order callbacks: {}", e.what());
        }
        catch (...)
        {
            TRADE_LOG_ERROR("Unknown exception in order callbacks");
        }
    }
}

void BaseTradeHandler::on_order_query_result(const TradeOrderData &data)
{
    // 数据管理器处理
    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_TRADE_ORDER_QUERY, &data, sizeof(TradeOrderData)))
        {
            TRADE_LOG_WARN("Failed to write order query data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_order_query_data = data;
    }

    // 使用数组管理器处理同步调用
    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _order_query_accumulators[data.request_id].push_back(data);

            if (data.is_last)
            {
                auto &accumulated_data = _order_query_accumulators[data.request_id];
                _order_query_sync_mgr.set_success(data.request_id, accumulated_data);

                // 清理累积器
                _order_query_accumulators.erase(data.request_id);
            }
        }
        else
        {
            _order_query_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 回调处理
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _order_query_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in order query callbacks: {}", e.what());
        }
    }
}

void BaseTradeHandler::on_fill_query_result(const TradeFillData &data)
{
    // 数据管理器处理
    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_TRADE_FILL, &data, sizeof(TradeFillData)))
        {
            TRADE_LOG_WARN("Failed to write fill data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_fill_data = data;
    }

    // 使用数组管理器处理同步调用
    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _trade_query_accumulators[data.request_id].push_back(data);

            if (data.is_last)
            {
                auto &accumulated_data = _trade_query_accumulators[data.request_id];
                _trade_query_sync_mgr.set_success(data.request_id, accumulated_data);

                // 清理累积器
                _trade_query_accumulators.erase(data.request_id);
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _trade_query_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 回调处理
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _fill_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in fill callbacks: {}", e.what());
        }
    }
}

void BaseTradeHandler::on_position_query_result(const PositionData &data)
{
    // 数据管理器处理
    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_TRADE_POSITION, &data, sizeof(PositionData)))
        {
            TRADE_LOG_WARN("Failed to write position data to data manager");
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_position_data = data;
    }

    // 使用数组管理器处理同步调用
    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _position_query_accumulators[data.request_id].push_back(data);

            // 如果是最后一项，完成同步调用
            if (data.is_last)
            {
                auto &accumulated_data = _position_query_accumulators[data.request_id];
                _position_query_sync_mgr.set_success(data.request_id, accumulated_data);

                // 清理累积器
                _position_query_accumulators.erase(data.request_id);
            }
        }
        else
        {
            _position_query_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 回调处理
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _position_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in position callbacks: {}", e.what());
        }
    }
}

void BaseTradeHandler::on_account_query_result(const AccountData &data, const char *account_id)
{
    // 数据管理器处理
    if (_data_manager)
    {
        if (!write_to_data_manager(DATA_TYPE_TRADE_ACCOUNT, &data, sizeof(AccountData)))
        {
            TRADE_LOG_WARN("Failed to write account data to data manager");
        }
    }

    // 使用数组管理器处理同步调用
    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _account_query_accumulators[data.request_id].push_back(data);

            if (data.is_last)
            {
                auto &accumulated_data = _account_query_accumulators[data.request_id];
                _account_query_sync_mgr.set_success(data.request_id, accumulated_data);
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _account_query_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 更新账户数据缓存
    const char *actual_account_id = account_id ? account_id : data.current_account_id;
    if (actual_account_id && strlen(actual_account_id) > 0)
    {
        update_account_data(data.account.login_id, data.account.fund_account_id, actual_account_id, data);
    }
    else
    {
        TRADE_LOG_WARN("Missing account_id in account data update");
    }

    // 回调处理
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _account_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in account callbacks: {}", e.what());
        }
    }
}

void BaseTradeHandler::on_error_received(int32_t error_code, const char *error_msg, int64_t request_id)
{
    TRADE_LOG_ERROR("Trade error received - code: {}, msg: {}, request_id: {}",
                    error_code, error_msg ? error_msg : "N/A", request_id);

    if (request_id > 0)
    {
        // 非查询操作的同步管理器
        _order_rsp_sync_mgr.set_error(request_id, error_code, error_msg);
        _cancel_rsp_sync_mgr.set_error(request_id, error_code, error_msg);
        _order_rtn_sync_mgr.set_error(request_id, error_code, error_msg);
        _bank_transfer_sync_mgr.set_error(request_id, error_code, error_msg);
        _fund_transfer_sync_mgr.set_error(request_id, error_code, error_msg);

        // 查询操作的数组管理器
        _order_query_sync_mgr.set_error(request_id, error_code, error_msg);
        _position_query_sync_mgr.set_error(request_id, error_code, error_msg);
        _account_query_sync_mgr.set_error(request_id, error_code, error_msg);
        _trade_query_sync_mgr.set_error(request_id, error_code, error_msg);
    }

    // 回调处理
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _error_callbacks(error_code, error_msg, request_id);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in error callbacks: {}", e.what());
        }
    }
}

void BaseTradeHandler::on_status_changed(Status status, const char *message)
{
    _current_status.store(status);

    // 更新连接和登录状态
    switch (status)
    {
    case STATUS_CONNECTED:
        _connected.store(true);
        break;
    case STATUS_DISCONNECTED:
        _connected.store(false);
        break;
    case STATUS_LOGIN_SUCCESS:
        break;
    case STATUS_LOGIN_FAILED:
        break;
    default:
        break;
    }

    TRADE_LOG_INFO("Trade handler status changed to: {}, message: {}",
                   static_cast<int>(status), message ? message : "N/A");
}

void BaseTradeHandler::on_bank_securities_transfer_update(const BankSecuritiesTransferData &data)
{
    if (_data_manager)
    {
        // 数据管理器模式：直接写入数据管理器
        if (!write_to_data_manager(DATA_TYPE_BANK_SECURITIES_TRANSFER, &data, sizeof(BankSecuritiesTransferData)))
        {
            TRADE_LOG_WARN("Failed to write bank securities transfer data to data manager");
        }
    }
    else
    {
        // 内存缓存模式：只更新内存缓存
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_bank_securities_transfer_data = data;
    }

    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            _bank_transfer_sync_mgr.set_success(data.request_id, data);
        }
        else
        {
            _bank_transfer_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 使用eventpp回调列表
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _bank_securities_transfer_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in bank securities transfer callbacks: {}", e.what());
        }
        catch (...)
        {
            TRADE_LOG_ERROR("Unknown exception in bank securities transfer callbacks");
        }
    }
}

void BaseTradeHandler::on_fund_transfer_update(const FundTransferData &data)
{
    if (_data_manager)
    {
        // 数据管理器模式：直接写入数据管理器
        if (!write_to_data_manager(DATA_TYPE_FUND_TRANSFER, &data, sizeof(FundTransferData)))
        {
            TRADE_LOG_WARN("Failed to write fund transfer data to data manager");
        }
    }
    else
    {
        // 内存缓存模式：只更新内存缓存
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_fund_transfer_data = data;
    }

    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            _fund_transfer_sync_mgr.set_success(data.request_id, data);
        }
        else
        {
            _fund_transfer_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 使用eventpp回调列表
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _fund_transfer_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in fund transfer callbacks: {}", e.what());
        }
        catch (...)
        {
            TRADE_LOG_ERROR("Unknown exception in fund transfer callbacks");
        }
    }
}

void BaseTradeHandler::on_order_rsp_update(const TradeOrderRspData &data)
{
    // 计算订单延迟
    if (_performance_enabled)
    {
        _update_order_latency_stats(data.request_id);
    }

    if (_data_manager)
    {
        // 数据管理器模式：直接写入数据管理器
        if (!write_to_data_manager(DATA_TYPE_ORDER_RSP, &data, sizeof(TradeOrderRspData)))
        {
            TRADE_LOG_WARN("Failed to write order response data to data manager");
        }
    }
    else
    {
        // 内存缓存模式：只更新内存缓存
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_order_rsp_data = data;
    }

    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            _order_rsp_sync_mgr.set_success(data.request_id, data);
        }
        else
        {
            _order_rsp_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 使用eventpp回调列表
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _order_rsp_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in order rsp callbacks: {}", e.what());
        }
        catch (...)
        {
            TRADE_LOG_ERROR("Unknown exception in order rsp callbacks");
        }
    }
}

void BaseTradeHandler::on_cancel_rsp_update(const TradeOrderCancelRsqData &data)
{
    // 计算撤单延迟
    if (_performance_enabled)
    {
        _update_cancel_latency_stats(data.request_id);
    }

    if (_data_manager)
    {
        // 数据管理器模式：直接写入数据管理器
        if (!write_to_data_manager(DATA_TYPE_CANCEL_RSP, &data, sizeof(TradeOrderCancelRsqData)))
        {
            TRADE_LOG_WARN("Failed to write cancel response data to data manager");
        }
    }
    else
    {
        // 内存缓存模式：只更新内存缓存
        std::lock_guard<std::mutex> lock(_mutex);
        _latest_cancel_rsp_data = data;
    }

    if (data.request_id > 0)
    {
        if (data.error_code == 0)
        {
            // 成功响应
            _cancel_rsp_sync_mgr.set_success(data.request_id, data);
        }
        else
        {
            // 错误响应
            _cancel_rsp_sync_mgr.set_error(data.request_id, data.error_code, data.error_msg);
        }
    }

    // 使用eventpp回调列表
    if (_callback_mode_enabled.load())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        try
        {
            _cancel_rsp_callbacks(&data);
        }
        catch (const std::exception &e)
        {
            TRADE_LOG_ERROR("Exception in cancel rsp callbacks: {}", e.what());
        }
        catch (...)
        {
            TRADE_LOG_ERROR("Unknown exception in cancel rsp callbacks");
        }
    }
}

void BaseTradeHandler::_update_order_latency_stats(int64_t request_id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (request_id == -1)
    {
        return;
    }

    auto it = _pending_order_times.find(request_id);
    if (it != _pending_order_times.end())
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - it->second).count();
        uint64_t latency_us = latency_ns / 1000;

        _total_order_latency_us += latency_us;
        _total_orders_responded.fetch_add(1);

        if (latency_us > _max_order_latency_us)
        {
            _max_order_latency_us = latency_us;
        }
        if (latency_us < _min_order_latency_us)
        {
            _min_order_latency_us = latency_us;
        }

        // 清理映射关系
        _pending_order_times.erase(it);
    }
}

void BaseTradeHandler::_update_cancel_latency_stats(int64_t request_id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (request_id == -1)
    {
        return;
    }

    auto it = _pending_cancel_times.find(request_id);
    if (it != _pending_cancel_times.end())
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - it->second).count();
        uint64_t latency_us = latency_ns / 1000;

        _total_cancel_latency_us += latency_us;
        _total_cancels_responded.fetch_add(1);

        if (latency_us > _max_cancel_latency_us)
        {
            _max_cancel_latency_us = latency_us;
        }
        if (latency_us < _min_cancel_latency_us)
        {
            _min_cancel_latency_us = latency_us;
        }

        // 清理映射关系
        _pending_cancel_times.erase(it);
    }
}

// ===== 数据落地方法 =====
bool BaseTradeHandler::write_to_data_manager(FTDataType data_type, const void *data, size_t size)
{
    if (!_data_manager)
    {
        return false;
    }

    try
    {
        ssize_t result = _data_manager->write(data_type, data, size);
        return result > 0;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception writing to data manager: {}", e.what());
        return false;
    }
}

// ===== 私有方法 =====
bool BaseTradeHandler::_initialize_data_manager(const DataConfig &config)
{
    try
    {
        _data_manager = std::make_unique<DataManager>("trade_handler");

        // 定义交易相关的数据类型
        std::vector<FTDataType> trade_data_types = {
            DATA_TYPE_TRADE_ORDER_RTN,
            DATA_TYPE_TRADE_FILL,
            DATA_TYPE_TRADE_POSITION,
            DATA_TYPE_TRADE_ACCOUNT,
            DATA_TYPE_BANK_SECURITIES_TRANSFER,
            DATA_TYPE_FUND_TRANSFER,
            DATA_TYPE_ORDER_RSP,
            DATA_TYPE_CANCEL_RSP};

        // 注册数据源，每种类型分配足够的空间
        size_t size_per_type = 64 * 1024 * 1024; // 64MB per type
        return _data_manager->register_data_source(config, trade_data_types, size_per_type);
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception initializing data manager: {}", e.what());
        return false;
    }
}

void BaseTradeHandler::_start_performance_reporting()
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
        TRADE_LOG_INFO("Performance reporting thread started, interval: {}ms", _report_interval_ms);
        
        // 同时检查性能开关和服务运行状态
        while (_performance_enabled && _running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(_report_interval_ms));
            
            // 双重检查：性能开关和服务状态
            if (!_performance_enabled || !_running.load()) break;
            
            _report_performance_stats();
        }
        
        TRADE_LOG_INFO("Performance reporting thread stopped"); });
}

void BaseTradeHandler::_report_performance_stats()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_performance_report);
    double seconds = duration.count() / 1000.0;

    _last_performance_report = now;

    uint64_t total_orders_sent = _total_orders_sent.load();
    uint64_t total_orders_responded = _total_orders_responded.load();
    uint64_t total_cancels_sent = _total_cancels_sent.load();
    uint64_t total_cancels_responded = _total_cancels_responded.load();

    // 计算订单统计
    double avg_order_latency_us, max_order_latency_us, min_order_latency_us;
    get_order_latency_stats(&avg_order_latency_us, &max_order_latency_us, &min_order_latency_us);

    // 计算撤单统计
    double avg_cancel_latency_us, max_cancel_latency_us, min_cancel_latency_us;
    get_cancel_latency_stats(&avg_cancel_latency_us, &max_cancel_latency_us, &min_cancel_latency_us);

    // 计算响应率
    double order_response_rate = total_orders_sent > 0 ? static_cast<double>(total_orders_responded) / total_orders_sent * 100.0 : 0.0;
    double cancel_response_rate = total_cancels_sent > 0 ? static_cast<double>(total_cancels_responded) / total_cancels_sent * 100.0 : 0.0;

    TRADE_LOG_INFO("=== Trading Performance Report (Period: {:.1f}s) ===", seconds);

    // 订单性能统计
    if (total_orders_sent > 0)
    {
        TRADE_LOG_INFO("Orders - Sent: {}, Responded: {}, Response Rate: {:.1f}%",
                       total_orders_sent, total_orders_responded, order_response_rate);

        if (total_orders_responded > 0)
        {
            TRADE_LOG_INFO("Order Latency - Avg: {:.2f}μs, Max: {:.2f}μs, Min: {:.2f}μs",
                           avg_order_latency_us, max_order_latency_us, min_order_latency_us);

            // 根据延迟大小选择合适的单位显示
            if (avg_order_latency_us < 1000.0)
            {
                TRADE_LOG_INFO("Average order response time: {:.2f} microseconds", avg_order_latency_us);
            }
            else
            {
                TRADE_LOG_INFO("Average order response time: {:.2f} milliseconds", avg_order_latency_us / 1000.0);
            }
        }
    }

    // 撤单性能统计
    if (total_cancels_sent > 0)
    {
        TRADE_LOG_INFO("Cancels - Sent: {}, Responded: {}, Response Rate: {:.1f}%",
                       total_cancels_sent, total_cancels_responded, cancel_response_rate);

        if (total_cancels_responded > 0)
        {
            TRADE_LOG_INFO("Cancel Latency - Avg: {:.2f}μs, Max: {:.2f}μs, Min: {:.2f}μs",
                           avg_cancel_latency_us, max_cancel_latency_us, min_cancel_latency_us);

            // 根据延迟大小选择合适的单位显示
            if (avg_cancel_latency_us < 1000.0)
            {
                TRADE_LOG_INFO("Average cancel response time: {:.2f} microseconds", avg_cancel_latency_us);
            }
            else
            {
                TRADE_LOG_INFO("Average cancel response time: {:.2f} milliseconds", avg_cancel_latency_us / 1000.0);
            }
        }
    }

    // 清理过期的待处理时间记录（超过10秒的认为丢失）
    _cleanup_expired_pending_times();
}

void BaseTradeHandler::_cleanup_expired_pending_times()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto timeout = std::chrono::seconds(10); // 10秒超时

    // 清理过期的订单时间记录
    for (auto it = _pending_order_times.begin(); it != _pending_order_times.end();)
    {
        if (now - it->second > timeout)
        {
            TRADE_LOG_WARN("Order request timeout, removing from pending list: order_id={}", it->first);
            it = _pending_order_times.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 清理过期的撤单时间记录
    for (auto it = _pending_cancel_times.begin(); it != _pending_cancel_times.end();)
    {
        if (now - it->second > timeout)
        {
            TRADE_LOG_WARN("Cancel request timeout, removing from pending list: order_id={}", it->first);
            it = _pending_cancel_times.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

int BaseTradeHandler::_get_data_from_manager(FTDataType data_type, void *data, size_t data_size)
{
    if (!_data_manager)
    {
        return -1;
    }

    try
    {
        ssize_t result = _data_manager->read(data_type, data, data_size);
        return result > 0 ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception reading from data manager: {}", e.what());
        return -1;
    }
}

int BaseTradeHandler::_get_data_from_memory(FTDataType data_type, void *data, size_t data_size)
{
    std::lock_guard<std::mutex> lock(_mutex);

    switch (data_type)
    {
    case DATA_TYPE_TRADE_ORDER_RTN:
        if (data_size >= sizeof(TradeOrderData))
        {
            memcpy(data, &_latest_order_rtn_data, sizeof(TradeOrderData));
            return _latest_order_rtn_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_TRADE_ORDER_QUERY:
        if (data_size >= sizeof(TradeOrderData))
        {
            memcpy(data, &_latest_order_query_data, sizeof(TradeOrderData));
            return _latest_order_query_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_TRADE_FILL:
        if (data_size >= sizeof(TradeFillData))
        {
            memcpy(data, &_latest_fill_data, sizeof(TradeFillData));
            return _latest_fill_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_TRADE_POSITION:
        if (data_size >= sizeof(PositionData))
        {
            memcpy(data, &_latest_position_data, sizeof(PositionData));
            return _latest_position_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_BANK_SECURITIES_TRANSFER:
        if (data_size >= sizeof(BankSecuritiesTransferData))
        {
            memcpy(data, &_latest_bank_securities_transfer_data, sizeof(BankSecuritiesTransferData));
            return _latest_bank_securities_transfer_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_FUND_TRANSFER:
        if (data_size >= sizeof(FundTransferData))
        {
            memcpy(data, &_latest_fund_transfer_data, sizeof(FundTransferData));
            return _latest_fund_transfer_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_ORDER_RSP:
        if (data_size >= sizeof(TradeOrderRspData))
        {
            memcpy(data, &_latest_order_rsp_data, sizeof(TradeOrderRspData));
            return _latest_order_rsp_data.timestamp > 0 ? 1 : 0;
        }
        break;
    case DATA_TYPE_CANCEL_RSP:
        if (data_size >= sizeof(TradeOrderCancelRsqData))
        {
            memcpy(data, &_latest_cancel_rsp_data, sizeof(TradeOrderCancelRsqData));
            return _latest_cancel_rsp_data.timestamp > 0 ? 1 : 0;
        }
        break;
    default:
        break;
    }

    return -1;
}

void BaseTradeHandler::_load_accounts_from_config(const TradeConfig *config)
{
    TRADE_LOG_INFO("Starting account loading from config...");

    if (!config)
    {
        TRADE_LOG_ERROR("Invalid config pointer");
        return;
    }

    if (!config->account_list.accounts || config->account_list.account_count <= 0)
    {
        TRADE_LOG_WARN("No accounts found in configuration");
        return;
    }

    TRADE_LOG_INFO("Found {} accounts in configuration", config->account_list.account_count);

    int loaded_count = 0;
    int failed_count = 0;

    for (int i = 0; i < config->account_list.account_count; ++i)
    {
        TRADE_LOG_INFO("Loading account {} of {}", i + 1, config->account_list.account_count);

        try
        {
            const AccountInfo &account = config->account_list.accounts[i];

            // 直接调用基类的add_account方法
            int result = _add_account(account);

            if (result == 0)
            {
                loaded_count++;
                TRADE_LOG_INFO("Loaded account from config: login_id={}, cust_id={}, fund_account_id={}, account_id_sh={}, account_id_sz={}",
                               account.login_id, account.cust_id, account.fund_account_id, account.account_id_sh, account.account_id_sz);
            }
            else
            {
                failed_count++;
                TRADE_LOG_ERROR("Failed to load account from config: login_id={} (result: {})",
                                account.login_id, result);
            }
        }
        catch (const std::exception &e)
        {
            failed_count++;
            TRADE_LOG_ERROR("Exception loading account {}: {}", i, e.what());
        }

        TRADE_LOG_INFO("Completed processing account {} of {}", i + 1, config->account_list.account_count);
    }

    TRADE_LOG_INFO("Account loading completed - loaded: {}, failed: {}, total: {}",
                   loaded_count, failed_count, config->account_list.account_count);
}

std::string BaseTradeHandler::_generate_account_key(const char *login_id, const char *fund_account_id, const char *account_id) const
{
    if (login_id && strlen(login_id) > 0 && fund_account_id && strlen(fund_account_id) > 0 && account_id && strlen(account_id) > 0)
    {
        return std::string(login_id) + "_" + std::string(fund_account_id) + "_" + std::string(account_id);
    }
    else
    {
        TRADE_LOG_WARN("Invalid parameters for account key generation");
        return std::string("unknown_account_key");
    }
}

// ===== 基类提供的辅助方法 =====

int64_t BaseTradeHandler::generate_request_id()
{
    int64_t sequence = _next_request_id.fetch_add(1);

    // 检查溢出并重置
    if (sequence >= INT64_MAX - 1000)
    { // 留一些缓冲
        TRADE_LOG_WARN("Request ID counter approaching overflow, resetting...");
        _next_request_id.store(1);
        sequence = 1;
    }

    return sequence;
}

bool BaseTradeHandler::is_account_login(const char *login_id) const
{
    if (!login_id)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    return _logged_in_accounts.find(login_id) != _logged_in_accounts.end();
}

std::vector<std::string> BaseTradeHandler::get_logged_in_login_ids() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    std::vector<std::string> login_ids;
    login_ids.reserve(_logged_in_accounts.size());

    for (const auto &login_id : _logged_in_accounts)
    {
        login_ids.push_back(login_id);
    }

    return login_ids;
}

int BaseTradeHandler::get_logged_in_account_count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<int>(_logged_in_accounts.size());
}

void BaseTradeHandler::set_account_login_status(const char *login_id, bool login_status)
{
    if (!login_id)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    if (login_status)
    {
        _logged_in_accounts.insert(login_id);
        TRADE_LOG_INFO("Account {} marked as logged in, total logged in accounts: {}",
                       login_id, _logged_in_accounts.size());
    }
    else
    {
        _logged_in_accounts.erase(login_id);
        TRADE_LOG_INFO("Account {} marked as logged out, total logged in accounts: {}",
                       login_id, _logged_in_accounts.size());
    }
}

void BaseTradeHandler::clear_all_login_status()
{
    std::lock_guard<std::mutex> lock(_mutex);

    size_t count = _logged_in_accounts.size();
    _logged_in_accounts.clear();

    TRADE_LOG_INFO("Cleared login status for {} accounts", count);
}

NS_FT_END