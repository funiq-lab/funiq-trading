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
#include "laserpants/dotenv/dotenv.h"

#include "quote/quote_interface.h"
#include "types/quote_types.h"
#include "data/shared_memory.h"
#include "core/macros.h"

// 全局变量
std::atomic<bool> g_running(true);

// 全局句柄存储，用于多回调测试
std::vector<IndexDataCallbackHandle> g_index_handles;
std::vector<DepthDataCallbackHandle> g_depth_handles;
std::vector<DepthFiftyDataCallbackHandle> g_depth_fifty_handles;
std::vector<TradeDataCallbackHandle> g_trade_handles;
std::vector<EntrustDataCallbackHandle> g_entrust_handles;
std::vector<StatusCallbackHandle> g_status_handles;

// 测试模式配置 - 可以通过注释来切换测试不同模式
#define TEST_CALLBACK_MODE          1  // 回调模式
#define TEST_DATA_MANAGER_MODE      0  // 内存存储模式
#define TEST_SUBSCRIPTION_MODE      1  // 订阅管理测试

// 时间戳打印函数
void print_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::tm* tm_now = std::localtime(&time_t);
    printf("[%04d-%02d-%02d %02d:%02d:%02d.%03d] ", 
           tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
           tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, (int)ms.count());
}

// 简化的日志宏定义
#define PRINTF_INFO(...) do { print_timestamp(); printf("[INFO] " __VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define PRINTF_ERROR(...) do { print_timestamp(); printf("[ERROR] " __VA_ARGS__); printf("\n"); fflush(stderr); } while(0)
#define PRINTF_DEBUG(...) do { print_timestamp(); printf("[DEBUG] " __VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define PRINTF_WARN(...) do { print_timestamp(); printf("[WARN] " __VA_ARGS__); printf("\n"); fflush(stdout); } while(0)

// 信号处理函数
void signal_handler(int signal) {
    PRINTF_INFO("Received signal %d, shutting down...", signal);
    g_running.store(false);
}

// 设置信号处理
void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGHUP, SIG_IGN);
}

#if TEST_CALLBACK_MODE
// ===== 多个Index回调函数 =====
void on_index_data_callback_1(const IndexData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-1] Index - Code: %s, LastIndex: %.2f, Volume: %llu", 
                data->security_info.code, data->last_index, 
                (unsigned long long)data->total_volume);
}

void on_index_data_callback_2(const IndexData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-2] Index - Code: %s, PreClose: %.2f->%.2f, Change: %.2f", 
                data->security_info.code, data->pre_close_index, data->last_index,
                data->last_index - data->pre_close_index);
}

void on_index_data_callback_3(const IndexData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-3] Index - Code: %s, High: %.2f, Low: %.2f, Trades: %llu", 
                data->security_info.code, data->high_index, data->low_index,
                (unsigned long long)data->num_trades);
}

// ===== 多个Depth回调函数 =====
void on_depth_data_callback_1(const DepthData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-1] Depth - Code: %s, Price: %.3f, Bid1: %.3f(%llu), Ask1: %.3f(%llu)",
                data->security_info.code, data->last_price, 
                data->bid_price[0], (unsigned long long)data->bid_qty[0],
                data->ask_price[0], (unsigned long long)data->ask_qty[0]);
}

void on_depth_data_callback_2(const DepthData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-2] Depth - Code: %s, Volume: %llu, Value: %llu, Trades: %llu",
                data->security_info.code, (unsigned long long)data->total_volume,
                (unsigned long long)data->total_value, (unsigned long long)data->num_trades);
}

// ===== 多个DepthFifty回调函数 =====
void on_depth_fifty_callback_1(const DepthFiftyData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-1] DepthFifty - Code: %s, BidPrice: %.3f, AskPrice: %.3f",
                data->security_info.code, data->bid_price, data->ask_price);
}

void on_depth_fifty_callback_2(const DepthFiftyData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-2] DepthFifty - Code: %s, Timestamp: %llu",
                data->security_info.code, (unsigned long long)data->timestamp);
}

// ===== 多个Trade回调函数 =====
void on_trade_data_callback_1(const TradeData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-1] Trade - Code: %s, ID: %llu, Price: %.3f, Qty: %llu",
                data->security_info.code, (unsigned long long)data->trade_id,
                data->price, (unsigned long long)data->qty);
}

void on_trade_data_callback_2(const TradeData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-2] Trade - Code: %s, BidOrder: %llu, AskOrder: %llu, Flag: %c",
                data->security_info.code, (unsigned long long)data->bid_order_no,
                (unsigned long long)data->ask_order_no, data->trade_flag);
}

void on_trade_data_callback_3(const TradeData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-3] Trade - Code: %s, Index: %u, Value: %llu, ExecType: %c",
                data->security_info.code, data->trade_index,
                (unsigned long long)data->value, data->exec_type);
}

// ===== 多个Entrust回调函数 =====
void on_entrust_data_callback_1(const EntrustData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-1] Entrust - Code: %s, OrderNo: %llu, Type: %c, Side: %c",
                data->security_info.code, (unsigned long long)data->order_no, 
                data->order_type, data->side);
}

void on_entrust_data_callback_2(const EntrustData* data) {
    if (!data) return;
    PRINTF_DEBUG("[CALLBACK-2] Entrust - Code: %s, Price: %.3f, Qty: %llu, Balance: %llu",
                data->security_info.code, data->price, 
                (unsigned long long)data->qty, (unsigned long long)data->balance);
}

// ===== 状态回调函数 =====
void on_status_callback_1(Status status, const char* message) {
    PRINTF_INFO("[STATUS-1] Status: %d, Message: %s", static_cast<int>(status), 
                message ? message : "NULL");
}

void on_status_callback_2(Status status, const char* message) {
    PRINTF_INFO("[STATUS-2] Status: %d, Message: %s", static_cast<int>(status), 
                message ? message : "NULL");
}

// ===== 设置多回调模式 =====
void setup_multiple_callback_mode() {
    PRINTF_INFO("Setting up MULTIPLE CALLBACK mode...");
    
    // 设置多个Index回调
    PRINTF_INFO("Setting up Index callbacks...");
    IndexDataCallbackHandle idx_handle_1 = quote_set_index_data_callback(on_index_data_callback_1);
    IndexDataCallbackHandle idx_handle_2 = quote_set_index_data_callback(on_index_data_callback_2);
    IndexDataCallbackHandle idx_handle_3 = quote_set_index_data_callback(on_index_data_callback_3);
    
    g_index_handles.push_back(idx_handle_1);
    g_index_handles.push_back(idx_handle_2);
    g_index_handles.push_back(idx_handle_3);
    
    PRINTF_INFO("Index callbacks registered - Handle1: %s, Handle2: %s, Handle3: %s",
                idx_handle_1 ? "OK" : "FAIL",
                idx_handle_2 ? "OK" : "FAIL", 
                idx_handle_3 ? "OK" : "FAIL");
    
    // 设置多个Depth回调
    PRINTF_INFO("Setting up Depth callbacks...");
    DepthDataCallbackHandle depth_handle_1 = quote_set_depth_data_callback(on_depth_data_callback_1);
    DepthDataCallbackHandle depth_handle_2 = quote_set_depth_data_callback(on_depth_data_callback_2);
    
    g_depth_handles.push_back(depth_handle_1);
    g_depth_handles.push_back(depth_handle_2);
    
    PRINTF_INFO("Depth callbacks registered - Handle1: %s, Handle2: %s",
                depth_handle_1 ? "OK" : "FAIL",
                depth_handle_2 ? "OK" : "FAIL");
    
    // 设置多个DepthFifty回调
    PRINTF_INFO("Setting up DepthFifty callbacks...");
    DepthFiftyDataCallbackHandle depth_fifty_handle_1 = quote_set_depth_fifty_data_callback(on_depth_fifty_callback_1);
    DepthFiftyDataCallbackHandle depth_fifty_handle_2 = quote_set_depth_fifty_data_callback(on_depth_fifty_callback_2);
    
    g_depth_fifty_handles.push_back(depth_fifty_handle_1);
    g_depth_fifty_handles.push_back(depth_fifty_handle_2);
    
    PRINTF_INFO("DepthFifty callbacks registered - Handle1: %s, Handle2: %s",
                depth_fifty_handle_1 ? "OK" : "FAIL",
                depth_fifty_handle_2 ? "OK" : "FAIL");
    
    // 设置多个Trade回调
    PRINTF_INFO("Setting up Trade callbacks...");
    TradeDataCallbackHandle trade_handle_1 = quote_set_trade_data_callback(on_trade_data_callback_1);
    TradeDataCallbackHandle trade_handle_2 = quote_set_trade_data_callback(on_trade_data_callback_2);
    TradeDataCallbackHandle trade_handle_3 = quote_set_trade_data_callback(on_trade_data_callback_3);
    
    g_trade_handles.push_back(trade_handle_1);
    g_trade_handles.push_back(trade_handle_2);
    g_trade_handles.push_back(trade_handle_3);
    
    PRINTF_INFO("Trade callbacks registered - Handle1: %s, Handle2: %s, Handle3: %s",
                trade_handle_1 ? "OK" : "FAIL",
                trade_handle_2 ? "OK" : "FAIL",
                trade_handle_3 ? "OK" : "FAIL");
    
    // 设置多个Entrust回调
    PRINTF_INFO("Setting up Entrust callbacks...");
    EntrustDataCallbackHandle entrust_handle_1 = quote_set_entrust_data_callback(on_entrust_data_callback_1);
    EntrustDataCallbackHandle entrust_handle_2 = quote_set_entrust_data_callback(on_entrust_data_callback_2);
    
    g_entrust_handles.push_back(entrust_handle_1);
    g_entrust_handles.push_back(entrust_handle_2);
    
    PRINTF_INFO("Entrust callbacks registered - Handle1: %s, Handle2: %s",
                entrust_handle_1 ? "OK" : "FAIL",
                entrust_handle_2 ? "OK" : "FAIL");
    
    // 设置多个状态回调
    PRINTF_INFO("Setting up Status callbacks...");
    StatusCallbackHandle status_handle_1 = quote_set_status_callback(on_status_callback_1);
    StatusCallbackHandle status_handle_2 = quote_set_status_callback(on_status_callback_2);
    
    g_status_handles.push_back(status_handle_1);
    g_status_handles.push_back(status_handle_2);
    
    PRINTF_INFO("Status callbacks registered - Handle1: %s, Handle2: %s",
                status_handle_1 ? "OK" : "FAIL",
                status_handle_2 ? "OK" : "FAIL");
    
    // 测试空回调
    PRINTF_INFO("Testing NULL callback...");
    IndexDataCallbackHandle null_handle = quote_set_index_data_callback(nullptr);
    PRINTF_INFO("NULL callback handle: %s", null_handle ? "UNEXPECTED_OK" : "EXPECTED_FAIL");
    
    PRINTF_INFO("MULTIPLE CALLBACK mode setup complete!");
    PRINTF_INFO("Total callbacks registered - Index: %zu, Depth: %zu, DepthFifty: %zu, Trade: %zu, Entrust: %zu, Status: %zu",
                g_index_handles.size(), g_depth_handles.size(), g_depth_fifty_handles.size(),
                g_trade_handles.size(), g_entrust_handles.size(), g_status_handles.size());
}

// ===== 测试回调移除功能 =====
void test_callback_removal() {
    PRINTF_INFO("=== Starting callback removal test ===");
    
    // 等待5秒观察所有回调工作
    PRINTF_INFO("Observing all callbacks for 5 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 移除第一个Index回调
    if (!g_index_handles.empty() && g_index_handles[0]) {
        PRINTF_INFO("Removing first Index callback...");
        int result = quote_remove_index_data_callback(g_index_handles[0]);
        PRINTF_INFO("Remove result: %s", result == 0 ? "SUCCESS" : "FAILED");
        g_index_handles[0] = IndexDataCallbackHandle{}; // 清空句柄
    }
    
    // 等待3秒观察效果
    PRINTF_INFO("Observing after removing first Index callback for 3 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 移除所有Trade回调
    PRINTF_INFO("Removing all Trade callbacks...");
    for (size_t i = 0; i < g_trade_handles.size(); ++i) {
        if (g_trade_handles[i]) {
            int result = quote_remove_trade_data_callback(g_trade_handles[i]);
            PRINTF_INFO("Remove Trade callback %zu: %s", i+1, result == 0 ? "SUCCESS" : "FAILED");
            g_trade_handles[i] = TradeDataCallbackHandle{}; // 清空句柄
        }
    }
    
    // 等待3秒观察效果
    PRINTF_INFO("Observing after removing all Trade callbacks for 3 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 移除第一个Status回调
    if (!g_status_handles.empty() && g_status_handles[0]) {
        PRINTF_INFO("Removing first Status callback...");
        int result = quote_remove_status_callback(g_status_handles[0]);
        PRINTF_INFO("Remove Status callback result: %s", result == 0 ? "SUCCESS" : "FAILED");
        g_status_handles[0] = StatusCallbackHandle{}; // 清空句柄
    }
    
    PRINTF_INFO("=== Callback removal test completed ===");
}

// ===== 清理所有回调 =====
void cleanup_callbacks() {
    PRINTF_INFO("Cleaning up all remaining callbacks...");
    
    // 清理Index回调
    for (auto& handle : g_index_handles) {
        if (handle) {
            quote_remove_index_data_callback(handle);
        }
    }
    g_index_handles.clear();
    
    // 清理Depth回调
    for (auto& handle : g_depth_handles) {
        if (handle) {
            quote_remove_depth_data_callback(handle);
        }
    }
    g_depth_handles.clear();
    
    // 清理DepthFifty回调
    for (auto& handle : g_depth_fifty_handles) {
        if (handle) {
            quote_remove_depth_fifty_data_callback(handle);
        }
    }
    g_depth_fifty_handles.clear();
    
    // 清理Trade回调
    for (auto& handle : g_trade_handles) {
        if (handle) {
            quote_remove_trade_data_callback(handle);
        }
    }
    g_trade_handles.clear();
    
    // 清理Entrust回调
    for (auto& handle : g_entrust_handles) {
        if (handle) {
            quote_remove_entrust_data_callback(handle);
        }
    }
    g_entrust_handles.clear();
    
    // 清理Status回调
    for (auto& handle : g_status_handles) {
        if (handle) {
            quote_remove_status_callback(handle);
        }
    }
    g_status_handles.clear();
    
    PRINTF_INFO("All callbacks cleaned up!");
}

#endif

#if TEST_DATA_MANAGER_MODE
// ===== 模式2: 数据管理模式 =====
void test_memory_storage_mode() {
    // 获取最新指数数据
    IndexData index_data;
    int index_result = quote_get_index_data(&index_data);
    if (index_result > 0) {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Index Data - Code: %s, LastIndex: %.2f", 
                   index_data.security_info.code, index_data.last_index);
    }
    
    // 获取最新深度数据
    DepthData depth_data;
    int depth_result = quote_get_depth_data(&depth_data);
    if (depth_result > 0) {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Depth Data - Code: %s, LastPrice: %.3f", 
                   depth_data.security_info.code, depth_data.last_price);
    }
    
    // 获取最新深度50档数据
    DepthFiftyData depth_fifty_data;
    int depth_fifty_result = quote_get_depth_fifty_data(&depth_fifty_data);
    if (depth_fifty_result > 0) {
        PRINTF_INFO("[MEMORY_STORAGE] Latest DepthFifty Data - Code: %s, BidPrice: %.3f, AskPrice: %.3f", 
                   depth_fifty_data.security_info.code, depth_fifty_data.bid_price, depth_fifty_data.ask_price);
    }
    
    // 获取最新成交数据
    TradeData trade_data;
    int trade_result = quote_get_trade_data(&trade_data);
    if (trade_result > 0) {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Trade Data - Code: %s, Price: %.3f, Qty: %llu", 
                   trade_data.security_info.code, trade_data.price, 
                   (unsigned long long)trade_data.qty);
    }

    // 获取最新委托数据
    EntrustData entrust_data;
    int entrust_result = quote_get_entrust_data(&entrust_data);
    if (entrust_result > 0) {
        PRINTF_INFO("[MEMORY_STORAGE] Latest Entrust Data - Code: %s, OrderNo: %llu, Price: %.3f, Side: %c", 
                   entrust_data.security_info.code, (unsigned long long)entrust_data.order_no, 
                   entrust_data.price, entrust_data.side);
    }
}
#endif

#if TEST_SUBSCRIPTION_MODE
// ===== 订阅管理测试模式 =====
void test_subscription_management() {
    PRINTF_INFO("[SUBSCRIPTION_TEST] Testing subscription management...");
    
    // 测试添加市场订阅
    MarketType markets[] = {MARKET_SSE, MARKET_SZSE};
    int market_count = sizeof(markets) / sizeof(markets[0]);
    int result = quote_add_markets(markets, market_count);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Added markets: %d/%d", result, market_count);
    
    // 测试添加数据类型订阅
    FTDataType data_types[] = {DATA_TYPE_INDEX, DATA_TYPE_DEPTH, DATA_TYPE_TRADE};
    int data_type_count = sizeof(data_types) / sizeof(data_types[0]);
    result = quote_add_data_types(data_types, data_type_count);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Added data types: %d/%d", result, data_type_count);
    
    // 测试添加证券订阅
    SecurityInfo securities[3];
    memset(securities, 0, sizeof(securities));
    strncpy(securities[0].code, "000001", sizeof(securities[0].code) - 1);
    securities[0].market = MARKET_SZSE;
    strncpy(securities[1].code, "600000", sizeof(securities[1].code) - 1);
    securities[1].market = MARKET_SSE;
    strncpy(securities[2].code, "399001", sizeof(securities[2].code) - 1);
    securities[2].market = MARKET_SZSE;
    
    int security_count = sizeof(securities) / sizeof(securities[0]);
    result = quote_add_securities(securities, security_count);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Added securities: %d/%d", result, security_count);
    
    // 测试订阅状态查询
    for (int i = 0; i < market_count; ++i) {
        int subscribed = quote_is_market_subscribed(markets[i]);
        PRINTF_INFO("[SUBSCRIPTION_TEST] Market %d subscribed: %s", markets[i], subscribed ? "YES" : "NO");
    }
    
    for (int i = 0; i < data_type_count; ++i) {
        int subscribed = quote_is_data_type_subscribed(data_types[i]);
        PRINTF_INFO("[SUBSCRIPTION_TEST] DataType %d subscribed: %s", data_types[i], subscribed ? "YES" : "NO");
    }
    
    for (int i = 0; i < security_count; ++i) {
        int subscribed = quote_is_code_subscribed(securities[i].code);
        PRINTF_INFO("[SUBSCRIPTION_TEST] Code %s subscribed: %s", securities[i].code, subscribed ? "YES" : "NO");
    }
    
    // 测试数据处理判断
    for (int i = 0; i < security_count; ++i) {
        for (int j = 0; j < data_type_count; ++j) {
            int should_process = quote_should_process_data(securities[i].market, data_types[j], securities[i].code);
            PRINTF_INFO("[SUBSCRIPTION_TEST] Should process %s/%d: %s", 
                       securities[i].code, data_types[j], should_process ? "YES" : "NO");
        }
    }
    
    // 等待一段时间观察效果
    PRINTF_INFO("[SUBSCRIPTION_TEST] Waiting 10 seconds to observe data...");
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 测试移除部分订阅
    PRINTF_INFO("[SUBSCRIPTION_TEST] Testing subscription removal...");
    
    // 移除一个市场
    MarketType remove_markets[] = {MARKET_SSE};
    result = quote_remove_markets(remove_markets, 1);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Removed markets: %d/1", result);
    
    // 移除一个数据类型
    FTDataType remove_data_types[] = {DATA_TYPE_TRADE};
    result = quote_remove_data_types(remove_data_types, 1);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Removed data types: %d/1", result);
    
    // 移除一个证券
    SecurityInfo remove_securities[1];
    memset(remove_securities, 0, sizeof(remove_securities));
    strncpy(remove_securities[0].code, "600000", sizeof(remove_securities[0].code) - 1);
    remove_securities[0].market = MARKET_SSE;
    result = quote_remove_securities(remove_securities, 1);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Removed securities: %d/1", result);
    
    // 再次检查订阅状态
    PRINTF_INFO("[SUBSCRIPTION_TEST] Checking subscription status after removal...");
    int subscribed = quote_is_market_subscribed(MARKET_SSE);
    PRINTF_INFO("[SUBSCRIPTION_TEST] Market SSE subscribed: %s", subscribed ? "YES" : "NO");
    
    subscribed = quote_is_data_type_subscribed(DATA_TYPE_TRADE);
    PRINTF_INFO("[SUBSCRIPTION_TEST] DataType TRADE subscribed: %s", subscribed ? "YES" : "NO");
    
    subscribed = quote_is_code_subscribed("600000");
    PRINTF_INFO("[SUBSCRIPTION_TEST] Code 600000 subscribed: %s", subscribed ? "YES" : "NO");
    
    PRINTF_INFO("[SUBSCRIPTION_TEST] Subscription management test completed");
}
#endif

// 打印帮助信息
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -c, --config FILE     Configuration file path (JSON format)\n"
              << "  -l, --log-level LEVEL Log level (DEBUG|INFO|WARN|ERROR), default: INFO\n"
              << "  -d, --daemon          Run as daemon process\n"
              << "  -v, --verbose         Enable verbose output\n"
              << "  -h, --help            Show this help message\n"
              << "\nTest Modes (modify source code to enable/disable):\n"
              << "  CALLBACK_MODE:        Multiple callback testing with handle management\n"
              << "  DATA_MANAGER_MODE:    Latest data query interface\n"
              << "  SUBSCRIPTION_MODE:    Subscription management testing\n"
              << std::endl;
}

// 运行服务主循环
void run_service_loop() {
    PRINTF_INFO("Quote service running with %d test modes enabled. Press Ctrl+C to stop.",
                TEST_CALLBACK_MODE + TEST_DATA_MANAGER_MODE + TEST_SUBSCRIPTION_MODE);
    
#if TEST_CALLBACK_MODE
    // 运行10秒后测试回调移除
    auto start_time = std::chrono::steady_clock::now();
    bool removal_test_done = false;
#endif

#if TEST_SUBSCRIPTION_MODE
    bool subscription_test_done = false;
#endif
    
    int loop_count = 0;
    
    while (g_running.load()) {        
        // 检查服务状态
        if (quote_is_running()) {
            
#if TEST_CALLBACK_MODE
            // 在运行10秒后测试回调移除
            if (!removal_test_done) {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);
                if (elapsed.count() >= 10) {
                    test_callback_removal();
                    removal_test_done = true;
                }
            }
#endif

#if TEST_SUBSCRIPTION_MODE
            // 在服务启动后等待5秒执行订阅测试
            if (!subscription_test_done && loop_count > 5 && quote_is_connected()) {
                PRINTF_INFO("[SUBSCRIPTION_TEST] Service is ready, starting subscription tests...");
                test_subscription_management();
                subscription_test_done = true;
            }
#endif
            
#if TEST_DATA_MANAGER_MODE
            // 测试内存存储模式
            test_memory_storage_mode();
#endif
        }
        
        loop_count++;
        // 主循环延迟
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// 主函数
int main(int argc, char* argv[]) {
    const char* config_file = "./config/quote_config.json";
    bool daemon_mode = false;
    bool verbose = false;
    
    // 解析命令行参数
    static struct option long_options[] = {
        {"config",    required_argument, 0, 'c'},
        {"daemon",    no_argument,       0, 'd'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "c:dvh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c': config_file = optarg; break;
            case 'v': verbose = true; break;
            case 'h': print_usage(argv[0]); return 0;
            case '?': print_usage(argv[0]); return 1;
            default: break;
        }
    }
    
    PRINTF_INFO("Quote Service Starting with config: %s", config_file);
    PRINTF_INFO("Test modes enabled - Callback: %s, DataManager: %s, Subscription: %s",
                TEST_CALLBACK_MODE ? "ON" : "OFF",
                TEST_DATA_MANAGER_MODE ? "ON" : "OFF",
                TEST_SUBSCRIPTION_MODE ? "ON" : "OFF");
    
    // 设置信号处理
    setup_signal_handlers();
    
    // 守护进程处理
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            PRINTF_ERROR("Failed to fork daemon process");
            return 1;
        } else if (pid > 0) {
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
    
    try {
        // 初始化行情服务
        if (quote_init() != 0) {
            PRINTF_ERROR("Failed to initialize: %s", quote_get_last_error());
            exit_code = 1;
            goto cleanup;
        }
        
        // 启动服务
        if (quote_start() != 0) {
            PRINTF_ERROR("Failed to start: %s", quote_get_last_error());
            exit_code = 1;
            goto cleanup;
        }
        
        // 设置不同的测试模式
#if TEST_CALLBACK_MODE
        setup_multiple_callback_mode();
#endif
        
        PRINTF_INFO("Service started with parser: %s", quote_get_parser_id() ? quote_get_parser_id() : "Unknown");
        
        // 运行服务主循环
        run_service_loop();
        
    } catch (const std::exception& e) {
        PRINTF_ERROR("Exception: %s", e.what());
        exit_code = 1;
    } catch (...) {
        PRINTF_ERROR("Unknown exception occurred");
        exit_code = 1;
    }
    
cleanup:
    // 清理回调
#if TEST_CALLBACK_MODE
    cleanup_callbacks();
#endif
    
    // 取消订阅和清理服务资源
    quote_unsubscribe();
    quote_stop();
    quote_release();
    
    PRINTF_INFO("Service shutdown complete");
    return exit_code;
}
