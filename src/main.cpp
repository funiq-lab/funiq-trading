#include <iostream>
#include <memory>
#include <signal.h>
#include <atomic>
#include <ctime>
#include <getopt.h>
#include <sys/stat.h>
#include <thread>
#include "laserpants/dotenv/dotenv.h"
#include <nlohmann/json.hpp>
#include "core/macros.h"
#include "types/strategy_types.h"
#include "utils/cron_scheduler.h"
#include "utils/auto_scheduler.h"
#include "utils/trading_time.h"
#include "database/database_manager.hpp"
#include "quote/quote_interface.h"
#include "trade/trade_interface.h"
#include "quote/quote_parser.h"
#include "trade/trade_handler.h"
#include "strategy/strategy_interface.h"
#include "network/network_service.h"
#include "network/handlers/http/system_api_handler.h"
#include "network/handlers/http/trade_api_handler.h"
#include "network/handlers/http/quote_api_handler.h"
#include "network/handlers/http/strategy_api_handler.h"


std::atomic<bool> g_running(true);
std::unique_ptr<ft::network::NetworkService> g_network_service;
std::unique_ptr<ft::DatabaseManager> g_db_manager;
std::unique_ptr<ft::CronScheduler> g_cron_scheduler;
std::unique_ptr<ft::AutoScheduler> g_auto_scheduler;
std::unique_ptr<ft::TradingTimeManager> g_trading_time_manager;
std::shared_ptr<ft::network::TradeApiHandler> g_trade_handler_instance;

void register_api_handlers();

// 信号处理函数
void signal_handler(int signal)
{
    NETWORK_LOG_INFO("Received signal {}, shutting down all services...", signal);
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

// 检查文件是否存在
bool file_exists(const std::string &filename)
{
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

// 初始化数据库服务
bool init_database_service()
{
    NETWORK_LOG_INFO("Initializing database service...");

    try
    {
        g_db_manager = std::make_unique<ft::DatabaseManager>();

        if (!g_db_manager->initialize())
        {
            NETWORK_LOG_ERROR("Failed to initialize database: {}", g_db_manager->get_last_error());
            return false;
        }

        NETWORK_LOG_INFO("Database service initialized successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        NETWORK_LOG_ERROR("Exception initializing database service: {}", e.what());
        return false;
    }
}

// 初始化行情服务（只初始化，不启动）
bool init_quote_service()
{
    NETWORK_LOG_INFO("Initializing quote service...");

    int result = quote_init();
    if (result != 0)
    {
        NETWORK_LOG_ERROR("Failed to initialize quote service: {}", result);
        const char *error = quote_get_last_error();
        if (error)
        {
            NETWORK_LOG_ERROR("Quote service error: {}", error);
        }
        return false;
    }

    NETWORK_LOG_INFO("Quote service initialized successfully");
    const char *parser_id = quote_get_parser_id();
    if (parser_id)
    {
        NETWORK_LOG_INFO("Quote parser ID: {}", parser_id);
    }

    return true;
}

// 初始化交易服务（只初始化，不启动）
bool init_trade_service()
{
    NETWORK_LOG_INFO("Initializing trade service...");

    int result = trade_init();
    if (result != 0)
    {
        NETWORK_LOG_ERROR("Failed to initialize trade service: {}", result);
        const char *error = trade_get_last_error();
        if (error)
        {
            NETWORK_LOG_ERROR("Trade service error: {}", error);
        }
        return false;
    }

    NETWORK_LOG_INFO("Trade service initialized successfully");

    // 注入数据库管理器到交易服务
    if (g_db_manager)
    {
        trade_set_db_manager(g_db_manager.get());
        NETWORK_LOG_INFO("Database manager injected into trade service");
    }

    const char *interface_id = trade_get_interface_id();
    if (interface_id)
    {
        NETWORK_LOG_INFO("Trade interface ID: {}", interface_id);
    }

    return true;
}

// 初始化策略服务（只初始化，不启动）
bool init_strategy_service()
{
    NETWORK_LOG_INFO("Initializing strategy service...");

    int result = strategy_init();
    if (result != 0)
    {
        NETWORK_LOG_ERROR("Failed to initialize strategy service: {}", result);
        const char *error = strategy_get_last_error();
        if (error)
        {
            NETWORK_LOG_ERROR("Strategy service error: {}", error);
        }
        return false;
    }

    NETWORK_LOG_INFO("Strategy service initialized successfully");

    // 注入数据库管理器到策略服务
    if (g_db_manager)
    {
        strategy_set_db_manager(g_db_manager.get());
        NETWORK_LOG_INFO("Database manager injected into strategy service");
    }

    return true;
}

// 初始化网络服务
bool init_network_service()
{
    NETWORK_LOG_INFO("Initializing network service...");

    try
    {
        g_network_service = std::make_unique<ft::network::NetworkService>();

        // 注册Handler模块
        register_api_handlers();

        if (!g_network_service->start())
        {
            NETWORK_LOG_ERROR("Failed to start network service: {}", g_network_service->get_last_error());
            return false;
        }

        NETWORK_LOG_INFO("Network service started on {}:{}",
                         g_network_service->get_address(),
                         g_network_service->get_port());
        return true;
    }
    catch (const std::exception &e)
    {
        NETWORK_LOG_ERROR("Exception initializing network service: {}", e.what());
        return false;
    }
}

// 注册API处理器
void register_api_handlers()
{
    NETWORK_LOG_INFO("Registering API handlers...");

    // 注册系统API处理器
    auto system_handler = std::make_shared<ft::network::SystemApiHandler>();
    g_network_service->register_handler(system_handler);

    // 注册交易API处理器并保存实例引用
    g_trade_handler_instance = std::make_shared<ft::network::TradeApiHandler>();
    g_network_service->register_handler(g_trade_handler_instance);

    // 注册行情API处理器
    auto quote_handler = std::make_shared<ft::network::QuoteApiHandler>();
    g_network_service->register_handler(quote_handler);

    // 注册策略API处理器
    auto strategy_handler = std::make_shared<ft::network::StrategyApiHandler>();
    g_network_service->register_handler(strategy_handler);

    NETWORK_LOG_INFO("API handlers registered successfully");
}

// 设置服务依赖关系
bool setup_services_and_network()
{
    NETWORK_LOG_INFO("Setting up service dependencies...");

    // 获取服务实例指针
    void *quote_parser = quote_get_parser();
    void *trade_handler = trade_get_handler();
    void *strategy_service = strategy_get_service_instance();

    NETWORK_LOG_INFO("Quote parser: {}, Trade handler: {}, Strategy service: {}, Database manager: {}",
                     fmt::ptr(quote_parser), fmt::ptr(trade_handler), fmt::ptr(strategy_service),
                     fmt::ptr(g_db_manager.get()));

    // 设置交易服务的依赖服务
    trade_set_quote_parser(quote_parser);

    // 设置策略服务的依赖服务
    strategy_set_quote_parser(quote_parser);
    strategy_set_trade_handler(trade_handler);
    strategy_set_trading_time_manager(g_trading_time_manager.get());

    // 设置TradeApiHandler的所有依赖（但不初始化TWAP）
    if (g_trade_handler_instance) {
        // 注入行情解析器
        if (quote_parser) {
            g_trade_handler_instance->set_quote_parser(static_cast<ft::IQuoteParser*>(quote_parser));
            NETWORK_LOG_INFO("QuoteParser injected into TradeApiHandler");
        }
        
        // 注入交易处理器
        if (trade_handler) {
            g_trade_handler_instance->set_trade_handler(static_cast<ft::ITradeHandler*>(trade_handler));
            NETWORK_LOG_INFO("TradeHandler injected into TradeApiHandler");
        }
        
        // 注入交易时间管理器
        if (g_trading_time_manager) {
            g_trade_handler_instance->set_trading_time_manager(g_trading_time_manager.get());
            NETWORK_LOG_INFO("TradingTimeManager injected into TradeApiHandler");
        }

        g_trade_handler_instance->init_and_start_twap();
    }

    // 将服务实例传递给网络服务
    if (g_network_service)
    {
        g_network_service->set_service_instances(quote_parser, trade_handler, strategy_service, g_db_manager.get());
        NETWORK_LOG_INFO("Service instances set to network service");
    }
    else
    {
        NETWORK_LOG_ERROR("Network service not initialized");
        return false;
    }

    NETWORK_LOG_INFO("Service dependencies configured successfully");
    return true;
}

// 策略清理函数，仅取消不删除
void execute_strategy_cleanup()
{
    NETWORK_LOG_INFO("Executing strategy pending orders cleanup...");
    try
    {
        // 1. 通过策略接口清理
        const char *active_strategy_ids_json = strategy_get_active_strategy_ids();
        if (active_strategy_ids_json && strlen(active_strategy_ids_json) > 2)
        {
            try
            {
                // 解析JSON数组
                nlohmann::json strategy_ids = nlohmann::json::parse(active_strategy_ids_json);

                if (strategy_ids.is_array())
                {
                    for (const auto &strategy_id : strategy_ids)
                    {
                        if (strategy_id.is_string())
                        {
                            std::string id_str = strategy_id.get<std::string>();

                            int result = strategy_cancel_all_active_order_queue(id_str.c_str());
                            if (result != 0)
                            {
                                NETWORK_LOG_WARN("Failed to cancel orders for strategy: {}", id_str);
                            }
                            else
                            {
                                NETWORK_LOG_INFO("Successfully cancelled orders for strategy: {}", id_str);
                            }
                        }
                    }
                }
            }
            catch (const nlohmann::json::exception &e)
            {
                NETWORK_LOG_ERROR("Failed to parse strategy IDs JSON: {}", e.what());
            }
        }

        // 2. 通过数据库批量取消状态（不删除记录）
        if (g_db_manager)
        {
            // 先找出所有active状态的挂单
            StrategyPendingOrderQuery query;
            query.status = PendingOrderStatus::ACTIVE;
            query.filter_by_status = true;
            query.limit = 10000;

            auto batch_result = g_db_manager->batch_query_strategy_pending_orders(query);
            NETWORK_LOG_INFO("Found {} active pending orders", batch_result.records.size());

            if (!batch_result.records.empty())
            {
                // 收集这些id
                std::vector<int64_t> order_ids;
                for (const auto &order : batch_result.records)
                {
                    order_ids.push_back(order.id);
                }

                // 批量更新状态为取消（不删除记录）
                ft::BatchUpdateOptions options;
                options.use_transaction = true;

                auto update_result = g_db_manager->batch_update_strategy_pending_order_status(
                    order_ids, 
                    PendingOrderStatus::CANCELLED, 
                    options
                );
                NETWORK_LOG_INFO("Cancelled {} pending orders (updated status to CANCELLED)", update_result.success_count);
                
                if (update_result.failed_count > 0)
                {
                    NETWORK_LOG_WARN("Failed to cancel {} pending orders", update_result.failed_count);
                }
            }
        }
        NETWORK_LOG_INFO("Strategy pending orders cleanup completed");
    }
    catch (const std::exception &e)
    {
        NETWORK_LOG_ERROR("Exception during cleanup: {}", e.what());
    }
}

// 初始化 cron 调度器
bool init_cron_scheduler()
{
    NETWORK_LOG_INFO("Initializing cron scheduler...");

    try
    {
        g_cron_scheduler = std::make_unique<ft::CronScheduler>();

        if (!g_cron_scheduler->start())
        {
            NETWORK_LOG_ERROR("Failed to start cron scheduler");
            return false;
        }

        // 添加每日下午4点的清理任务
        // "0 0 16 * * *" = 每天下午4点0分
        auto job_id = g_cron_scheduler->add_job(
            "strategy_order_cleanup",
            "0 0 16 * * *", // cron 表达式：每天16:00
            // "0 * * * * *",  // cron 表达式：每分钟执行 用于测试
            execute_strategy_cleanup);

        if (job_id > 0)
        {
            NETWORK_LOG_INFO("Added daily cleanup job (ID: {}) at 16:00", job_id);
        }
        else
        {
            NETWORK_LOG_ERROR("Failed to add cleanup job");
            return false;
        }

        NETWORK_LOG_INFO("Cron scheduler initialized successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        NETWORK_LOG_ERROR("Exception initializing cron scheduler: {}", e.what());
        return false;
    }
}

// 打印服务状态
void print_service_status()
{
    NETWORK_LOG_INFO("=== Service Status ===");

    // 行情服务状态
    NETWORK_LOG_INFO("Quote Service: {}", quote_is_running() ? "RUNNING" : "STOPPED");
    NETWORK_LOG_INFO("  - Connection: {}", quote_is_connected() ? "CONNECTED" : "DISCONNECTED");

    // 交易服务状态
    NETWORK_LOG_INFO("Trade Service: {}", trade_is_running() ? "RUNNING" : "STOPPED");
    NETWORK_LOG_INFO("  - Connection: {}", trade_is_connected() ? "CONNECTED" : "DISCONNECTED");

    // 策略服务状态
    NETWORK_LOG_INFO("Strategy Service: {}", strategy_is_service_running() ? "RUNNING" : "STOPPED");
    NETWORK_LOG_INFO("  - Initialized: {}", strategy_is_service_initialized() ? "YES" : "NO");

    // 数据库服务状态
    if (g_db_manager)
    {
        NETWORK_LOG_INFO("Database Service: {}", g_db_manager->is_initialized() ? "INITIALIZED" : "NOT INITIALIZED");
    }
    else
    {
        NETWORK_LOG_INFO("Database Service: NOT CREATED");
    }

    // 网络服务状态
    if (g_network_service)
    {
        NETWORK_LOG_INFO("Network Service: {}", g_network_service->is_running() ? "RUNNING" : "STOPPED");
        NETWORK_LOG_INFO("  - Address: {}:{}",
                         g_network_service->get_address(),
                         g_network_service->get_port());
    }
    else
    {
        NETWORK_LOG_INFO("Network Service: NOT INITIALIZED");
    }

    if (g_cron_scheduler)
    {
        NETWORK_LOG_INFO("Cron Scheduler: {}", g_cron_scheduler->is_running() ? "RUNNING" : "STOPPED");
        std::string jobs_info = g_cron_scheduler->get_jobs_info();
        NETWORK_LOG_DEBUG("Cron Jobs: {}", jobs_info);
    }
    else
    {
        NETWORK_LOG_INFO("Cron Scheduler: NOT INITIALIZED");
    }

    if (g_auto_scheduler)
    {
        NETWORK_LOG_INFO("Auto Scheduler: {}", g_auto_scheduler->is_running() ? "RUNNING" : "STOPPED");
    }
    else
    {
        NETWORK_LOG_INFO("Auto Scheduler: NOT INITIALIZED");
    }

    NETWORK_LOG_INFO("======================");
}

// 优雅关闭所有服务
void shutdown_services()
{
    NETWORK_LOG_INFO("Shutting down all services...");

    // 1. 首先关闭自动调度器（防止自动启动服务）
    if (g_auto_scheduler)
    {
        NETWORK_LOG_INFO("Stopping auto scheduler...");
        g_auto_scheduler->stop_scheduler();
        g_auto_scheduler.reset();
        NETWORK_LOG_INFO("Auto scheduler stopped");
    }

    // 2. 关闭网络服务（这会自动清理TradeApiHandler中的TWAP算法）
    if (g_network_service)
    {
        NETWORK_LOG_INFO("Stopping network service...");
        g_network_service->stop();
        g_network_service.reset();
        NETWORK_LOG_INFO("Network service stopped");
    }

    // 3. 清理TradeApiHandler实例引用
    if (g_trade_handler_instance) {
        g_trade_handler_instance.reset();
        NETWORK_LOG_INFO("TradeApiHandler instance reference cleared");
    }

    // 4. 逆序关闭其他服务（策略 -> 交易 -> 行情 -> 数据库）
    NETWORK_LOG_INFO("Stopping strategy service...");
    strategy_stop_service();
    strategy_release();
    NETWORK_LOG_INFO("Strategy service stopped");

    NETWORK_LOG_INFO("Stopping trade service...");
    trade_stop();
    trade_release();
    NETWORK_LOG_INFO("Trade service stopped");

    NETWORK_LOG_INFO("Stopping quote service...");
    quote_stop();
    quote_release();
    NETWORK_LOG_INFO("Quote service stopped");

    // 5. 关闭数据库服务
    if (g_db_manager)
    {
        NETWORK_LOG_INFO("Stopping database service...");
        g_db_manager->close();
        g_db_manager.reset();
        NETWORK_LOG_INFO("Database service stopped");
    }

    // 6. 最后关闭 cron 调度器
    if (g_cron_scheduler)
    {
        NETWORK_LOG_INFO("Stopping cron scheduler...");
        g_cron_scheduler->stop();
        g_cron_scheduler.reset();
        NETWORK_LOG_INFO("Cron scheduler stopped");
    }
    
    // 7. 清理交易时间管理器
    if (g_trading_time_manager)
    {
        NETWORK_LOG_INFO("Cleaning up trading time manager...");
        g_trading_time_manager.reset();
        NETWORK_LOG_INFO("Trading time manager cleaned up");
    }

    NETWORK_LOG_INFO("All services shut down successfully");
}

// 打印帮助信息
void print_usage(const char *program_name)
{
    std::cout << "FT Trading Network API Server - HTTP API Gateway for Trading Services\n\n"
              << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -h, --help                Show this help message\n"
              << "\nDescription:\n"
              << "  This server provides HTTP API endpoints to control and monitor\n"
              << "  the quote, trade, and strategy services. All services are initialized\n"
              << "  but not started automatically - use the API endpoints to control them.\n"
              << "\nAPI Endpoints:\n"
              << "  GET  /health              - System health check\n"
              << "  GET  /api                 - List all available endpoints\n"
              << "\nConfiguration:\n"
              << "  HTTP_SERVER_ADDRESS=0.0.0.0  (default)\n"
              << "  HTTP_SERVER_PORT=8080        (default)\n"
              << "  HTTP_SERVER_THREADS=4        (default)\n"
              << std::endl;
}

// 解析命令行参数
bool parse_arguments(int argc, char *argv[])
{
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "h", long_options, nullptr)) != -1)
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0]);
            return false;
        case '?':
            return false;
        default:
            return false;
        }
    }

    return true;
}

// 服务启动函数，在服务启动后初始化TWAP
bool start_all_services() {
    // 启动行情服务
    if (quote_start() != 0) {
        NETWORK_LOG_ERROR("Failed to start quote service");
        return false;
    }
    
    // 启动交易服务
    if (trade_start() != 0) {
        NETWORK_LOG_ERROR("Failed to start trade service");
        quote_stop();  // 回滚
        return false;
    }
    
    // 启动策略服务
    if (strategy_start_service() != 0) {
        NETWORK_LOG_ERROR("Failed to start strategy service");
        trade_stop();   // 回滚
        quote_stop();   // 回滚
        return false;
    }

    // 所有服务启动成功后，初始化TWAP算法
    if (g_trade_handler_instance) {
        g_trade_handler_instance->init_and_start_twap();
    }
    
    return true;
}

// 添加服务停止函数
void stop_all_services() {
    // 按相反顺序停止服务
    strategy_stop_service();
    trade_stop();
    quote_stop();
}

// 添加初始化交易时间管理器的函数
bool init_trading_time_manager() {
    NETWORK_LOG_INFO("Initializing trading time manager...");
    
    try {
        // 创建交易时间管理器
        g_trading_time_manager = std::make_unique<ft::TradingTimeManager>();
        
        // 从环境变量加载配置
        if (g_trading_time_manager->load_config_from_env() != 0) {
            NETWORK_LOG_WARN("Failed to load trading time config from env, using default config");
            NETWORK_LOG_WARN("Error: {}", g_trading_time_manager->get_last_error());
        }
        NETWORK_LOG_INFO("Trading time manager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Exception initializing trading time manager: {}", e.what());
        return false;
    }
}

// 修改自动调度器初始化函数
bool init_auto_scheduler() {
    NETWORK_LOG_INFO("Initializing auto scheduler...");
    
    try {
        // 确保交易时间管理器已初始化
        if (!g_trading_time_manager) {
            NETWORK_LOG_ERROR("TradingTimeManager not initialized");
            return false;
        }
        
        // 创建 AutoScheduler（不再内部创建 TradingTimeManager）
        g_auto_scheduler = std::make_unique<ft::AutoScheduler>(g_cron_scheduler.get());
        
        // 注入交易时间管理器
        g_auto_scheduler->set_trading_time_manager(g_trading_time_manager.get());
        NETWORK_LOG_INFO("TradingTimeManager injected into AutoScheduler");
        
        // 设置调度间隔
        g_auto_scheduler->set_schedule_interval(1);

        // 设置启动和停止回调
        g_auto_scheduler->set_start_callback(start_all_services);
        g_auto_scheduler->set_stop_callback(stop_all_services);
        
        // 启动自动调度器
        if (g_auto_scheduler->start_scheduler() != 0) {
            NETWORK_LOG_ERROR("Failed to start auto scheduler: {}", g_auto_scheduler->get_last_error());
            return false;
        }
        
        NETWORK_LOG_INFO("Auto scheduler initialized successfully");
        
        // 显示当前状态
        NETWORK_LOG_INFO("Current trading time: {}", g_auto_scheduler->is_current_trading_time() ? "YES" : "NO");
        
        // 显示下一个交易时间
        auto next_time = g_auto_scheduler->get_next_trading_time();
        auto next_time_t = std::chrono::system_clock::to_time_t(next_time);
        NETWORK_LOG_INFO("Next trading time: {}", std::ctime(&next_time_t));
        
        return true;
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Exception initializing auto scheduler: {}", e.what());
        return false;
    }
}

// 主函数
int main(int argc, char *argv[])
{
    // 解析命令行参数
    if (!parse_arguments(argc, argv))
    {
        return 1;
    }

    // 设置信号处理
    setup_signal_handlers();

    // 加载环境变量
    const char *env_file = ".env";
    if (file_exists(env_file))
    {
        dotenv::init(env_file);
        NETWORK_LOG_INFO("Loaded environment variables from: {}", env_file);
    }
    else
    {
        NETWORK_LOG_WARN("Environment file not found: {}", env_file);
        NETWORK_LOG_INFO("Using default configurations");
    }

    NETWORK_LOG_INFO("Starting FT Trading Network API Server...");

    try
    {
        // 首先初始化数据库服务
        if (!init_database_service())
        {
            NETWORK_LOG_ERROR("Failed to initialize database service");
            return 1;
        }

        // 按顺序初始化各个服务（但不启动）
        if (!init_quote_service())
        {
            NETWORK_LOG_ERROR("Failed to initialize quote service");
            shutdown_services();
            return 1;
        }

        if (!init_trade_service())
        {
            NETWORK_LOG_ERROR("Failed to initialize trade service");
            shutdown_services();
            return 1;
        }

        if (!init_strategy_service())
        {
            NETWORK_LOG_ERROR("Failed to initialize strategy service");
            shutdown_services();
            return 1;
        }

        if (!init_network_service())
        {
            NETWORK_LOG_ERROR("Failed to initialize network service");
            shutdown_services();
            return 1;
        }

        // 初始化 cron 调度器（必须在 auto_scheduler 之前）
        if (!init_cron_scheduler())
        {
            NETWORK_LOG_ERROR("Failed to initialize cron scheduler");
            shutdown_services();
            return 1;
        }

        // 初始化交易时间管理器（必须在 auto_scheduler 之前）
        if (!init_trading_time_manager())
        {
            NETWORK_LOG_ERROR("Failed to initialize trading time manager");
            shutdown_services();
            return 1;
        }

        // 设置服务依赖关系（必须在 auto_scheduler 之前）
        if (!setup_services_and_network())
        {
            NETWORK_LOG_ERROR("Failed to setup service dependencies");
            shutdown_services();
            return 1;
        }

        // 最后初始化自动调度器
        if (!init_auto_scheduler())
        {
            NETWORK_LOG_ERROR("Failed to initialize auto scheduler");
            shutdown_services();
            return 1;
        }

        NETWORK_LOG_INFO("All services initialized and network API is ready!");
        NETWORK_LOG_INFO("API server listening on http://{}:{}",
                         g_network_service->get_address(),
                         g_network_service->get_port());
        NETWORK_LOG_INFO("Use GET /api for endpoint documentation");

        print_service_status();
        NETWORK_LOG_INFO("Press Ctrl+C to shutdown");

        // 等待信号
        while (g_running.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    catch (const std::exception &e)
    {
        NETWORK_LOG_ERROR("Exception caught: {}", e.what());
        shutdown_services();
        return 1;
    }
    catch (...)
    {
        NETWORK_LOG_ERROR("Unknown exception caught");
        shutdown_services();
        return 1;
    }

    // 优雅关闭
    shutdown_services();
    NETWORK_LOG_INFO("FT Trading Network API Server shut down successfully");

    return 0;
}