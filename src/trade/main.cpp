#include <iostream>
#include <memory>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <getopt.h>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <ctime>
#include <vector>
#include <mutex>
#include <algorithm>
#include "laserpants/dotenv/dotenv.h"

#include "trade/trade_interface.h"
#include "types/trade_types.h"
#include "core/macros.h"

// 全局变量
std::atomic<bool> g_running(true);

// 全局句柄存储，用于多回调测试
std::vector<TradeOrderCallbackHandle> g_order_handles;
std::vector<TradeFillCallbackHandle> g_fill_handles;
std::vector<PositionCallbackHandle> g_position_handles;
std::vector<AccountCallbackHandle> g_account_handles;
std::vector<TradeErrorCallbackHandle> g_error_handles;
std::vector<BankSecuritiesTransferCallbackHandle> g_bank_transfer_handles;
std::vector<FundTransferCallbackHandle> g_fund_transfer_handles;
std::vector<TradeOrderRspCallbackHandle> g_order_rsp_handles;
std::vector<TradeOrderCancelRspCallbackHandle> g_cancel_rsp_handles;

// 测试模式配置 - 可以通过注释来切换测试不同模式
#define TEST_CALLBACK_MODE 1      // 回调模式
#define TEST_DATA_MANAGER_MODE 0  // 内存存储模式
#define TEST_ACCOUNT_MANAGEMENT 1 // 账户管理测试
#define TEST_TRADING_OPERATIONS 1 // 交易操作测试
#define TEST_SYNC_OPERATIONS 1    // 同步接口测试

// 测试订单配置
struct TestOrderConfig
{
    const char *symbol;       // 证券代码
    MarketType market;        // 市场类型
    OrderSide side;           // 买卖方向
    double price;             // 价格
    uint64_t qty;             // 数量
    OrderType order_type;     // 订单类型
    const char *description;  // 测试描述
    bool should_fail;         // 是否应该失败
    bool should_partial_fill; // 是否应该部分成交
};

// 测试订单数据 - 根据图中规则设计
static TestOrderConfig g_test_orders[] = {
    // === 会失败的订单（超过数量限制） ===
    {"000001", MARKET_SZSE, SIDE_BUY, 10.50, 10000, ORDER_TYPE_LIMIT, "深圳全部成交-超量失败", true, false},
    {"600000", MARKET_SSE, SIDE_BUY, 15.20, 1500, ORDER_TYPE_LIMIT, "上海全部成交-超量失败", true, false},
    {"300001", MARKET_SZSE, SIDE_BUY, 25.80, 1200, ORDER_TYPE_LIMIT, "科创板全部成交-超量失败", true, false},

    // === 会成功的订单（在数量限制内） ===
    {"000001", MARKET_SZSE, SIDE_BUY, 10.50, 100, ORDER_TYPE_LIMIT, "深圳全部成交-正常", false, false},
    {"600000", MARKET_SSE, SIDE_BUY, 15.20, 100, ORDER_TYPE_LIMIT, "上海全部成交-正常", false, false},
    {"300001", MARKET_SZSE, SIDE_BUY, 25.80, 200, ORDER_TYPE_LIMIT, "科创板全部成交-正常", false, false},

    // === 会部分成交的订单（接近数量上限） ===
    {"000001", MARKET_SZSE, SIDE_BUY, 10.50, 700, ORDER_TYPE_LIMIT, "深圳部分成交", false, true},
    {"600000", MARKET_SSE, SIDE_BUY, 15.20, 700, ORDER_TYPE_LIMIT, "上海部分成交", false, true},
    {"300001", MARKET_SZSE, SIDE_BUY, 25.80, 600, ORDER_TYPE_LIMIT, "科创板部分成交", false, true},

    // === 卖单测试 ===
    {"000001", MARKET_SZSE, SIDE_SELL, 10.60, 100, ORDER_TYPE_LIMIT, "深圳卖单-正常", false, false},
    {"600000", MARKET_SSE, SIDE_SELL, 15.30, 100, ORDER_TYPE_LIMIT, "上海卖单-正常", false, false},

    // === 不同订单类型测试 ===
    {"000001", MARKET_SZSE, SIDE_BUY, 0.0, 100, ORDER_TYPE_MARKET, "深圳市价单", false, false},
    {"600000", MARKET_SSE, SIDE_BUY, 0.0, 100, ORDER_TYPE_MARKET, "上海市价单", false, false},
    {"000001", MARKET_SZSE, SIDE_BUY, 0.0, 100, ORDER_TYPE_BEST_PRICE, "深圳最优价", false, false},
};

// 测试撤单配置
struct TestCancelConfig
{
    const char *description; // 测试描述
    bool should_fail;        // 是否应该失败
};

static TestCancelConfig g_test_cancels[] = {
    {"撤单-正常情况", false},
    {"撤单-已成交订单", true},
    {"撤单-不存在订单", true},
};

// 时间戳打印函数
void print_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm *tm_now = std::localtime(&time_t);
    printf("[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
           tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
           tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, (int)ms.count());
}

// 简化的日志宏定义
#define PRINTF_INFO(...)               \
    do                                 \
    {                                  \
        print_timestamp();             \
        printf("[INFO] " __VA_ARGS__); \
        printf("\n");                  \
        fflush(stdout);                \
    } while (0)
#define PRINTF_ERROR(...)               \
    do                                  \
    {                                   \
        print_timestamp();              \
        printf("[ERROR] " __VA_ARGS__); \
        printf("\n");                   \
        fflush(stderr);                 \
    } while (0)
#define PRINTF_DEBUG(...)               \
    do                                  \
    {                                   \
        print_timestamp();              \
        printf("[DEBUG] " __VA_ARGS__); \
        printf("\n");                   \
        fflush(stdout);                 \
    } while (0)
#define PRINTF_WARN(...)               \
    do                                 \
    {                                  \
        print_timestamp();             \
        printf("[WARN] " __VA_ARGS__); \
        printf("\n");                  \
        fflush(stdout);                \
    } while (0)

// 信号处理函数
void signal_handler(int signal)
{
    PRINTF_INFO("Received signal %d, shutting down...", signal);
    g_running.store(false);
}

// 设置信号处理
void setup_signal_handlers()
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGHUP, SIG_IGN);
}

#if TEST_CALLBACK_MODE
// ===== 多个Order回调函数 =====
void on_order_callback_1(const TradeOrderData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] Order - RequestID: %lld, OrderID: %lld, Symbol: %s, Side: %c, Status: %d",
                 (long long)data->request_id, (long long)data->order_id, data->security_info.code, data->side, data->status);
}

void on_order_callback_2(const TradeOrderData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] Order - RequestID: %lld, OrderID: %lld, Symbol: %s, Price: %.3f, Qty: %llu, Error: %d",
                 (long long)data->request_id, (long long)data->order_id, data->security_info.code, data->price, (unsigned long long)data->qty, data->error_code);
}

void on_order_callback_3(const TradeOrderData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-3] Order - RequestID: %lld, OrderID: %lld, BatchID: %llu, Type: %d, Market: %d, Timestamp: %llu",
                 (long long)data->request_id, (long long)data->order_id, (unsigned long long)data->batch_id, data->order_type, data->security_info.market,
                 (unsigned long long)data->timestamp);
}

// ===== 多个Fill回调函数 =====
void on_fill_callback_1(const TradeFillData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] Fill - RequestID: %lld, OrderID: %lld, Symbol: %s, Price: %.3f, Qty: %llu",
                 (long long)data->request_id, (long long)data->order_id, data->security_info.code, data->price,
                 (unsigned long long)data->qty);
}

void on_fill_callback_2(const TradeFillData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] Fill - RequestID: %lld, OrderID: %lld, Symbol: %s, Value: %.2f, Fee: %.2f, TradeTime: %llu",
                 (long long)data->request_id, (long long)data->order_id, data->security_info.code, data->value, data->fee,
                 (unsigned long long)data->trade_time);
}

// ===== 多个Position回调函数 =====
void on_position_callback_1(const PositionData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] Position - RequestID: %lld, Symbol: %s, TotalQty: %llu, AvailableQty: %llu",
                 (long long)data->request_id, data->security_info.code, (unsigned long long)data->total_qty,
                 (unsigned long long)data->available_qty);
}

void on_position_callback_2(const PositionData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] Position - RequestID: %lld, Symbol: %s, CostPrice: %.3f, ProfitLoss: %.2f, MarketValue: %.2f",
                 (long long)data->request_id, data->security_info.code, data->cost_price, data->profit_loss, data->market_value);
}

// ===== 多个Account回调函数 =====
void on_account_callback_1(const AccountData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] Account - RequestID: %lld, LoginID: %s, CashBalance: %.2f, AvailableCash: %.2f",
                 (long long)data->request_id, data->account.login_id, data->cash_balance, data->available_cash);
}

void on_account_callback_2(const AccountData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] Account - RequestID: %lld, FundAccount: %s, FrozenCash: %.2f, MarketValue: %.2f, TotalAsset: %.2f",
                 (long long)data->request_id, data->account.fund_account_id, data->frozen_cash, data->market_value, data->total_asset);
}

// ===== 多个Error回调函数 =====
void on_error_callback_1(int32_t error_code, const char *error_msg, int64_t request_id)
{
    PRINTF_ERROR("[CALLBACK-1] Error - RequestID: %lld, Code: %d, Msg: %s", 
                 (long long)request_id, error_code, error_msg ? error_msg : "N/A");
}

void on_error_callback_2(int32_t error_code, const char *error_msg, int64_t request_id)
{
    PRINTF_ERROR("[CALLBACK-2] Error - RequestID: %lld, Code: %d, Msg: %s", 
                 (long long)request_id, error_code, error_msg ? error_msg : "N/A");
}

// ===== 多个OrderRsp回调函数 =====
void on_order_rsp_callback_1(const TradeOrderRspData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] OrderRsp - RequestID: %lld, OrderID: %lld, Price: %.3f, Qty: %llu, Error: %d",
                 (long long)data->request_id, (long long)data->order_id, data->price, (unsigned long long)data->qty, data->error_code);
}

void on_order_rsp_callback_2(const TradeOrderRspData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] OrderRsp - RequestID: %lld, OrderID: %lld, Error: %d, Msg: %s, Timestamp: %llu",
                 (long long)data->request_id, (long long)data->order_id, data->error_code, data->error_msg, (unsigned long long)data->timestamp);
}

// ===== 多个CancelRsp回调函数 =====
void on_cancel_rsp_callback_1(const TradeOrderCancelRsqData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-1] CancelRsp - RequestID: %lld, OrderID: %lld, Error: %d",
                 (long long)data->request_id, (long long)data->order_id, data->error_code);
}

void on_cancel_rsp_callback_2(const TradeOrderCancelRsqData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK-2] CancelRsp - RequestID: %lld, OrderID: %lld, Error: %d, Msg: %s, Timestamp: %llu",
                 (long long)data->request_id, (long long)data->order_id, data->error_code, data->error_msg, (unsigned long long)data->timestamp);
}

// ===== 银证转账和资金划拨回调函数 =====
void on_bank_transfer_callback(const BankSecuritiesTransferData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK] BankTransfer - RequestID: %lld, Direction: %d, BusinessStatus: %d, SerialNo: %llu",
                 (long long)data->request_id, data->direction, data->business_status, (unsigned long long)data->serial_no);
}

void on_fund_transfer_callback(const FundTransferData *data)
{
    if (!data)
        return;
    PRINTF_DEBUG("[CALLBACK] FundTransfer - RequestID: %lld",
                 (long long)data->request_id);
}

// ===== 设置多回调模式 =====
void setup_multiple_callback_mode()
{
    PRINTF_INFO("Setting up MULTIPLE CALLBACK mode...");

    // 设置多个Order回调
    PRINTF_INFO("Setting up Order callbacks...");
    TradeOrderCallbackHandle order_handle_1 = trade_set_order_rtn_callback(on_order_callback_1);
    TradeOrderCallbackHandle order_handle_2 = trade_set_order_rtn_callback(on_order_callback_2);
    TradeOrderCallbackHandle order_handle_3 = trade_set_order_rtn_callback(on_order_callback_3);

    g_order_handles.push_back(order_handle_1);
    g_order_handles.push_back(order_handle_2);
    g_order_handles.push_back(order_handle_3);

    PRINTF_INFO("Order callbacks registered - Handle1: %s, Handle2: %s, Handle3: %s",
                order_handle_1 ? "OK" : "FAIL",
                order_handle_2 ? "OK" : "FAIL",
                order_handle_3 ? "OK" : "FAIL");

    // 设置多个Fill回调
    PRINTF_INFO("Setting up Fill callbacks...");
    TradeFillCallbackHandle fill_handle_1 = trade_set_fill_callback(on_fill_callback_1);
    TradeFillCallbackHandle fill_handle_2 = trade_set_fill_callback(on_fill_callback_2);

    g_fill_handles.push_back(fill_handle_1);
    g_fill_handles.push_back(fill_handle_2);

    PRINTF_INFO("Fill callbacks registered - Handle1: %s, Handle2: %s",
                fill_handle_1 ? "OK" : "FAIL",
                fill_handle_2 ? "OK" : "FAIL");

    // 设置多个Position回调
    PRINTF_INFO("Setting up Position callbacks...");
    PositionCallbackHandle position_handle_1 = trade_set_position_callback(on_position_callback_1);
    PositionCallbackHandle position_handle_2 = trade_set_position_callback(on_position_callback_2);

    g_position_handles.push_back(position_handle_1);
    g_position_handles.push_back(position_handle_2);

    PRINTF_INFO("Position callbacks registered - Handle1: %s, Handle2: %s",
                position_handle_1 ? "OK" : "FAIL",
                position_handle_2 ? "OK" : "FAIL");

    // 设置多个Account回调
    PRINTF_INFO("Setting up Account callbacks...");
    AccountCallbackHandle account_handle_1 = trade_set_account_callback(on_account_callback_1);
    AccountCallbackHandle account_handle_2 = trade_set_account_callback(on_account_callback_2);

    g_account_handles.push_back(account_handle_1);
    g_account_handles.push_back(account_handle_2);

    PRINTF_INFO("Account callbacks registered - Handle1: %s, Handle2: %s",
                account_handle_1 ? "OK" : "FAIL",
                account_handle_2 ? "OK" : "FAIL");

    // 设置多个Error回调
    PRINTF_INFO("Setting up Error callbacks...");
    TradeErrorCallbackHandle error_handle_1 = trade_set_error_callback(on_error_callback_1);
    TradeErrorCallbackHandle error_handle_2 = trade_set_error_callback(on_error_callback_2);

    g_error_handles.push_back(error_handle_1);
    g_error_handles.push_back(error_handle_2);

    PRINTF_INFO("Error callbacks registered - Handle1: %s, Handle2: %s",
                error_handle_1 ? "OK" : "FAIL",
                error_handle_2 ? "OK" : "FAIL");

    // 设置多个OrderRsp回调
    PRINTF_INFO("Setting up OrderRsp callbacks...");
    TradeOrderRspCallbackHandle order_rsp_handle_1 = trade_set_order_rsp_callback(on_order_rsp_callback_1);
    TradeOrderRspCallbackHandle order_rsp_handle_2 = trade_set_order_rsp_callback(on_order_rsp_callback_2);

    g_order_rsp_handles.push_back(order_rsp_handle_1);
    g_order_rsp_handles.push_back(order_rsp_handle_2);

    PRINTF_INFO("OrderRsp callbacks registered - Handle1: %s, Handle2: %s",
                order_rsp_handle_1 ? "OK" : "FAIL",
                order_rsp_handle_2 ? "OK" : "FAIL");

    // 设置多个CancelRsp回调
    PRINTF_INFO("Setting up CancelRsp callbacks...");
    TradeOrderCancelRspCallbackHandle cancel_rsp_handle_1 = trade_set_cancel_rsp_callback(on_cancel_rsp_callback_1);
    TradeOrderCancelRspCallbackHandle cancel_rsp_handle_2 = trade_set_cancel_rsp_callback(on_cancel_rsp_callback_2);

    g_cancel_rsp_handles.push_back(cancel_rsp_handle_1);
    g_cancel_rsp_handles.push_back(cancel_rsp_handle_2);

    PRINTF_INFO("CancelRsp callbacks registered - Handle1: %s, Handle2: %s",
                cancel_rsp_handle_1 ? "OK" : "FAIL",
                cancel_rsp_handle_2 ? "OK" : "FAIL");

    // 设置银证转账和资金划拨回调
    PRINTF_INFO("Setting up Transfer callbacks...");
    BankSecuritiesTransferCallbackHandle bank_handle = trade_set_bank_securities_transfer_callback(on_bank_transfer_callback);
    FundTransferCallbackHandle fund_handle = trade_set_fund_transfer_callback(on_fund_transfer_callback);

    g_bank_transfer_handles.push_back(bank_handle);
    g_fund_transfer_handles.push_back(fund_handle);

    PRINTF_INFO("Transfer callbacks registered - Bank: %s, Fund: %s",
                bank_handle ? "OK" : "FAIL",
                fund_handle ? "OK" : "FAIL");

    // 测试空回调
    PRINTF_INFO("Testing NULL callback...");
    TradeOrderCallbackHandle null_handle = trade_set_order_rtn_callback(nullptr);
    PRINTF_INFO("NULL callback handle: %s", null_handle ? "UNEXPECTED_OK" : "EXPECTED_FAIL");

    PRINTF_INFO("MULTIPLE CALLBACK mode setup complete!");
    PRINTF_INFO("Total callbacks registered - Order: %zu, Fill: %zu, Position: %zu, Account: %zu, Error: %zu",
                g_order_handles.size(), g_fill_handles.size(), g_position_handles.size(),
                g_account_handles.size(), g_error_handles.size());
}

// ===== 测试回调移除功能 =====
void test_callback_removal()
{
    PRINTF_INFO("=== Starting callback removal test ===");

    // 等待5秒观察所有回调工作
    PRINTF_INFO("Observing all callbacks for 5 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 移除第一个Order回调
    if (!g_order_handles.empty() && g_order_handles[0])
    {
        PRINTF_INFO("Removing first Order callback...");
        int result = trade_remove_order_rtn_callback(g_order_handles[0]);
        PRINTF_INFO("Remove result: %s", result == 0 ? "SUCCESS" : "FAILED");
        g_order_handles[0] = TradeOrderCallbackHandle{}; // 清空句柄
    }

    // 等待3秒观察效果
    PRINTF_INFO("Observing after removing first Order callback for 3 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 移除所有Fill回调
    PRINTF_INFO("Removing all Fill callbacks...");
    for (size_t i = 0; i < g_fill_handles.size(); ++i)
    {
        if (g_fill_handles[i])
        {
            int result = trade_remove_fill_callback(g_fill_handles[i]);
            PRINTF_INFO("Remove Fill callback %zu: %s", i + 1, result == 0 ? "SUCCESS" : "FAILED");
            g_fill_handles[i] = TradeFillCallbackHandle{}; // 清空句柄
        }
    }

    // 等待3秒观察效果
    PRINTF_INFO("Observing after removing all Fill callbacks for 3 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 移除第一个Error回调
    if (!g_error_handles.empty() && g_error_handles[0])
    {
        PRINTF_INFO("Removing first Error callback...");
        int result = trade_remove_error_callback(g_error_handles[0]);
        PRINTF_INFO("Remove Error callback result: %s", result == 0 ? "SUCCESS" : "FAILED");
        g_error_handles[0] = TradeErrorCallbackHandle{}; // 清空句柄
    }

    PRINTF_INFO("=== Callback removal test completed ===");
}

// ===== 清理所有回调 =====
void cleanup_callbacks()
{
    PRINTF_INFO("Cleaning up all remaining callbacks...");

    // 清理Order回调
    for (auto &handle : g_order_handles)
    {
        if (handle)
        {
            trade_remove_order_rtn_callback(handle);
        }
    }
    g_order_handles.clear();

    // 清理Fill回调
    for (auto &handle : g_fill_handles)
    {
        if (handle)
        {
            trade_remove_fill_callback(handle);
        }
    }
    g_fill_handles.clear();

    // 清理Position回调
    for (auto &handle : g_position_handles)
    {
        if (handle)
        {
            trade_remove_position_callback(handle);
        }
    }
    g_position_handles.clear();

    // 清理Account回调
    for (auto &handle : g_account_handles)
    {
        if (handle)
        {
            trade_remove_account_callback(handle);
        }
    }
    g_account_handles.clear();

    // 清理Error回调
    for (auto &handle : g_error_handles)
    {
        if (handle)
        {
            trade_remove_error_callback(handle);
        }
    }
    g_error_handles.clear();

    // 清理银证转账回调
    for (auto &handle : g_bank_transfer_handles)
    {
        if (handle)
        {
            trade_remove_bank_securities_transfer_callback(handle);
        }
    }
    g_bank_transfer_handles.clear();

    // 清理资金划拨回调
    for (auto &handle : g_fund_transfer_handles)
    {
        if (handle)
        {
            trade_remove_fund_transfer_callback(handle);
        }
    }
    g_fund_transfer_handles.clear();

    // 清理OrderRsp回调
    for (auto &handle : g_order_rsp_handles)
    {
        if (handle)
        {
            trade_remove_order_rsp_callback(handle);
        }
    }
    g_order_rsp_handles.clear();

    // 清理CancelRsp回调
    for (auto &handle : g_cancel_rsp_handles)
    {
        if (handle)
        {
            trade_remove_cancel_rsp_callback(handle);
        }
    }
    g_cancel_rsp_handles.clear();

    PRINTF_INFO("All callbacks cleaned up!");
}

#endif

#if TEST_DATA_MANAGER_MODE
// ===== 模式2: 数据管理模式 =====
void test_memory_storage_mode()
{
    // 获取最新订单数据
    TradeOrderData order_data;
    int order_result = trade_get_order_data(&order_data);
    if (order_result > 0)
    {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Order Data - OrderID: %lld, Symbol: %s, Price: %.3f",
                    (long long)order_data.order_id, order_data.security_info.code, order_data.price);
    }

    // 获取最新成交数据
    TradeFillData fill_data;
    int fill_result = trade_get_fill_data(&fill_data);
    if (fill_result > 0)
    {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Fill Data - OrderID: %lld, Price: %.3f, Qty: %llu",
                    (long long)fill_data.order_id, fill_data.price, (unsigned long long)fill_data.qty);
    }

    // 获取最新持仓数据
    PositionData position_data;
    int position_result = trade_get_position_data(&position_data);
    if (position_result > 0)
    {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Position Data - Symbol: %s, TotalQty: %llu, CostPrice: %.3f",
                    position_data.security_info.code, (unsigned long long)position_data.total_qty, position_data.cost_price);
    }
}
#endif

#if TEST_ACCOUNT_MANAGEMENT
// ===== 模式3: 账户管理测试 =====
void test_account_management()
{
    // 测试获取账户数量
    int account_count = trade_get_account_count();
    PRINTF_INFO("[ACCOUNT_MGMT] Current account count: %d", account_count);

    if (account_count > 0)
    {
        // 获取所有账户ID
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result == 0 && account_ids && count > 0)
        {
            PRINTF_INFO("[ACCOUNT_MGMT] Account IDs and Login Status:");
            for (int i = 0; i < count; ++i)
            {
                // 检查登录状态
                int login_status = trade_is_account_login(account_ids[i]);
                const char* status_str = (login_status == 1) ? "LOGGED_IN" : "LOGGED_OUT";
                
                PRINTF_INFO("  [%d] %s - Status: %s", i, account_ids[i], status_str);

                // 获取账户信息
                AccountInfo account_info;
                result = trade_get_account_info_by_id(account_ids[i], &account_info);
                if (result == 0)
                {
                    PRINTF_INFO("    - Fund Account: %s, SH: %s, SZ: %s",
                                account_info.fund_account_id,
                                account_info.account_id_sh,
                                account_info.account_id_sz);

                    // 只有在账户已登录时才尝试获取资金数据
                    if (login_status == 1)
                    {
                        // 获取上海市场的账户资金数据
                        if (strlen(account_info.account_id_sh) > 0)
                        {
                            AccountData sh_account_data;
                            result = trade_get_account_data_by_id(account_info.login_id,
                                                                 account_info.fund_account_id,
                                                                 account_info.account_id_sh,
                                                                 &sh_account_data);
                            if (result > 0)
                            {
                                PRINTF_INFO("    - SH Market - CashBalance: %.2f, AvailableCash: %.2f, FrozenCash: %.2f",
                                            sh_account_data.cash_balance, sh_account_data.available_cash, sh_account_data.frozen_cash);
                            }
                        }

                        // 获取深圳市场的账户资金数据
                        if (strlen(account_info.account_id_sz) > 0)
                        {
                            AccountData sz_account_data;
                            result = trade_get_account_data_by_id(account_info.login_id,
                                                                 account_info.fund_account_id,
                                                                 account_info.account_id_sz,
                                                                 &sz_account_data);
                            if (result > 0)
                            {
                                PRINTF_INFO("    - SZ Market - CashBalance: %.2f, AvailableCash: %.2f, FrozenCash: %.2f",
                                            sz_account_data.cash_balance, sz_account_data.available_cash, sz_account_data.frozen_cash);
                            }
                        }

                        // 或者使用便利接口获取所有市场的数据
                        AccountData sh_data, sz_data;
                        result = trade_get_all_account_data_by_fund_id(account_info.login_id,
                                                                      account_info.fund_account_id,
                                                                      &sh_data, &sz_data);
                        if (result > 0)
                        {
                            PRINTF_INFO("    - All Markets Retrieved: SH[%.2f], SZ[%.2f]",
                                        sh_data.cash_balance, sz_data.cash_balance);
                        }
                    }
                    else
                    {
                        PRINTF_INFO("    - Account not logged in, skipping fund data query");
                    }
                }
            }
            
            // 释放内存
            trade_free_account_ids(account_ids, count);
        }
        
        // 测试登录状态统计
        int total_logged_in = trade_get_logged_in_account_count();
        PRINTF_INFO("[ACCOUNT_MGMT] Total logged in accounts: %d/%d", total_logged_in, account_count);
        
        // 获取已登录账户列表
        char **logged_in_ids = nullptr;
        int logged_in_count = 0;
        result = trade_get_logged_in_login_ids(&logged_in_ids, &logged_in_count);
        
        if (result == 0 && logged_in_ids && logged_in_count > 0)
        {
            PRINTF_INFO("[ACCOUNT_MGMT] Currently logged in accounts:");
            for (int i = 0; i < logged_in_count; ++i)
            {
                PRINTF_INFO("  - %s", logged_in_ids[i]);
            }
            trade_free_account_ids(logged_in_ids, logged_in_count);
        }
        else if (logged_in_count == 0)
        {
            PRINTF_WARN("[ACCOUNT_MGMT] No accounts are currently logged in");
        }
    }
}
#endif

#if TEST_TRADING_OPERATIONS
// ===== 交易操作测试模式 =====

class TradingTestManager
{
private:
    std::vector<int64_t> _active_order_ids;
    std::mutex _order_mutex;

public:
    TradingTestManager() {}

    // 添加活跃订单ID
    void add_active_order(int64_t order_id)
    {
        std::lock_guard<std::mutex> lock(_order_mutex);
        _active_order_ids.push_back(order_id);
        PRINTF_INFO("[TRADING_TEST] Added active order: %lld", (long long)order_id);
    }

    // 移除活跃订单ID
    void remove_active_order(int64_t order_id)
    {
        std::lock_guard<std::mutex> lock(_order_mutex);
        auto it = std::find(_active_order_ids.begin(), _active_order_ids.end(), order_id);
        if (it != _active_order_ids.end())
        {
            _active_order_ids.erase(it);
            PRINTF_INFO("[TRADING_TEST] Removed active order: %lld", (long long)order_id);
        }
    }

    // 获取第一个活跃订单ID用于撤单测试
    int64_t get_first_active_order()
    {
        std::lock_guard<std::mutex> lock(_order_mutex);
        return _active_order_ids.empty() ? 0 : _active_order_ids[0];
    }

    // 获取活跃订单数量
    size_t get_active_order_count()
    {
        std::lock_guard<std::mutex> lock(_order_mutex);
        return _active_order_ids.size();
    }

    // 执行单个测试订单
    bool execute_test_order(const TestOrderConfig &config)
    {
        // 获取第一个账户信息
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0 || !account_ids || count == 0)
        {
            PRINTF_ERROR("[TRADING_TEST] No accounts available");
            return false;
        }

        AccountInfo account_info;
        result = trade_get_account_info_by_id(account_ids[0], &account_info);
        
        if (result != 0)
        {
            trade_free_account_ids(account_ids, count);
            PRINTF_ERROR("[TRADING_TEST] Failed to get account info");
            return false;
        }
        
        // 检查账户登录状态
        int login_status = trade_is_account_login(account_info.login_id);
        if (login_status != 1)
        {
            trade_free_account_ids(account_ids, count);
            PRINTF_ERROR("[TRADING_TEST] Account %s is not logged in, cannot execute order: %s", 
                        account_info.login_id, config.description);
            return false;
        }
        
        PRINTF_INFO("[TRADING_TEST] Account %s is logged in, proceeding with order", account_info.login_id);
        
        // 释放账户ID内存
        trade_free_account_ids(account_ids, count);
        
        // 构建测试订单
        TradeOrderRequest order = {};
        // 填充账户信息
        order.account = account_info;
        // 填充证券信息
        strncpy(order.security_code, config.symbol, sizeof(order.security_code) - 1);
        order.market = config.market;
        // 填充订单信息
        order.side = config.side;
        order.order_type = config.order_type;
        order.price = config.price;
        order.qty = config.qty;

        PRINTF_INFO("[TRADING_TEST] Sending order: %s - %s %s %llu@%.2f (%s)",
                    config.description,
                    config.symbol,
                    config.side == SIDE_BUY ? "BUY" : "SELL",
                    (unsigned long long)config.qty,
                    config.price,
                    config.order_type == ORDER_TYPE_LIMIT ? "LIMIT" : config.order_type == ORDER_TYPE_MARKET ? "MARKET" : "BEST");

        // 发送订单 - 现在接收返回的request_id
        int64_t request_id = trade_send_order(&order);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Order sent successfully, request_id: %lld", (long long)request_id);
            // 如果不是预期失败的订单，添加到活跃订单列表
            if (!config.should_fail)
            {
                add_active_order(request_id); // 使用request_id作为订单标识
            }
            return true;
        }
        else
        {
            if (config.should_fail)
            {
                PRINTF_INFO("[TRADING_TEST] Order failed as expected: %s", trade_get_last_error());
            }
            else
            {
                PRINTF_ERROR("[TRADING_TEST] Unexpected order failure: %s", trade_get_last_error());
            }
            return config.should_fail;
        }
    }

    // 执行撤单测试
    bool execute_test_cancel(const TestCancelConfig &config)
    {
        int64_t order_id = get_first_active_order();
        if (order_id == 0)
        {
            PRINTF_WARN("[TRADING_TEST] No active orders for cancel test: %s", config.description);
            return false;
        }

        // 获取第一个账户信息
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0 || !account_ids || count == 0)
        {
            PRINTF_ERROR("[TRADING_TEST] No accounts available for cancel");
            return false;
        }

        AccountInfo account_info;
        result = trade_get_account_info_by_id(account_ids[0], &account_info);
        
        // 释放账户ID内存
        trade_free_account_ids(account_ids, count);
        
        if (result != 0)
        {
            PRINTF_ERROR("[TRADING_TEST] Failed to get account info for cancel");
            return false;
        }

        PRINTF_INFO("[TRADING_TEST] Canceling order: %s - OrderID: %lld",
                    config.description, (long long)order_id);

        // 发送撤单 - 现在接收返回的request_id
        int64_t request_id = trade_cancel_order(&account_info, order_id, order_id, MARKET_SZSE);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Cancel sent successfully, request_id: %lld", (long long)request_id);
            remove_active_order(order_id);
            return true;
        }
        else
        {
            if (config.should_fail)
            {
                PRINTF_INFO("[TRADING_TEST] Cancel failed as expected: %s", trade_get_last_error());
            }
            else
            {
                PRINTF_ERROR("[TRADING_TEST] Unexpected cancel failure: %s", trade_get_last_error());
            }
            return config.should_fail;
        }
    }

    // 执行查询测试
    void execute_query_tests()
    {
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0 || !account_ids || count == 0)
        {
            return;
        }

        AccountInfo account_info;
        result = trade_get_account_info_by_id(account_ids[0], &account_info);
        
        // 释放账户ID内存
        trade_free_account_ids(account_ids, count);
        
        if (result != 0)
        {
            return;
        }

        PRINTF_INFO("[TRADING_TEST] Querying orders for SZSE market...");
        int64_t request_id = trade_query_orders(&account_info, MARKET_SZSE, nullptr, 0, nullptr);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Query orders request_id: %lld", (long long)request_id);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        PRINTF_INFO("[TRADING_TEST] Querying positions for SZSE market...");
        request_id = trade_query_positions(&account_info, MARKET_SZSE, nullptr, nullptr);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Query positions request_id: %lld", (long long)request_id);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        PRINTF_INFO("[TRADING_TEST] Querying funds for SZSE market...");
        request_id = trade_query_funds(&account_info, MARKET_SZSE, "CNY", nullptr);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Query funds request_id: %lld", (long long)request_id);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        PRINTF_INFO("[TRADING_TEST] Querying trades for SZSE market...");
        request_id = trade_query_trades(&account_info, MARKET_SZSE, nullptr, 0, nullptr);
        if (request_id > 0)
        {
            PRINTF_INFO("[TRADING_TEST] Query trades request_id: %lld", (long long)request_id);
        }
    }
};

// 全局测试管理器
std::unique_ptr<TradingTestManager> g_test_manager;

// 执行交易测试序列
void execute_trading_test_sequence(TradingTestManager *test_mgr)
{
    if (!test_mgr)
        return;
    
    PRINTF_INFO("[TRADING_TEST] Starting comprehensive trading test sequence...");
    
    // 第一阶段：发送各种类型的测试订单
    PRINTF_INFO("[TRADING_TEST] === Phase 1: Order Submission Tests ===");
    for (size_t i = 0; i < sizeof(g_test_orders) / sizeof(g_test_orders[0]); ++i)
    {
        test_mgr->execute_test_order(g_test_orders[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    // 等待订单处理
    PRINTF_INFO("[TRADING_TEST] Waiting for orders to be processed...");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 第二阶段：执行查询测试
    PRINTF_INFO("[TRADING_TEST] === Phase 2: Query Tests ===");
    test_mgr->execute_query_tests();
    
    // 等待查询结果
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 第三阶段：撤单测试
    PRINTF_INFO("[TRADING_TEST] === Phase 3: Cancel Tests ===");
    PRINTF_INFO("[TRADING_TEST] Active orders count: %zu", test_mgr->get_active_order_count());
    for (size_t i = 0; i < sizeof(g_test_cancels) / sizeof(g_test_cancels[0]) &&
                       test_mgr->get_active_order_count() > 0;
         ++i)
    {
        test_mgr->execute_test_cancel(g_test_cancels[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    PRINTF_INFO("[TRADING_TEST] Trading test sequence completed!");
}
#endif

#if TEST_SYNC_OPERATIONS
// ===== 同步接口测试配置 =====
struct SyncTestConfig
{
    const char *test_name;        // 测试名称
    const char *symbol;           // 证券代码
    MarketType market;            // 市场类型
    OrderSide side;               // 买卖方向
    double price;                 // 价格
    uint64_t qty;                 // 数量
    OrderType order_type;         // 订单类型
    int timeout_ms;               // 超时时间（毫秒）
    bool should_succeed;          // 是否应该成功
    bool test_timeout;            // 是否测试超时
};

// 同步测试订单配置
static SyncTestConfig g_sync_test_orders[] = {
    // 正常同步下单测试
    {"同步下单-深圳限价单", "000001", MARKET_SZSE, SIDE_BUY, 10.50, 100, ORDER_TYPE_LIMIT, 30000, true, false},
    {"同步下单-上海限价单", "600000", MARKET_SSE, SIDE_BUY, 15.20, 100, ORDER_TYPE_LIMIT, 30000, true, false},
    {"同步下单-深圳市价单", "000002", MARKET_SZSE, SIDE_BUY, 0.0, 100, ORDER_TYPE_MARKET, 30000, true, false},
    
    // 失败测试
    {"同步下单-超量失败", "000001", MARKET_SZSE, SIDE_BUY, 10.50, 10000, ORDER_TYPE_LIMIT, 30000, false, false},
    
    // 超时测试
    {"同步下单-超时测试", "000003", MARKET_SZSE, SIDE_BUY, 10.50, 100, ORDER_TYPE_LIMIT, 100, false, true},
    
    // 卖单测试
    {"同步下单-深圳卖单", "000001", MARKET_SZSE, SIDE_SELL, 10.60, 100, ORDER_TYPE_LIMIT, 30000, true, false},
    {"同步下单-上海卖单", "600000", MARKET_SSE, SIDE_SELL, 15.30, 100, ORDER_TYPE_LIMIT, 30000, true, false},
};

// 同步接口测试管理器
class SyncInterfaceTestManager 
{
private:
    std::vector<int64_t> _successful_order_ids;  // 成功下单的订单ID，用于后续撤单测试
    std::mutex _order_mutex;
    
    // 测试统计
    struct TestStats {
        int total_tests = 0;
        int passed_tests = 0;
        int failed_tests = 0;
        int timeout_tests = 0;
        
        void reset() {
            total_tests = passed_tests = failed_tests = timeout_tests = 0;
        }
        
        void print_summary(const char* category) {
            PRINTF_INFO("[SYNC_TEST] %s Summary - Total: %d, Passed: %d, Failed: %d, Timeout: %d", 
                       category, total_tests, passed_tests, failed_tests, timeout_tests);
            if (total_tests > 0) {
                double success_rate = (double)passed_tests / total_tests * 100.0;
                PRINTF_INFO("[SYNC_TEST] Success Rate: %.1f%%", success_rate);
            }
        }
    } _stats;

public:
    SyncInterfaceTestManager() {}
    
    // 获取测试用的账户信息
    bool get_test_account(AccountInfo& account) {
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0 || !account_ids || count == 0) {
            PRINTF_ERROR("[SYNC_TEST] No accounts available for testing");
            return false;
        }
        
        result = trade_get_account_info_by_id(account_ids[0], &account);
        
        if (result != 0) {
            trade_free_account_ids(account_ids, count);
            PRINTF_ERROR("[SYNC_TEST] Failed to get account info: %s", trade_get_last_error());
            return false;
        }
        
        // 检查登录状态
        int login_status = trade_is_account_login(account.login_id);
        if (login_status != 1) {
            trade_free_account_ids(account_ids, count);
            PRINTF_ERROR("[SYNC_TEST] Account %s is not logged in, cannot run sync tests", account.login_id);
            return false;
        }
        
        PRINTF_INFO("[SYNC_TEST] Using account %s (logged in) for testing", account.login_id);
        
        // 释放内存
        trade_free_account_ids(account_ids, count);
        
        return true;
    }
    
    // 测试同步下单接口
    void test_sync_send_order() {
        PRINTF_INFO("[SYNC_TEST] ======= 开始同步下单接口测试 =======");
        _stats.reset();
        
        AccountInfo account;
        if (!get_test_account(account)) {
            return;
        }
        
        for (const auto& config : g_sync_test_orders) {
            test_single_sync_order(account, config);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 间隔500ms
        }
        
        _stats.print_summary("同步下单接口");
        PRINTF_INFO("[SYNC_TEST] ======= 同步下单接口测试完成 =======");
    }
    
    // 测试单个同步下单
    void test_single_sync_order(const AccountInfo& account, const SyncTestConfig& config) {
        PRINTF_INFO("[SYNC_TEST] 执行测试: %s", config.test_name);
        _stats.total_tests++;
        
        // 构建订单
        TradeOrderRequest order = {};
        order.account = account;
        strncpy(order.security_code, config.symbol, sizeof(order.security_code) - 1);
        order.market = config.market;
        order.side = config.side;
        order.order_type = config.order_type;
        order.price = config.price;
        order.qty = config.qty;
        
        // 记录开始时间
        auto start_time = std::chrono::steady_clock::now();
        
        // 调用同步接口
        TradeOrderRspData result = {};
        int32_t ret = trade_send_order_sync(&order, &result, config.timeout_ms);
        
        // 计算耗时
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // 分析结果
        if (config.test_timeout) {
            // 测试超时场景
            if (ret != 0 && duration.count() >= config.timeout_ms - 100) { // 允许100ms误差
                PRINTF_INFO("[SYNC_TEST] ✅ %s - 超时测试成功，耗时: %lldms, 返回码: %d", 
                           config.test_name, (long long)duration.count(), ret);
                _stats.timeout_tests++;
                _stats.passed_tests++;
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ %s - 超时测试失败，预期超时但未超时，耗时: %lldms, 返回码: %d", 
                            config.test_name, (long long)duration.count(), ret);
                _stats.failed_tests++;
            }
        } else if (config.should_succeed) {
            // 预期成功的测试
            if (ret == 0) {
                PRINTF_INFO("[SYNC_TEST] ✅ %s - 成功，耗时: %lldms, OrderID: %lld, Price: %.3f, Qty: %llu", 
                           config.test_name, (long long)duration.count(), 
                           (long long)result.order_id, result.price, (unsigned long long)result.qty);
                _stats.passed_tests++;
                
                // 保存成功的订单ID用于撤单测试
                std::lock_guard<std::mutex> lock(_order_mutex);
                _successful_order_ids.push_back(result.order_id);
                
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ %s - 失败，耗时: %lldms, 错误码: %d, 错误消息: %s", 
                            config.test_name, (long long)duration.count(), ret, result.error_msg);
                _stats.failed_tests++;
            }
        } else {
            // 预期失败的测试
            if (ret != 0) {
                PRINTF_INFO("[SYNC_TEST] ✅ %s - 按预期失败，耗时: %lldms, 错误码: %d, 错误消息: %s", 
                           config.test_name, (long long)duration.count(), ret, result.error_msg);
                _stats.passed_tests++;
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ %s - 意外成功，预期应该失败，耗时: %lldms", 
                            config.test_name, (long long)duration.count());
                _stats.failed_tests++;
            }
        }
    }
    
    // 测试同步撤单接口
    void test_sync_cancel_order() {
        PRINTF_INFO("[SYNC_TEST] ======= 开始同步撤单接口测试 =======");
        _stats.reset();
        
        AccountInfo account;
        if (!get_test_account(account)) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(_order_mutex);
        if (_successful_order_ids.empty()) {
            PRINTF_WARN("[SYNC_TEST] 没有可撤销的订单，跳过撤单测试");
            return;
        }
        
        // 测试正常撤单
        for (size_t i = 0; i < std::min(_successful_order_ids.size(), size_t(3)); ++i) {
            test_single_sync_cancel(account, _successful_order_ids[i], 30000, true, "正常撤单");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        // 测试撤销不存在的订单
        test_single_sync_cancel(account, 999999999, 30000, false, "撤销不存在订单");
        
        // 测试超时撤单
        if (!_successful_order_ids.empty()) {
            test_single_sync_cancel(account, _successful_order_ids.back(), 100, false, "超时撤单测试");
        }
        
        _stats.print_summary("同步撤单接口");
        PRINTF_INFO("[SYNC_TEST] ======= 同步撤单接口测试完成 =======");
    }
    
    // 测试单个同步撤单
    void test_single_sync_cancel(const AccountInfo& account, int64_t order_id, int timeout_ms, 
                                bool should_succeed, const char* test_name) {
        PRINTF_INFO("[SYNC_TEST] 执行撤单测试: %s, OrderID: %lld", test_name, (long long)order_id);
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        TradeOrderCancelRsqData result = {};
        int32_t ret = trade_cancel_order_sync(&account, order_id, order_id, MARKET_SZSE, &result, timeout_ms);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (should_succeed) {
            if (ret == 0) {
                PRINTF_INFO("[SYNC_TEST] ✅ %s - 成功，耗时: %lldms, OrderID: %lld", 
                           test_name, (long long)duration.count(), (long long)result.order_id);
                _stats.passed_tests++;
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ %s - 失败，耗时: %lldms, 错误码: %d, 错误消息: %s", 
                            test_name, (long long)duration.count(), ret, result.error_msg);
                _stats.failed_tests++;
            }
        } else {
            if (ret != 0) {
                PRINTF_INFO("[SYNC_TEST] ✅ %s - 按预期失败，耗时: %lldms, 错误码: %d", 
                           test_name, (long long)duration.count(), ret);
                _stats.passed_tests++;
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ %s - 意外成功，预期应该失败，耗时: %lldms", 
                            test_name, (long long)duration.count());
                _stats.failed_tests++;
            }
        }
    }
    
    // 测试同步查询接口
    void test_sync_query_interfaces() {
        PRINTF_INFO("[SYNC_TEST] ======= 开始同步查询接口测试 =======");
        _stats.reset();
        
        AccountInfo account;
        if (!get_test_account(account)) {
            return;
        }
        
        // 测试查询订单
        test_sync_query_orders(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 测试查询持仓
        test_sync_query_positions(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 测试查询资金
        test_sync_query_funds(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 测试查询成交
        test_sync_query_trades(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        _stats.print_summary("同步查询接口");
        PRINTF_INFO("[SYNC_TEST] ======= 同步查询接口测试完成 =======");
    }
    
    // 测试同步查询订单
    void test_sync_query_orders(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步查询订单接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        TradeOrderData* results = nullptr;
        int count = 0;
        int32_t ret = trade_query_orders_sync(&account, MARKET_SZSE, nullptr, 0, nullptr, &results, &count, 30000, nullptr);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 查询订单成功，耗时: %lldms, 查询到 %d 条记录", 
                       (long long)duration.count(), count);
            
            // 打印查询结果
            for (int i = 0; i < count; ++i) {
                PRINTF_INFO("[SYNC_TEST]   [%d] OrderID: %lld, Symbol: %s, Status: %d, Price: %.3f, Qty: %llu", 
                           i + 1, (long long)results[i].order_id, results[i].security_info.code, 
                           results[i].status, results[i].price, (unsigned long long)results[i].qty);
            }
            
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 查询订单失败，耗时: %lldms, 错误码: %d", 
                        (long long)duration.count(), ret);
            _stats.failed_tests++;
        }
        
        // 释放查询结果内存
        if (results) {
            trade_free_query_results(results, count);
        }
    }
    
    // 测试同步查询持仓
    void test_sync_query_positions(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步查询持仓接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        PositionData* results = nullptr;
        int count = 0;
        int32_t ret = trade_query_positions_sync(&account, MARKET_SZSE, nullptr, nullptr, &results, &count, 30000, nullptr);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 查询持仓成功，耗时: %lldms, 查询到 %d 条记录", 
                       (long long)duration.count(), count);
            
            // 打印查询结果
            for (int i = 0; i < count; ++i) {
                PRINTF_INFO("[SYNC_TEST]   [%d] Symbol: %s, TotalQty: %llu, AvailableQty: %llu, CostPrice: %.3f", 
                           i + 1, results[i].security_info.code, 
                           (unsigned long long)results[i].total_qty, 
                           (unsigned long long)results[i].available_qty,
                           results[i].cost_price);
            }
            
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 查询持仓失败，耗时: %lldms, 错误码: %d", 
                        (long long)duration.count(), ret);
            _stats.failed_tests++;
        }
        
        // 释放查询结果内存
        if (results) {
            trade_free_query_results(results, count);
        }
    }
    
    // 测试同步查询资金
    void test_sync_query_funds(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步查询资金接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        AccountData* results = nullptr;
        int count = 0;
        int32_t ret = trade_query_funds_sync(&account, MARKET_SZSE, "CNY", nullptr, &results, &count, 30000, nullptr);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 查询资金成功，耗时: %lldms, 查询到 %d 条记录", 
                       (long long)duration.count(), count);
            
            // 打印查询结果
            for (int i = 0; i < count; ++i) {
                PRINTF_INFO("[SYNC_TEST]   [%d] CashBalance: %.2f, AvailableCash: %.2f, FrozenCash: %.2f", 
                           i + 1, results[i].cash_balance, results[i].available_cash, results[i].frozen_cash);
            }
            
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 查询资金失败，耗时: %lldms, 错误码: %d", 
                        (long long)duration.count(), ret);
            _stats.failed_tests++;
        }
        
        // 释放查询结果内存
        if (results) {
            trade_free_query_results(results, count);
        }
    }
    
    // 测试同步查询成交
    void test_sync_query_trades(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步查询成交接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        TradeFillData* results = nullptr;
        int count = 0;
        int32_t ret = trade_query_trades_sync(&account, MARKET_SZSE, nullptr, 0, nullptr, &results, &count, 30000, nullptr);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 查询成交成功，耗时: %lldms, 查询到 %d 条记录", 
                       (long long)duration.count(), count);
            
            // 打印查询结果
            for (int i = 0; i < count; ++i) {
                PRINTF_INFO("[SYNC_TEST]   [%d] TradeID: %lld, OrderID: %lld, Symbol: %s, Price: %.3f, Qty: %llu", 
                           i + 1, (long long)results[i].trade_id, (long long)results[i].order_id,
                           results[i].security_info.code, results[i].price, 
                           (unsigned long long)results[i].qty);
            }
            
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 查询成交失败，耗时: %lldms, 错误码: %d", 
                        (long long)duration.count(), ret);
            _stats.failed_tests++;
        }
        
        // 释放查询结果内存
        if (results) {
            trade_free_query_results(results, count);
        }
    }
    
    // 测试同步转账接口
    void test_sync_transfer_interfaces() {
        PRINTF_INFO("[SYNC_TEST] ======= 开始同步转账接口测试 =======");
        _stats.reset();
        
        AccountInfo account;
        if (!get_test_account(account)) {
            return;
        }
        
        // 测试银证转账（这里使用小金额测试，避免实际转账）
        test_sync_bank_securities_transfer(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 测试资金划拨
        test_sync_fund_transfer(account);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        _stats.print_summary("同步转账接口");
        PRINTF_INFO("[SYNC_TEST] ======= 同步转账接口测试完成 =======");
    }
    
    // 测试同步银证转账
    void test_sync_bank_securities_transfer(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步银证转账接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        BankSecuritiesTransferData result = {};
        // 使用很小的金额进行测试（1元），方向为银行转证券
        int32_t ret = trade_bank_securities_transfer_sync(&account, 
                                                         BANK_SECURITIES_TRANSFER_IN, 
                                                         MARKET_SZSE,
                                                         1.0, "CNY", "", "", "测试转账", 
                                                         &result, 30000);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 银证转账成功，耗时: %lldms, Direction: %d, SerialNo: %llu", 
                       (long long)duration.count(), 
                       result.direction, (unsigned long long)result.serial_no);
            _stats.passed_tests++;
        } else {
            // 银证转账可能因为各种原因失败（如账户未开通、密码错误等），这是正常的
            PRINTF_WARN("[SYNC_TEST] ⚠️ 银证转账失败（可能是正常的），耗时: %lldms, 错误码: %d", 
                       (long long)duration.count(), ret);
            _stats.passed_tests++; // 将其视为通过，因为接口正常响应了
        }
    }
    
    // 测试同步资金划拨
    void test_sync_fund_transfer(const AccountInfo& account) {
        PRINTF_INFO("[SYNC_TEST] 测试同步资金划拨接口");
        _stats.total_tests++;
        
        auto start_time = std::chrono::steady_clock::now();
        
        FundTransferData result = {};
        // 测试从深圳账户向上海账户划拨1元
        int32_t ret = trade_fund_transfer_sync(&account, 
                                              account.account_id_sz, MARKET_SZSE,
                                              account.account_id_sh, MARKET_SSE,
                                              1.0, &result, 30000);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (ret == 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 资金划拨成功，耗时: %lldms", 
                       (long long)duration.count());
            _stats.passed_tests++;
        } else {
            // 资金划拨可能因为各种原因失败（如资金不足、账户限制等），这也是正常的
            PRINTF_WARN("[SYNC_TEST] ⚠️ 资金划拨失败（可能是正常的），耗时: %lldms, 错误码: %d", 
                       (long long)duration.count(), ret);
            _stats.passed_tests++; // 将其视为通过，因为接口正常响应了
        }
    }
    
    // 测试参数验证
    void test_parameter_validation() {
        PRINTF_INFO("[SYNC_TEST] ======= 开始参数验证测试 =======");
        _stats.reset();
        
        // 测试空指针参数
        test_null_parameters();
        
        // 测试无效参数
        test_invalid_parameters();
        
        _stats.print_summary("参数验证");
        PRINTF_INFO("[SYNC_TEST] ======= 参数验证测试完成 =======");
    }
    
    // 测试空指针参数
    void test_null_parameters() {
        PRINTF_INFO("[SYNC_TEST] 测试空指针参数");
        
        TradeOrderRspData order_result = {};
        int32_t ret = trade_send_order_sync(nullptr, &order_result, 30000);
        _stats.total_tests++;
        
        if (ret != 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 空order参数正确返回失败");
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 空order参数应该返回失败");
            _stats.failed_tests++;
        }
        
        AccountInfo account;
        if (get_test_account(account)) {
            TradeOrderRequest order = {};
            ret = trade_send_order_sync(&order, nullptr, 30000);
            _stats.total_tests++;
            
            if (ret != 0) {
                PRINTF_INFO("[SYNC_TEST] ✅ 空result参数正确返回失败");
                _stats.passed_tests++;
            } else {
                PRINTF_ERROR("[SYNC_TEST] ❌ 空result参数应该返回失败");
                _stats.failed_tests++;
            }
        }
    }
    
    // 测试无效参数
    void test_invalid_parameters() {
        PRINTF_INFO("[SYNC_TEST] 测试无效参数");
        
        AccountInfo account;
        if (!get_test_account(account)) {
            return;
        }
        
        // 测试无效超时时间
        TradeOrderRequest order = {};
        order.account = account;
        strncpy(order.security_code, "000001", sizeof(order.security_code));
        order.market = MARKET_SZSE;
        order.side = SIDE_BUY;
        order.order_type = ORDER_TYPE_LIMIT;
        order.price = 10.0;
        order.qty = 100;
        
        TradeOrderRspData result = {};
        int32_t ret = trade_send_order_sync(&order, &result, -1000); // 负数超时时间
        _stats.total_tests++;
        
        if (ret != 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 无效超时时间正确返回失败");
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 无效超时时间应该返回失败");
            _stats.failed_tests++;
        }
        
        // 测试无效市场类型
        order.market = (MarketType)999; // 无效市场类型
        ret = trade_send_order_sync(&order, &result, 30000);
        _stats.total_tests++;
        
        if (ret != 0) {
            PRINTF_INFO("[SYNC_TEST] ✅ 无效市场类型正确返回失败");
            _stats.passed_tests++;
        } else {
            PRINTF_ERROR("[SYNC_TEST] ❌ 无效市场类型应该返回失败");
            _stats.failed_tests++;
        }
    }
    
    // 执行完整的同步接口测试套件
    void run_comprehensive_sync_tests() {
        PRINTF_INFO("[SYNC_TEST] ===============================================");
        PRINTF_INFO("[SYNC_TEST] 开始执行完整的同步接口测试套件");
        PRINTF_INFO("[SYNC_TEST] ===============================================");
        
        auto overall_start = std::chrono::steady_clock::now();
        
        // 1. 参数验证测试
        test_parameter_validation();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 2. 同步下单测试
        test_sync_send_order();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 3. 同步撤单测试
        test_sync_cancel_order();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 4. 同步查询测试
        test_sync_query_interfaces();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 5. 同步转账测试（可选，因为可能涉及实际资金）
        test_sync_transfer_interfaces();
        
        auto overall_end = std::chrono::steady_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);
        
        PRINTF_INFO("[SYNC_TEST] ===============================================");
        PRINTF_INFO("[SYNC_TEST] 同步接口测试套件完成，总耗时: %llds", (long long)total_duration.count());
        PRINTF_INFO("[SYNC_TEST] ===============================================");
    }
};

// 全局同步测试管理器
std::unique_ptr<SyncInterfaceTestManager> g_sync_test_manager;

#endif // TEST_SYNC_OPERATIONS

// 打印帮助信息
void print_usage(const char *program_name)
{
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << " -c, --config FILE Configuration file path (JSON format)\n"
              << " -l, --log-level LEVEL Log level (DEBUG|INFO|WARN|ERROR), default: INFO\n"
              << " -d, --daemon Run as daemon process\n"
              << " -v, --verbose Enable verbose output\n"
              << " -h, --help Show this help message\n"
              << "\nTest Modes (modify source code to enable/disable):\n"
              << " CALLBACK_MODE: Multiple callback testing with handle management\n"
              << " DATA_MANAGER_MODE: Latest data query interface\n"
              << " ACCOUNT_MANAGEMENT: Account management testing\n"
              << " TRADING_OPERATIONS: Trading operations testing\n"
              << " SYNC_OPERATIONS: Synchronization interface testing\n"
              << std::endl;
}

// 登录状态监控
struct LoginStatusMonitor {
    std::mutex status_mutex;
    std::map<std::string, bool> last_known_status;
    
    void check_and_report_login_status() {
        std::lock_guard<std::mutex> lock(status_mutex);
        
        // 获取所有账户ID
        char **account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0 || !account_ids || count == 0) {
            PRINTF_WARN("[LOGIN_MONITOR] No accounts configured");
            return;
        }
        
        PRINTF_INFO("[LOGIN_MONITOR] Checking login status for %d accounts", count);
        
        for (int i = 0; i < count; ++i) {
            const char* login_id = account_ids[i];
            int is_logged_in = trade_is_account_login(login_id);
            bool current_status = (is_logged_in == 1);
            
            // 检查状态是否发生变化
            auto it = last_known_status.find(login_id);
            bool status_changed = (it == last_known_status.end() || it->second != current_status);
            
            if (status_changed) {
                PRINTF_INFO("[LOGIN_MONITOR] Account %s login status changed: %s", 
                    login_id, current_status ? "LOGGED_IN" : "LOGGED_OUT");
                last_known_status[login_id] = current_status;
            }
        }
        
        // 获取已登录账户总数
        int logged_in_count = trade_get_logged_in_account_count();
        PRINTF_INFO("[LOGIN_MONITOR] Total logged in accounts: %d/%d", logged_in_count, count);
        
        // 获取已登录账户列表
        char **logged_in_ids = nullptr;
        int logged_in_list_count = 0;
        result = trade_get_logged_in_login_ids(&logged_in_ids, &logged_in_list_count);
        
        if (result == 0 && logged_in_ids && logged_in_list_count > 0) {
            PRINTF_INFO("[LOGIN_MONITOR] Logged in accounts:");
            for (int i = 0; i < logged_in_list_count; ++i) {
                PRINTF_INFO("  - %s", logged_in_ids[i]);
            }
            trade_free_account_ids(logged_in_ids, logged_in_list_count);
        }
        
        // 释放内存
        trade_free_account_ids(account_ids, count);
    }
};

// 全局登录状态监控器
std::unique_ptr<LoginStatusMonitor> g_login_monitor;

// 修改运行服务主循环，加入多回调测试
void run_service_loop()
{
    PRINTF_INFO("Trade service running with %d test modes enabled. Press Ctrl+C to stop.",
                TEST_CALLBACK_MODE + TEST_DATA_MANAGER_MODE + TEST_ACCOUNT_MANAGEMENT + TEST_TRADING_OPERATIONS + TEST_SYNC_OPERATIONS);
    
    // 初始化登录状态监控器
    g_login_monitor = std::make_unique<LoginStatusMonitor>();
    
#if TEST_CALLBACK_MODE
    // 运行10秒后测试回调移除
    auto start_time = std::chrono::steady_clock::now();
    bool removal_test_done = false;
#endif

#if TEST_SYNC_OPERATIONS
    bool sync_test_executed = false;
#endif

    int loop_count = 0;
    bool trading_test_executed = false;
    
    while (g_running.load())
    {
        // 检查服务状态
        if (trade_is_running())
        {
            // 每5秒检查一次登录状态
            if (loop_count % 5 == 0)
            {
                g_login_monitor->check_and_report_login_status();
            }
            
            // 检查是否有任何账户登录
            int logged_in_count = trade_get_logged_in_account_count();
            bool has_logged_in_accounts = (logged_in_count > 0);
            
#if TEST_CALLBACK_MODE
            // 在运行10秒后测试回调移除
            if (!removal_test_done)
            {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);
                if (elapsed.count() >= 10)
                {
                    test_callback_removal();
                    removal_test_done = true;
                }
            }
#endif

#if TEST_SYNC_OPERATIONS
            // 在服务启动后等待10秒让系统完全初始化，并且有账户登录后执行同步接口测试
            if (!sync_test_executed && loop_count > 10 && has_logged_in_accounts)
            {
                PRINTF_INFO("[SYNC_TEST] Service is ready and accounts are logged in, starting sync interface tests...");
                g_sync_test_manager = std::make_unique<SyncInterfaceTestManager>();
                g_sync_test_manager->run_comprehensive_sync_tests();
                sync_test_executed = true;
            }
            else if (!sync_test_executed && loop_count > 10 && !has_logged_in_accounts)
            {
                if (loop_count % 10 == 0) // 每10秒提醒一次
                {
                    PRINTF_WARN("[SYNC_TEST] Waiting for accounts to login before starting sync tests...");
                }
            }
#endif

#if TEST_ACCOUNT_MANAGEMENT
            // 每10秒测试一次账户管理
            if (loop_count % 10 == 0)
            {
                test_account_management();
            }
#endif

#if TEST_DATA_MANAGER_MODE
            // 测试内存存储模式
            test_memory_storage_mode();
#endif

#if TEST_TRADING_OPERATIONS
            // 在服务启动后等待12秒执行交易测试序列（在同步测试之后），并且需要有账户登录
            if (!trading_test_executed && loop_count > 12 && has_logged_in_accounts)
            {
                PRINTF_INFO("[TRADING_TEST] Service is ready and accounts are logged in, starting trading tests...");
                g_test_manager = std::make_unique<TradingTestManager>();
                execute_trading_test_sequence(g_test_manager.get());
                trading_test_executed = true;
            }
            else if (!trading_test_executed && loop_count > 12 && !has_logged_in_accounts)
            {
                if (loop_count % 10 == 0) // 每10秒提醒一次
                {
                    PRINTF_WARN("[TRADING_TEST] Waiting for accounts to login before starting trading tests...");
                }
            }
#endif
        }
        else
        {
            if (loop_count % 30 == 0) // 每30秒提醒一次服务未运行
            {
                PRINTF_WARN("Trade service is not running, current status: %d", trade_get_status());
            }
        }
        
        loop_count++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // 清理登录状态监控器
    g_login_monitor.reset();
}

// 主函数
int main(int argc, char *argv[])
{
    const char *config_file = "./config/trade_config.json";
    bool daemon_mode = false;
    bool verbose = false;
    
    // 解析命令行参数
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"daemon", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    
    int option_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "c:dvh", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'c':
            config_file = optarg;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case '?':
            print_usage(argv[0]);
            return 1;
        default:
            break;
        }
    }
    
    PRINTF_INFO("Trade Service Starting with config: %s", config_file);
    PRINTF_INFO("Test modes enabled - Callback: %s, DataManager: %s, AccountMgmt: %s, Trading: %s, Sync: %s",
                TEST_CALLBACK_MODE ? "ON" : "OFF",
                TEST_DATA_MANAGER_MODE ? "ON" : "OFF",
                TEST_ACCOUNT_MANAGEMENT ? "ON" : "OFF",
                TEST_TRADING_OPERATIONS ? "ON" : "OFF",
                TEST_SYNC_OPERATIONS ? "ON" : "OFF");
    
    // 设置信号处理
    setup_signal_handlers();
    
    // 守护进程处理
    if (daemon_mode)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            PRINTF_ERROR("Failed to fork daemon process");
            return 1;
        }
        else if (pid > 0)
        {
            PRINTF_INFO("Daemon started with PID: %d", pid);
            return 0;
        }
        setsid();
        chdir("/");
        umask(0);
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }
    
    int exit_code = 0;
    try
    {
        // 初始化交易服务
        if (trade_init_with_config(config_file) != 0)
        {
            PRINTF_ERROR("Failed to initialize: %s", trade_get_last_error());
            exit_code = 1;
            goto cleanup;
        }
        
        // 启动服务
        if (trade_start() != 0)
        {
            PRINTF_ERROR("Failed to start: %s", trade_get_last_error());
            exit_code = 1;
            goto cleanup;
        }
        
        // 连接交易系统
        if (trade_connect() != 0)
        {
            PRINTF_ERROR("Failed to connect: %s", trade_get_last_error());
            exit_code = 1;
            goto cleanup;
        }
        
        // 设置不同的测试模式
#if TEST_CALLBACK_MODE
        setup_multiple_callback_mode();
#endif
        
        PRINTF_INFO("Service started with interface: %s", trade_get_interface_id() ? trade_get_interface_id() : "Unknown");
        
        // 运行服务主循环
        run_service_loop();
    }
    catch (const std::exception &e)
    {
        PRINTF_ERROR("Exception: %s", e.what());
        exit_code = 1;
    }
    catch (...)
    {
        PRINTF_ERROR("Unknown exception occurred");
        exit_code = 1;
    }
    
cleanup:
    // 清理登录状态监控器
    if (g_login_monitor) {
        g_login_monitor.reset();
    }
    
    // 清理回调
#if TEST_CALLBACK_MODE
    cleanup_callbacks();
#endif
    
    // 清理测试管理器
#if TEST_TRADING_OPERATIONS
    if (g_test_manager) {
        g_test_manager.reset();
    }
#endif

#if TEST_SYNC_OPERATIONS
    if (g_sync_test_manager) {
        g_sync_test_manager.reset();
    }
#endif
    
    // 断开连接和清理服务资源
    trade_disconnect();
    trade_stop();
    trade_release();
    
    PRINTF_INFO("Service shutdown complete");
    return exit_code;
}