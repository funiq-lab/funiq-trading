#include "network/handlers/http/system_api_handler.h"
#include "network/error/error.h"
#include "network/network_service.h"
#include "quote/quote_interface.h"
#include "trade/trade_interface.h"
#include "strategy/strategy_interface.h"
#include "logger/logger.hpp"
#include "utils/url_parser.h"
#include "utils/file.hpp"
#include "laserpants/dotenv/dotenv.h"
#include <chrono>
#include <thread>
#include <unistd.h>   // readlink
#include <limits.h>   // PATH_MAX
#include <fstream>
#include <regex>
#include <sstream>
#include <set>

NS_FT_BEGIN

namespace network {

SystemApiHandler::SystemApiHandler() : BaseApiHandler("/system") {
    NETWORK_LOG_INFO("SystemApiHandler created with prefix: {}", get_path_prefix());
}

void SystemApiHandler::register_routes(NetworkService* service) {
    // 健康检查
    service->get(get_full_path("/health"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_health_check(req, url_result); },
        "System health check");

    // 全局服务控制
    service->post(get_full_path("/start"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_start_all_services(req, url_result); },
        "Start all services");

    service->post(get_full_path("/stop"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_stop_all_services(req, url_result); },
        "Stop all services");

    // 服务状态查询
    service->get(get_full_path("/status"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_service_status(req, url_result); },
        "Get detailed service status");

    // 账号信息查询
    service->get(get_full_path("/accounts"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_accounts(req, url_result); },
        "Get all account information");

    // 账号详情
    service->get(get_full_path("/accounts/detail"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_account_detail(req, url_result); },
        "Get specific account information");

    // 获取日志
    service->get(get_full_path("/logs"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_logs(req, url_result); },
        "Get logs by date");

    NETWORK_LOG_INFO("SystemApiHandler routes registered");
}

HttpResponse SystemApiHandler::handle_health_check(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        json services = json::object();
        bool all_healthy = true;

        // 检查行情服务状态
        bool quote_running = (quote_is_running() == 1);
        bool quote_connected = (quote_is_connected() == 1);
        bool quote_healthy = quote_running && quote_connected;
        
        services["quote"] = {
            {"running", quote_running},
            {"connected", quote_connected},
            {"healthy", quote_healthy}
        };
        
        if (!quote_healthy) all_healthy = false;

        // 检查交易服务状态
        bool trade_running = (trade_is_running() == 1);
        bool trade_connected = (trade_is_connected() == 1);
        bool trade_healthy = trade_running && trade_connected;
        
        services["trade"] = {
            {"running", trade_running},
            {"connected", trade_connected},
            {"healthy", trade_healthy}
        };
        
        if (!trade_healthy) all_healthy = false;

        // 检查策略服务状态
        bool strategy_running = (strategy_is_service_running() == 1);
        bool strategy_initialized = (strategy_is_service_initialized() == 1);
        bool strategy_healthy = strategy_running && strategy_initialized;
        
        services["strategy"] = {
            {"running", strategy_running},
            {"initialized", strategy_initialized},
            {"healthy", strategy_healthy}
        };
        
        if (!strategy_healthy) all_healthy = false;

        json data = {
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"services", services},
            {"overall_status", all_healthy ? "healthy" : "unhealthy"}
        };

        auto status = all_healthy ? http::status::ok : http::status::service_unavailable;
        json response_body = {
            {"code", ErrorCode::SUCCESS},
            {"message", ErrorCode::get_message(ErrorCode::SUCCESS)},
            {"data", data}
        };
        
        return create_json_response(status, response_body, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Health check failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_start_all_services(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        json services = json::object();
        bool all_started = true;

        // 启动行情服务并等待连接就绪
        bool quote_started = start_quote_service();
        if (quote_started) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline) {
                if (quote_is_running() == 1) { break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (quote_is_running() != 1) {
                NETWORK_LOG_ERROR("Quote service failed readiness check after start");
                quote_started = false;
            }
        }
        services["quote"] = {
            {"attempted", true},
            {"started", quote_started},
            {"running", quote_is_running() == 1},
            {"connected", quote_is_connected() == 1}
        };
        if (!quote_started) all_started = false;
        void* latest_parser = quote_get_parser();
        // 启动交易服务并等待连接/登录就绪；并将最新的quote指针注入trade
        bool trade_started = false;
        if (quote_started) {
            trade_started = start_trade_service();
            if (trade_started) {
                // 注入最新的quote parser 给交易处理器
                if (latest_parser) { trade_set_quote_parser(latest_parser); }

                const auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (std::chrono::steady_clock::now() < deadline2) {
                    if (trade_is_running() == 1 && trade_is_connected() == 1) { break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (!(trade_is_running() == 1 && trade_is_connected() == 1)) {
                    NETWORK_LOG_ERROR("Trade service failed readiness check after start");
                    trade_started = false;
                }
            }
        }
        services["trade"] = {
            {"attempted", true},
            {"started", trade_started},
            {"running", trade_is_running() == 1},
            {"connected", trade_is_connected() == 1}
        };
        if (!trade_started) all_started = false;

        // 最后启动策略服务（仅在quote/trade 就绪后）并刷新依赖指针
        bool strategy_started = false;
        if (quote_started && trade_started) {
            // 先将最新的依赖指针注入到策略服务
            void* latest_trade  = trade_get_handler();
            if (latest_parser) { strategy_set_quote_parser(latest_parser); }
            if (latest_trade)  { strategy_set_trade_handler(latest_trade); }
            strategy_started = start_strategy_service();
        }
        services["strategy"] = {
            {"attempted", true},
            {"started", strategy_started},
            {"running", strategy_is_service_running() == 1}
        };
        if (!strategy_started) all_started = false;

        json data = {
            {"services", services},
            {"overall_result", all_started ? "success" : "partial_failure"},
            {"message", all_started ? "All services started successfully" : "Some services failed to start"}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Start services failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_stop_all_services(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        json services = json::object();
        bool all_stopped = true;

        // 按逆序停止服务：策略 -> 交易 -> 行情
        
        // 停止策略服务
        bool strategy_stopped = stop_strategy_service();
        // 等待依赖清理完成，降低悬空指针风险
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        services["strategy"] = {
            {"attempted", true},
            {"stopped", strategy_stopped},
            {"running", strategy_is_service_running() == 1}
        };
        if (!strategy_stopped) all_stopped = false;

        // 停止交易服务
        bool trade_stopped = stop_trade_service();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        services["trade"] = {
            {"attempted", true},
            {"stopped", trade_stopped},
            {"running", trade_is_running() == 1}
        };
        if (!trade_stopped) all_stopped = false;

        // 停止行情服务
        bool quote_stopped = stop_quote_service();
        services["quote"] = {
            {"attempted", true},
            {"stopped", quote_stopped},
            {"running", quote_is_running() == 1}
        };
        if (!quote_stopped) all_stopped = false;

        json data = {
            {"services", services},
            {"overall_result", all_stopped ? "success" : "partial_failure"},
            {"message", all_stopped ? "All services stopped successfully" : "Some services failed to stop"}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Stop services failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_service_status(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        json data = {
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"services", {
                {"quote", {
                    {"running", quote_is_running() == 1},
                    {"connected", quote_is_connected() == 1},
                    {"parser_id", quote_get_parser_id() ? quote_get_parser_id() : ""},
                    {"last_error", quote_get_last_error() ? quote_get_last_error() : ""}
                }},
                {"trade", {
                    {"running", trade_is_running() == 1},
                    {"connected", trade_is_connected() == 1},
                    {"interface_id", trade_get_interface_id() ? trade_get_interface_id() : ""},
                    {"logged_in_accounts", trade_get_logged_in_account_count()},
                    {"total_accounts", trade_get_account_count()},
                    {"last_error", trade_get_last_error() ? trade_get_last_error() : ""}
                }},
                {"strategy", {
                    {"running", strategy_is_service_running() == 1},
                    {"initialized", strategy_is_service_initialized() == 1},
                    {"active_instances", static_cast<int>(strategy_get_active_instance_count())},
                    {"last_error", strategy_get_last_error() ? strategy_get_last_error() : ""}
                }}
            }}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get service status failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_get_accounts(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        char** account_ids = nullptr;
        int count = 0;
        int result = trade_get_all_login_ids(&account_ids, &count);
        
        if (result != 0) {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to get account list",
                req
            );
        }

        json accounts = json::array();
        
        for (int i = 0; i < count; ++i) {
            AccountInfo account_info;
            if (trade_get_account_info_by_id(account_ids[i], &account_info) == 0) {
                json account_data = {
                    {"login_id", account_info.login_id},
                    {"cust_id", account_info.cust_id},
                    {"fund_account_id", account_info.fund_account_id},
                    {"account_id_sh", account_info.account_id_sh},
                    {"account_id_sz", account_info.account_id_sz},
                    {"is_logged_in", trade_is_account_login(account_ids[i]) == 1}
                    // 注意：不返回密码信息
                };
                accounts.push_back(account_data);
            }
        }

        // 释放内存
        trade_free_account_ids(account_ids, count);

        json data = {
            {"accounts", accounts},
            {"total_count", count},
            {"logged_in_count", trade_get_logged_in_account_count()}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get accounts failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_get_account_detail(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        std::string login_id = url_result.get_param("login_id");
        if (login_id.empty()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid Login ID in path",
                req
            );
        }

        // 检查账户是否存在
        if (trade_has_account(login_id.c_str()) != 1) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found",
                req
            );
        }

        // 获取账户基本信息
        AccountInfo account_info;
        if (trade_get_account_info_by_id(login_id.c_str(), &account_info) != 0) {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to get account information",
                req
            );
        }

        json account_detail = {
            {"login_id", account_info.login_id},
            {"cust_id", account_info.cust_id},
            {"fund_account_id", account_info.fund_account_id},
            {"account_id_sh", account_info.account_id_sh},
            {"account_id_sz", account_info.account_id_sz},
            {"is_logged_in", trade_is_account_login(login_id.c_str()) == 1}
            // 注意：不返回密码信息
        };

        // 使用 trade_get_account_data_by_id 分别获取沪市和深市的账户数据
        json funds_data = json::array();
        
        // 获取沪市账户数据
        if (strlen(account_info.account_id_sh) > 0) {
            AccountData* results = nullptr;
            int count = 0;

            trade_query_funds_sync(&account_info, MARKET_SSE, nullptr, nullptr, &results, &count, 30000, nullptr);
            if (count > 0) {
                AccountData sh_data = results[0];
                json sh_fund_data = {
                    {"account_id", sh_data.current_account_id},
                    {"market", static_cast<int>(sh_data.current_market)},
                    {"currency", sh_data.currency},
                    {"total_asset", sh_data.total_asset},
                    {"cash_balance", sh_data.cash_balance},
                    {"available_cash", sh_data.available_cash},
                    {"frozen_cash", sh_data.frozen_cash},
                    {"market_value", sh_data.market_value},
                    {"buying_power", sh_data.buying_power},
                    {"withdrawal_cash", sh_data.withdrawal_cash},
                    {"profit_loss", sh_data.profit_loss},
                    {"profit_loss_ratio", sh_data.profit_loss_ratio},
                    {"timestamp", sh_data.timestamp}
                };
                funds_data.push_back(sh_fund_data);
            }
        }
        
        // 获取深市账户数据
        if (strlen(account_info.account_id_sz) > 0) {
            AccountData* results = nullptr;
            int count = 0;

            trade_query_funds_sync(&account_info, MARKET_SZSE, nullptr, nullptr, &results, &count, 30000, nullptr);
            if (count > 0) {
                AccountData sz_data = results[0];
                json sz_fund_data = {
                    {"account_id", sz_data.current_account_id},
                    {"market", static_cast<int>(sz_data.current_market)},
                    {"currency", sz_data.currency},
                    {"total_asset", sz_data.total_asset},
                    {"cash_balance", sz_data.cash_balance},
                    {"available_cash", sz_data.available_cash},
                    {"frozen_cash", sz_data.frozen_cash},
                    {"market_value", sz_data.market_value},
                    {"buying_power", sz_data.buying_power},
                    {"withdrawal_cash", sz_data.withdrawal_cash},
                    {"profit_loss", sz_data.profit_loss},
                    {"profit_loss_ratio", sz_data.profit_loss_ratio},
                    {"timestamp", sz_data.timestamp}
                };
                funds_data.push_back(sz_fund_data);
            }
        }
        
        account_detail["funds"] = funds_data;

        json data = {
            {"account", account_detail}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get account detail failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

HttpResponse SystemApiHandler::handle_get_logs(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取date参数 (格式: YYYY-MM-DD)
        std::string date = url_result.get_param("date");
        if (date.empty()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing 'date' parameter (format: YYYY-MM-DD)",
                req
            );
        }

        // 严格验证日期格式: YYYY-MM-DD，年份范围2000-2099，月份01-12，日期01-31
        std::regex date_pattern(R"(^(20\d{2})-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])$)");
        if (!std::regex_match(date, date_pattern)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid date format, expected YYYY-MM-DD (e.g., 2025-11-19)",
                req
            );
        }

        // 获取logger参数(必填)，并进行白名单校验
        std::string logger_name = url_result.get_param("logger");
        if (logger_name.empty()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing 'logger' parameter. Valid options: quote, scheduler, server, strategy, trade",
                req
            );
        }

        // 白名单校验
        static const std::set<std::string> valid_loggers = {
            "quote", "scheduler", "server", "strategy", "trade"
        };

        if (valid_loggers.find(logger_name) == valid_loggers.end()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid logger name. Valid options: quote, scheduler, server, strategy, trade",
                req
            );
        }

        // 获取日志目录
        dotenv::init();
        std::string log_dir = dotenv::getenv("LOG_FILE_DIR", "logs");

        // 日志文件格式: logger_YYYY-MM-DD.log
        std::string log_file = log_dir + "/" + logger_name + "_" + date + ".log";
        std::string logs_content;

        if (file_exists(log_file) && is_regular_file(log_file)) {
            std::ifstream file(log_file);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                logs_content = buffer.str();
                file.close();
            } else {
                NETWORK_LOG_WARN("Failed to open log file: {}", log_file);
                logs_content = "No logs found for the specified date";
            }
        } else {
            logs_content = "No logs found for the specified date";
        }

        json data = {
            {"date", date},
            {"logger", logger_name},
            {"logs", logs_content}
        };

        return create_success_response(data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get logs failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR),
            req
        );
    }
}

// ===== 服务控制私有方法实现 =====

bool SystemApiHandler::start_quote_service() {
    NETWORK_LOG_INFO("Starting quote service...");
    int result = quote_start();
    if (result == 0) {
        NETWORK_LOG_INFO("Quote service started successfully");
        return true;
    } else {
        const char* error = quote_get_last_error();
        NETWORK_LOG_ERROR("Failed to start quote service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

bool SystemApiHandler::stop_quote_service() {
    NETWORK_LOG_INFO("Stopping quote service...");
    int result = quote_stop();
    if (result == 0) {
        NETWORK_LOG_INFO("Quote service stopped successfully");
        return true;
    } else {
        const char* error = quote_get_last_error();
        NETWORK_LOG_ERROR("Failed to stop quote service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

bool SystemApiHandler::start_trade_service() {
    NETWORK_LOG_INFO("Starting trade service...");
    int result = trade_start();
    if (result == 0) {
        NETWORK_LOG_INFO("Trade service started successfully");
        return true;
    } else {
        const char* error = trade_get_last_error();
        NETWORK_LOG_ERROR("Failed to start trade service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

bool SystemApiHandler::stop_trade_service() {
    NETWORK_LOG_INFO("Stopping trade service...");
    int result = trade_stop();
    if (result == 0) {
        NETWORK_LOG_INFO("Trade service stopped successfully");
        return true;
    } else {
        const char* error = trade_get_last_error();
        NETWORK_LOG_ERROR("Failed to stop trade service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

bool SystemApiHandler::start_strategy_service() {
    NETWORK_LOG_INFO("Starting strategy service...");
    int result = strategy_start_service();
    if (result == 0) {
        NETWORK_LOG_INFO("Strategy service started successfully");
        return true;
    } else {
        const char* error = strategy_get_last_error();
        NETWORK_LOG_ERROR("Failed to start strategy service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

bool SystemApiHandler::stop_strategy_service() {
    NETWORK_LOG_INFO("Stopping strategy service...");
    int result = strategy_stop_service();
    if (result == 0) {
        NETWORK_LOG_INFO("Strategy service stopped successfully");
        return true;
    } else {
        const char* error = strategy_get_last_error();
        NETWORK_LOG_ERROR("Failed to stop strategy service: {} - {}", result, error ? error : "Unknown error");
        return false;
    }
}

} // namespace network

NS_FT_END