#include "network/handlers/http/trade_api_handler.h"
#include "network/network_service.h"
#include "trade/trade_interface.h"
#include "quote/quote_interface.h"
#include "utils/url_parser.h"
#include "utils/trade_utils.h"
#include "logger/logger.hpp"
#include "network/error/error.h"
#include "algorithm/twap.h"
#include "utils/trading_time.h"
#include <chrono>
#include <iomanip>
#include <sstream>

// ATP特殊错误码 - 表示"未找到数据"而非"查询失败"
#define ATP_ERROR_NOT_FOUND_ORDER 100008    // 未找到指定的订单
#define ATP_ERROR_NOT_FOUND_TER   100009    // 未找到指定的成交回报  
#define ATP_ERROR_NOT_FOUND_SHARE 100010    // 未找到股份

NS_FT_BEGIN
namespace network {

TradeApiHandler::TradeApiHandler() : BaseApiHandler("/trade") {
    NETWORK_LOG_INFO("TradeApiHandler created");
}

TradeApiHandler::~TradeApiHandler() {
    // 停止TWAP算法
    stop_twap_algorithm();
    
    // 清理依赖指针
    _trading_time_manager = nullptr;
}

void TradeApiHandler::register_routes(NetworkService* service) {
    // 股票买入（输入总金额）
    service->post(get_full_path("/buy"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_buy_stock(req, url_result); },
        "Buy stock with specified amount, quantity calculated from current price.");

    // 股票卖出（输入股数）
    service->post(get_full_path("/sell"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_sell_stock(req, url_result); },
        "Sell stock with specified quantity at current market price.");

    // 撤单
    service->post(get_full_path("/cancel"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_cancel_order(req, url_result); },
        "Cancel existing order");

    // 银证转账
    service->post(get_full_path("/bank-transfer"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_bank_securities_transfer(req, url_result); },
        "Bank securities transfer");

    // 市场间资金转账
    service->post(get_full_path("/fund-transfer"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_fund_transfer(req, url_result); },
        "Fund transfer between markets");

    // TWAP拆单卖出
    service->post(get_full_path("/twap-sell"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_twap_sell(req, url_result); },
        "TWAP split sell with intelligent time-based execution");

    // TWAP任务状态查询
    service->get(get_full_path("/twap/task"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_twap_task_status(req, url_result); },
        "Query TWAP task status by task ID");

    // TWAP任务取消
    service->post(get_full_path("/twap/cancel"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_twap_cancel_task(req, url_result); },
        "Cancel TWAP task by task ID");

    // ===== 查询接口改为GET方法 =====
    
    // 委托查询
    service->get(get_full_path("/query/orders"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_orders(req, url_result); },
        "Query order information");

    // 成交查询
    service->get(get_full_path("/query/trades"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_trades(req, url_result); },
        "Query trade information");

    // 持仓查询
    service->get(get_full_path("/query/positions"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_positions(req, url_result); },
        "Query position information");

    // 资金查询
    service->get(get_full_path("/query/funds"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_funds(req, url_result); },
        "Query fund information");

    NETWORK_LOG_INFO("TradeApiHandler routes registered");
}

void TradeApiHandler::set_quote_parser(IQuoteParser* quote_parser) {
    _quote_parser = quote_parser;
    NETWORK_LOG_INFO("QuoteParser set for TradeApiHandler");

    if (_twap_initialized && _twap_algorithm) {
        _twap_algorithm->set_quote_parser(_quote_parser);
        NETWORK_LOG_INFO("QuoteParser updated in TWAP algorithm");
    }
}

void TradeApiHandler::set_trade_handler(ITradeHandler* trade_handler) {
    _trade_handler = trade_handler;
    NETWORK_LOG_INFO("TradeHandler set for TradeApiHandler");

    if (_twap_initialized && _twap_algorithm) {
        _twap_algorithm->set_trade_handler(_trade_handler);
        NETWORK_LOG_INFO("TradeHandler updated in TWAP algorithm");
    }
}

void TradeApiHandler::set_trading_time_manager(TradingTimeManager* trading_time_manager) {
    _trading_time_manager = trading_time_manager;
    NETWORK_LOG_INFO("TradingTimeManager set for TradeApiHandler");

    if (_twap_initialized && _twap_algorithm) {
        _twap_algorithm->set_trading_time_manager(_trading_time_manager);
        NETWORK_LOG_INFO("TradingTimeManager updated in TWAP algorithm");
    }
}

void TradeApiHandler::init_and_start_twap() {
    NETWORK_LOG_INFO("Attempting to initialize and start TWAP algorithm...");
    
    // 检查所有依赖是否已设置
    if (!_quote_parser) {
        NETWORK_LOG_WARN("Cannot start TWAP: QuoteParser not set");
        return;
    }
    
    if (!_trade_handler) {
        NETWORK_LOG_WARN("Cannot start TWAP: TradeHandler not set");
        return;
    }
    
    if (!_trading_time_manager) {
        NETWORK_LOG_WARN("Cannot start TWAP: TradingTimeManager not set");
        return;
    }
    
    // 尝试初始化TWAP算法
    if (init_twap_algorithm()) {
        // 启动TWAP算法
        if (start_twap_algorithm()) {
            NETWORK_LOG_INFO("TWAP algorithm successfully initialized and started");
        } else {
            NETWORK_LOG_ERROR("TWAP algorithm initialized but failed to start");
        }
    } else {
        NETWORK_LOG_ERROR("Failed to initialize TWAP algorithm");
    }
}

// ===== TWAP算法管理方法实现 =====

bool TradeApiHandler::init_twap_algorithm() {
    if (_twap_initialized) {
        NETWORK_LOG_INFO("TWAP algorithm already initialized");
        return true;  // 已经初始化过了
    }
    
    // 检查所有依赖是否已设置
    if (!_quote_parser) {
        NETWORK_LOG_WARN("Cannot initialize TWAP: QuoteParser not set");
        return false;
    }
    
    if (!_trade_handler) {
        NETWORK_LOG_WARN("Cannot initialize TWAP: TradeHandler not set");
        return false;
    }
    
    if (!_trading_time_manager) {
        NETWORK_LOG_WARN("Cannot initialize TWAP: TradingTimeManager not set");
        return false;
    }
    
    NETWORK_LOG_INFO("Initializing TWAP algorithm...");
    
    try {
        // 创建TWAP算法实例
        _twap_algorithm = std::make_unique<TwapAlgorithm>();
        
        // 设置依赖服务
        _twap_algorithm->set_quote_parser(_quote_parser);
        _twap_algorithm->set_trade_handler(_trade_handler);
        _twap_algorithm->set_trading_time_manager(_trading_time_manager);
        
        // 初始化算法
        if (_twap_algorithm->init() != 0) {
            NETWORK_LOG_ERROR("Failed to initialize TWAP algorithm: {}", 
                            _twap_algorithm->get_last_error());
            _twap_algorithm.reset();
            return false;
        }
        
        _twap_initialized = true;
        NETWORK_LOG_INFO("TWAP algorithm initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Exception initializing TWAP algorithm: {}", e.what());
        _twap_algorithm.reset();
        return false;
    }
}

bool TradeApiHandler::start_twap_algorithm() {
    if (!_twap_initialized || !_twap_algorithm) {
        NETWORK_LOG_WARN("Cannot start TWAP: algorithm not initialized");
        return false;
    }
    
    if (_twap_algorithm->is_running()) {
        NETWORK_LOG_INFO("TWAP algorithm is already running");
        return true;  // 已经运行中
    }
    
    NETWORK_LOG_INFO("Starting TWAP algorithm...");
    
    if (_twap_algorithm->start() != 0) {
        NETWORK_LOG_ERROR("Failed to start TWAP algorithm: {}", 
                        _twap_algorithm->get_last_error());
        return false;
    }
    
    NETWORK_LOG_INFO("TWAP algorithm started successfully");
    return true;
}

void TradeApiHandler::stop_twap_algorithm() {
    if (_twap_algorithm && _twap_algorithm->is_running()) {
        NETWORK_LOG_INFO("Stopping TWAP algorithm...");
        _twap_algorithm->stop();
        NETWORK_LOG_INFO("TWAP algorithm stopped");
    }
    
    if (_twap_algorithm) {
        _twap_algorithm.reset();
        _twap_initialized = false;
        NETWORK_LOG_INFO("TWAP algorithm cleaned up");
    }
}

bool TradeApiHandler::is_twap_available() const {
    bool available = _twap_initialized && 
                    _twap_algorithm && 
                    _twap_algorithm->is_running() && 
                    _quote_parser != nullptr &&
                    _trade_handler != nullptr &&
                    _trading_time_manager != nullptr;
                    
    if (!available) {
        NETWORK_LOG_DEBUG("TWAP not available - initialized: {}, algorithm: {}, running: {}, quote: {}, trade: {}, trading_time: {}", 
                         _twap_initialized, 
                         fmt::ptr(_twap_algorithm.get()),
                         _twap_algorithm ? _twap_algorithm->is_running() : false,
                         fmt::ptr(_quote_parser),
                         fmt::ptr(_trade_handler), 
                         fmt::ptr(_trading_time_manager));
    }
    
    return available;
}

// ===== 现有的交易API处理方法保持不变 =====

HttpResponse TradeApiHandler::handle_buy_stock(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("security_code") ||
            !request_data.contains("amount")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, security_code, amount",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        std::string security_code = request_data["security_code"];
        bool chasing_rise_halfway = request_data.value("chasing_rise_halfway", false);
        double amount = request_data["amount"].get<double>();

        // 可选的价格参数
        bool has_custom_price = request_data.contains("price") && !request_data["price"].is_null();
        double custom_price = 0.0;
        if (has_custom_price) {
            custom_price = request_data["price"].get<double>();
            // 格式化为2位小数
            custom_price = std::round(custom_price * 100.0) / 100.0;
        }

        // 验证金额
        if (amount <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Amount must be greater than 0",
                req
            );
        }

        // 验证自定义价格
        if (has_custom_price && custom_price <= 0.0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Price must be greater than 0",
                req
            );
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 从行情服务获取完整深度数据
        DepthData depth_data = {};
        if (!get_stock_info_from_quote(security_code, depth_data)) {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to get stock info from quote service",
                req
            );
        }

        double buy_price;

        // 如果传入了自定义价格，直接使用该价格(忽略价格笼子)
        if (has_custom_price) {
            buy_price = custom_price;
        } else {
            // 否则使用原有逻辑
            buy_price = depth_data.last_price;

            if (chasing_rise_halfway) {
                // 如果启用追涨半途，使用卖一价作为买入参考价格，计算价格笼子上限作为买入价格买入
                buy_price = utils::calculate_price_cage_upper_limit(depth_data.ask_price[0], security_code);
            }
        }

        if (buy_price <= 0.0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid buy price",
                req
            );
        }

        // 根据总金额和买入价格计算买入股数
        int64_t quantity = utils::calculate_buy_quantity(amount, buy_price, security_code);
        if (quantity <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Insufficient amount to buy minimum quantity",
                req
            );
        }

        // 构造订单请求
        TradeOrderRequest order = build_order_request(
            account, security_code, depth_data.security_info.market, SIDE_BUY, quantity, buy_price
        );

        // 同步下单
        TradeOrderRspData result = {};
        int32_t order_result = trade_send_order_sync(&order, &result, DEFAULT_TIMEOUT_MS);

        if (order_result == 0) {
            json response_data = {
                // 账户信息
                {"login_id", result.account.login_id},
                {"cust_id", result.account.cust_id},
                {"fund_account_id", result.account.fund_account_id},
                {"branch_id", result.account.branch_id},
                {"current_account_id", result.current_account_id},
                
                // 证券信息
                {"security_code", security_code},
                {"market", static_cast<int>(depth_data.security_info.market)},
                
                // 订单信息
                {"request_id", result.request_id},
                {"order_id", result.order_id},
                {"batch_id", result.batch_id},
                {"side", static_cast<int>(result.side)},
                {"order_type", static_cast<int>(result.order_type)},
                {"price", result.price},
                {"qty", result.qty},
                {"business_type", result.business_type},
                
                // 时间信息
                {"response_time", result.response_time},
                {"timestamp", result.timestamp}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                result.error_msg,
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Buy stock failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_sell_stock(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("security_code") ||
            !request_data.contains("quantity")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, security_code, quantity",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        std::string security_code = request_data["security_code"];
        int64_t quantity = request_data["quantity"].get<int64_t>();

        // 可选的价格参数
        bool has_custom_price = request_data.contains("price") && !request_data["price"].is_null();
        double custom_price = 0.0;
        if (has_custom_price) {
            custom_price = request_data["price"].get<double>();
            // 格式化为2位小数
            custom_price = std::round(custom_price * 100.0) / 100.0;
        }

        // 验证股数
        if (quantity <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Quantity must be greater than 0",
                req
            );
        }

        // 验证自定义价格
        if (has_custom_price && custom_price <= 0.0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Price must be greater than 0",
                req
            );
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 从行情服务获取完整深度数据
        DepthData depth_data = {};
        if (!get_stock_info_from_quote(security_code, depth_data)) {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to get stock info from quote service",
                req
            );
        }

        double sell_price;

        // 如果传入了自定义价格，直接使用该价格
        if (has_custom_price) {
            sell_price = custom_price;
        } else {
            // 否则使用当前价格作为卖出参考价格
            sell_price = depth_data.last_price;
        }

        if (sell_price <= 0.0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid sell price",
                req
            );
        }

        // 构造订单请求
        TradeOrderRequest order = build_order_request(
            account, security_code, depth_data.security_info.market, SIDE_SELL, quantity, sell_price
        );

        // 同步下单
        TradeOrderRspData result = {};
        int32_t order_result = trade_send_order_sync(&order, &result, DEFAULT_TIMEOUT_MS);

        if (order_result == 0) {
            json response_data = {
                // 账户信息
                {"login_id", result.account.login_id},
                {"cust_id", result.account.cust_id},
                {"fund_account_id", result.account.fund_account_id},
                {"branch_id", result.account.branch_id},
                {"current_account_id", result.current_account_id},
                
                // 证券信息
                {"security_code", security_code},
                {"market", static_cast<int>(depth_data.security_info.market)},
                
                // 订单信息
                {"request_id", result.request_id},
                {"order_id", result.order_id},
                {"batch_id", result.batch_id},
                {"side", static_cast<int>(result.side)},
                {"order_type", static_cast<int>(result.order_type)},
                {"price", result.price},
                {"qty", result.qty},
                {"business_type", result.business_type},
                
                // 时间信息
                {"response_time", result.response_time},
                {"timestamp", result.timestamp}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                result.error_msg,
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Sell stock failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_cancel_order(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("market") ||
            !request_data.contains("order_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, market, order_id",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        MarketType market = static_cast<MarketType>(request_data["market"].get<int>());
        int64_t order_id = request_data["order_id"].get<int64_t>();
        uint64_t batch_id = request_data.value("batch_id", 0);

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步撤单
        TradeOrderCancelRsqData result = {};
        int32_t cancel_result = trade_cancel_order_sync(
            &account, order_id, batch_id, market, &result, DEFAULT_TIMEOUT_MS
        );

        if (cancel_result == 0) {
            json response_data = {
                // 账户信息
                {"login_id", result.account.login_id},
                {"cust_id", result.account.cust_id},
                {"fund_account_id", result.account.fund_account_id},
                {"branch_id", result.account.branch_id},
                {"current_account_id", result.current_account_id},
                
                // 订单信息
                {"request_id", result.request_id},
                {"order_id", result.order_id},
                {"batch_id", result.batch_id},
                
                // 时间信息
                {"response_time", result.response_time},
                {"timestamp", result.timestamp}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                result.error_msg,
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Cancel order failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_bank_securities_transfer(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("direction") ||
            !request_data.contains("market") ||
            !request_data.contains("transfer_amount") ||
            !request_data.contains("bank_password") ||
            !request_data.contains("fund_password")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, direction, market, transfer_amount, bank_password, fund_password",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        BankSecuritiesTransferDirection direction = static_cast<BankSecuritiesTransferDirection>(
            request_data["direction"].get<int>());
        MarketType market = static_cast<MarketType>(request_data["market"].get<int>());
        double transfer_amount = request_data["transfer_amount"].get<double>();
        std::string currency = request_data.value("currency", "CNY");
        std::string bank_password = request_data["bank_password"];
        std::string fund_password = request_data["fund_password"];
        std::string remark = request_data.value("remark", "");

        // 验证金额
        if (transfer_amount <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Transfer amount must be greater than 0",
                req
            );
        }

        // 验证方向
        if (direction != BANK_SECURITIES_TRANSFER_IN && direction != BANK_SECURITIES_TRANSFER_OUT) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid transfer direction (1=in, 2=out)",
                req
            );
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步银证转账
        BankSecuritiesTransferData result = {};
        int32_t transfer_result = trade_bank_securities_transfer_sync(
            &account, direction, market, transfer_amount, currency.c_str(),
            bank_password.c_str(), fund_password.c_str(), remark.c_str(),
            &result, DEFAULT_TIMEOUT_MS
        );

        if (transfer_result == 0) {
            json response_data = {
                // 账户信息
                {"login_id", result.login_id},
                {"cust_id", result.cust_id},
                {"fund_account_id", result.fund_account_id},
                {"branch_id", result.branch_id},
                {"account_id", result.account_id},
                
                // 转账信息
                {"request_id", result.request_id},
                {"serial_no", result.serial_no},
                {"transfer_serial_no", result.transfer_serial_no},
                {"bank_transfer_serial_no", result.bank_transfer_serial_no},
                {"direction", static_cast<int>(result.direction)},
                {"business_status", result.business_status},
                
                // 时间信息
                {"transact_time", result.transact_time},
                {"timestamp", result.timestamp}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                result.error_msg,
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Bank securities transfer failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_fund_transfer(const HttpRequest& req, const UrlParseResult& url_result) {
     try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("from_market") ||
            !request_data.contains("to_market") ||
            !request_data.contains("transfer_amount")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, from_market, to_market, transfer_amount",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        MarketType from_market = static_cast<MarketType>(request_data["from_market"].get<int>());
        MarketType to_market = static_cast<MarketType>(request_data["to_market"].get<int>());
        double transfer_amount = request_data["transfer_amount"].get<double>();
        std::string remark = request_data.value("remark", "");

        // 验证金额
        if (transfer_amount <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Transfer amount must be greater than 0",
                req
            );
        }

        // 验证市场
        if ((from_market != MARKET_SSE && from_market != MARKET_SZSE) ||
            (to_market != MARKET_SSE && to_market != MARKET_SZSE)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid market type (1=SSE, 2=SZSE)",
                req
            );
        }

        // 验证不能是同一个市场
        if (from_market == to_market) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Cannot transfer between same market",
                req
            );
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 根据市场类型获取对应的账户ID
        std::string fund_out_account_id;
        std::string fund_in_account_id;
        
        if (from_market == MARKET_SSE) {
            fund_out_account_id = account.account_id_sh;
        } else {
            fund_out_account_id = account.account_id_sz;
        }
        
        if (to_market == MARKET_SSE) {
            fund_in_account_id = account.account_id_sh;
        } else {
            fund_in_account_id = account.account_id_sz;
        }

        // 验证账户ID是否有效
        if (fund_out_account_id.empty() || fund_in_account_id.empty()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Account ID not found for specified market",
                req
            );
        }

        // 同步资金转账
        FundTransferData result = {};
        int32_t transfer_result = trade_fund_transfer_sync(
            &account, fund_out_account_id.c_str(), from_market,
            fund_in_account_id.c_str(), to_market, transfer_amount,
            &result, DEFAULT_TIMEOUT_MS
        );

        if (transfer_result == 0) {
            json response_data = {
                // 账户信息
                {"login_id", result.login_id},
                {"cust_id", result.cust_id},
                {"fund_account_id", result.fund_account_id},
                {"branch_id", result.branch_id},
                
                // 转账信息
                {"request_id", result.request_id},
                
                // 时间信息
                {"timestamp", result.timestamp}
            };

            if (!remark.empty()) {
                response_data["remark"] = remark;
            }

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                result.error_msg,
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Inter market fund transfer failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_twap_sell(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }

        // 检查TWAP算法是否可用
        if (!is_twap_available()) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "TWAP service is not available",
                req
            );
        }

        // 解析请求体
        json request_data;
        std::string parse_error;
        if (!parse_json_body(req, request_data, parse_error)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_JSON_FORMAT,
                parse_error,
                req
            );
        }

        // 验证必需字段
        if (!request_data.contains("login_id") || 
            !request_data.contains("security_code") ||
            !request_data.contains("quantity")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required fields: login_id, security_code, quantity",
                req
            );
        }

        // 解析参数
        std::string login_id = request_data["login_id"];
        std::string security_code = request_data["security_code"];
        int64_t quantity = request_data["quantity"].get<int64_t>();

        // 验证股数
        if (quantity <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Quantity must be greater than 0",
                req
            );
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 从行情服务获取完整深度数据
        DepthData depth_data = {};
        if (!get_stock_info_from_quote(security_code, depth_data)) {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to get stock info from quote service",
                req
            );
        }

        // 解析时间参数（可选）
        auto now = std::chrono::system_clock::now();
        
        std::chrono::system_clock::time_point start_time = now;
        if (request_data.contains("start_time") && !request_data["start_time"].is_null()) {
            std::string start_time_str = request_data["start_time"];
            if (!start_time_str.empty()) {
                start_time = parse_iso8601_time(start_time_str);
                if (start_time == std::chrono::system_clock::time_point{}) {
                    return create_error_response(
                        http::status::bad_request,
                        ErrorCode::INVALID_ARGUMENT,
                        "Invalid start_time format, use ISO8601 (e.g., 2024-01-15T09:30:00)",
                        req
                    );
                }
            }
        }

        std::chrono::system_clock::time_point end_time = start_time + std::chrono::minutes(10);
        if (request_data.contains("end_time") && !request_data["end_time"].is_null()) {
            std::string end_time_str = request_data["end_time"];
            if (!end_time_str.empty()) {
                end_time = parse_iso8601_time(end_time_str);
                if (end_time == std::chrono::system_clock::time_point{}) {
                    return create_error_response(
                        http::status::bad_request,
                        ErrorCode::INVALID_ARGUMENT,
                        "Invalid end_time format, use ISO8601 (e.g., 2024-01-15T09:40:00)",
                        req
                    );
                }
            }
        }

        // 验证时间顺序
        if (start_time >= end_time) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "end_time must be after start_time",
                req
            );
        }

        // 解析其他可选参数
        int interval_seconds = request_data.value("interval_seconds", 10);
        bool enable_price_threshold = request_data.value("enable_price_threshold", false);
        double price_threshold_ratio = request_data.value("price_threshold_ratio", 0.05);

        // 验证间隔时间
        if (interval_seconds <= 0 || interval_seconds > 3600) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "interval_seconds must be between 1 and 3600",
                req
            );
        }

        // 验证价格阈值比例
        if (price_threshold_ratio < 0.0 || price_threshold_ratio > 1.0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "price_threshold_ratio must be between 0.0 and 1.0",
                req
            );
        }

        // 创建TWAP任务
        uint64_t task_id = _twap_algorithm->create_twap_sell_task(
            account,
            security_code,
            depth_data.security_info.market,
            quantity,
            start_time,
            end_time,
            interval_seconds,
            enable_price_threshold,
            price_threshold_ratio
        );

        if (task_id > 0) {
            // 获取任务详细信息
            std::string task_status = _twap_algorithm->get_twap_task_status(task_id);
            json task_info = json::parse(task_status);

            json response_data = {
                {"task_id", task_id},
                {"task_id_formatted", TwapAlgorithm::format_task_id(task_id)},
                {"login_id", login_id},
                {"security_code", security_code},
                {"market", static_cast<int>(depth_data.security_info.market)},
                {"total_quantity", quantity},
                {"interval_seconds", interval_seconds},
                {"enable_price_threshold", enable_price_threshold},
                {"price_threshold_ratio", price_threshold_ratio},
                {"task_details", task_info}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to create TWAP task: " + _twap_algorithm->get_last_error(),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("TWAP sell failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_twap_task_status(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        if (!is_twap_available()) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "TWAP service is not available",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("task_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: task_id",
                req
            );
        }

        // 从查询参数中获取task_id
        std::string task_id_str = url_result.get_param("task_id");
        uint64_t task_id = 0;
        try {
            task_id = std::stoull(task_id_str);
        } catch (const std::exception& e) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid task_id format",
                req
            );
        }

        // 查询任务状态
        std::string task_status = _twap_algorithm->get_twap_task_status(task_id);
        if (task_status.empty()) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "TWAP task not found",
                req
            );
        }

        json task_info = json::parse(task_status);
        return create_success_response(task_info, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query TWAP task status failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_twap_cancel_task(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        if (!is_twap_available()) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "TWAP service is not available",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("task_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: task_id",
                req
            );
        }

        // 从查询参数中获取task_id
        std::string task_id_str = url_result.get_param("task_id");
        uint64_t task_id = 0;
        try {
            task_id = std::stoull(task_id_str);
        } catch (const std::exception& e) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid task_id format",
                req
            );
        }

        // 取消任务
        int result = _twap_algorithm->cancel_twap_task(task_id);
        if (result == 0) {
            json response_data = {
                {"task_id", task_id},
                {"task_id_formatted", TwapAlgorithm::format_task_id(task_id)},
                {"status", "cancelled"},
                {"message", "TWAP task cancelled successfully"}
            };
            
            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Failed to cancel TWAP task: " + _twap_algorithm->get_last_error(),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Cancel TWAP task failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_query_orders(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("login_id") || !url_result.has_param("market")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameters: login_id, market",
                req
            );
        }

        // 解析参数
        std::string login_id = url_result.get_param("login_id");
        MarketType market = static_cast<MarketType>(std::stoi(url_result.get_param("market")));
        std::string security_code = url_result.get_param("security_code");
        int64_t order_id = 0;

        OrderQueryCondition condition = ORDER_QUERY_CONDITION_ALL;
        if (url_result.has_param("condition")) {
            condition = static_cast<OrderQueryCondition>(std::stoi(url_result.get_param("condition")));
        }

        ReturnSeqType sort = RETURN_SEQ_DEFAULT;
        if (url_result.has_param("sort")) {
            sort = static_cast<ReturnSeqType>(std::stoi(url_result.get_param("sort")));
        }

        int64_t limit = DEFAULT_RETURN_NUM;
        if (url_result.has_param("limit")) {
            limit = std::stoll(url_result.get_param("limit"));
        }

        if (url_result.has_param("order_id")) {
            order_id = std::stoll(url_result.get_param("order_id"));
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步查询委托
        TradeOrderData* results = nullptr;
        int count = 0;
        char error_msg[TRADE_ERROR_MSG_SIZE] = {0};
        QueryRequestParams params = {};
        params.condition.order_condition = condition;
        params.return_seq = sort;
        params.return_num = limit;
        
        int32_t query_result = trade_query_orders_sync(
            &account, market, 
            security_code.empty() ? nullptr : security_code.c_str(),
            order_id, &params, &results, &count, DEFAULT_TIMEOUT_MS, error_msg
        );

        // ATP特殊错误码处理：100008 - 未找到指定的订单
        // 这种情况下返回空数据而不是错误
        if (query_result == 0 || query_result == ATP_ERROR_NOT_FOUND_ORDER) {
            json response_data = {
                {"login_id", login_id},
                {"total_count", count},
                {"orders", json::array()}
            };

            // 添加查询结果到数组中
            for (int i = 0; i < count; ++i) {
                json order_info = {
                    // 账户信息
                    {"cust_id", results[i].account.cust_id},
                    {"fund_account_id", results[i].account.fund_account_id},
                    {"branch_id", results[i].account.branch_id},
                    
                    // 证券信息
                    {"security_code", results[i].security_info.code},
                    {"security_name", results[i].security_info.name},
                    {"market", static_cast<int>(results[i].security_info.market)},
                    
                    // 订单信息
                    {"request_id", results[i].request_id},
                    {"order_id", results[i].order_id},
                    {"exchange_order_id", results[i].exchange_order_id},
                    {"batch_id", results[i].batch_id},
                    {"side", static_cast<int>(results[i].side)},
                    {"sign", static_cast<int>(results[i].sign)},
                    {"order_type", static_cast<int>(results[i].order_type)},
                    {"status", static_cast<int>(results[i].status)},
                    {"status_msg", results[i].status_msg},
                    
                    // 价格和数量信息
                    {"price", results[i].price},
                    {"qty", results[i].qty},
                    {"leaves_qty", results[i].leaves_qty},
                    {"cum_qty", results[i].cum_qty},
                    {"avg_price", results[i].avg_price},
                    {"cum_amt", results[i].cum_amt},
                    
                    // 时间信息
                    {"order_time", results[i].order_time},
                    {"update_time", results[i].update_time},
                    {"timestamp", results[i].timestamp},
                    
                    // 其他信息
                    {"business_type", results[i].business_type},
                    {"opu_id", results[i].opu_id}
                };
                response_data["orders"].push_back(order_info);
            }

            if (!security_code.empty()) {
                response_data["security_code"] = security_code;
            }
            if (order_id > 0) {
                response_data["order_id"] = order_id;
            }

            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_success_response(response_data, req);
        } else {
            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                std::string(error_msg),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query orders failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_query_trades(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("login_id") || !url_result.has_param("market")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameters: login_id, market",
                req
            );
        }

        // 解析参数
        std::string login_id = url_result.get_param("login_id");
        MarketType market = static_cast<MarketType>(std::stoi(url_result.get_param("market")));
        std::string security_code = url_result.get_param("security_code");
        int64_t order_id = 0;

        TradeQueryCondition condition = TRADE_QUERY_CONDITION_GLOBAL_PARAMS;
        if (url_result.has_param("condition")) {
            condition = static_cast<TradeQueryCondition>(std::stoi(url_result.get_param("condition")));
        }

        ReturnSeqType sort = RETURN_SEQ_DEFAULT;
        if (url_result.has_param("sort")) {
            sort = static_cast<ReturnSeqType>(std::stoi(url_result.get_param("sort")));
        }
        int64_t limit = DEFAULT_RETURN_NUM;
        if (url_result.has_param("limit")) {
            limit = std::stoll(url_result.get_param("limit"));
        }

        if (url_result.has_param("order_id")) {
            order_id = std::stoll(url_result.get_param("order_id"));
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步查询成交
        TradeFillData* results = nullptr;
        int count = 0;
        char error_msg[TRADE_ERROR_MSG_SIZE] = {0};
        QueryRequestParams params = {};
        params.condition.trade_condition = condition;
        params.return_seq = sort;
        params.return_num = limit;
        
        int32_t query_result = trade_query_trades_sync(
            &account, market, 
            security_code.empty() ? nullptr : security_code.c_str(),
            order_id, &params, &results, &count, DEFAULT_TIMEOUT_MS, error_msg
        );

        // ATP特殊错误码处理：100009 - 未找到指定的成交回报
        // 这种情况下返回空数据而不是错误
        if (query_result == 0 || query_result == ATP_ERROR_NOT_FOUND_TER) {
            json response_data = {
                {"login_id", login_id},
                {"total_count", count},
                {"trades", json::array()}
            };

            // 添加查询结果到数组中
            for (int i = 0; i < count; ++i) {
                json trade_info = {
                    // 账户信息
                    {"cust_id", results[i].account.cust_id},
                    {"fund_account_id", results[i].account.fund_account_id},
                    {"branch_id", results[i].account.branch_id},
                    
                    // 证券信息
                    {"security_code", results[i].security_info.code},
                    {"security_name", results[i].security_info.name},
                    {"market", static_cast<int>(results[i].security_info.market)},
                    
                    // 成交信息
                    {"request_id", results[i].request_id},
                    {"trade_id", results[i].trade_id},
                    {"order_id", results[i].order_id},
                    {"exchange_order_id", results[i].exchange_order_id},
                    {"batch_id", results[i].batch_id},
                    {"side", static_cast<int>(results[i].side)},
                    {"order_type", static_cast<int>(results[i].order_type)},
                    {"business_type", results[i].business_type},
                    
                    // 价格和数量信息
                    {"price", results[i].price},
                    {"qty", results[i].qty},
                    {"value", results[i].value},
                    {"fee", results[i].fee},
                    {"currency", results[i].currency},
                    
                    // 时间信息
                    {"trade_time", results[i].trade_time},
                    {"timestamp", results[i].timestamp}
                };
                response_data["trades"].push_back(trade_info);
            }

            if (!security_code.empty()) {
                response_data["security_code"] = security_code;
            }
            if (order_id > 0) {
                response_data["order_id"] = order_id;
            }

            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_success_response(response_data, req);
        } else {
            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                std::string(error_msg),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query trades failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_query_positions(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("login_id") || !url_result.has_param("market")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameters: login_id, market",
                req
            );
        }

        // 解析参数
        std::string login_id = url_result.get_param("login_id");
        MarketType market = static_cast<MarketType>(std::stoi(url_result.get_param("market")));
        std::string security_code = url_result.get_param("security_code");

        int64_t limit = DEFAULT_RETURN_NUM;
        if (url_result.has_param("limit")) {
            limit = std::stoll(url_result.get_param("limit"));
        }

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步查询持仓
        PositionData* results = nullptr;
        int count = 0;
        char error_msg[TRADE_ERROR_MSG_SIZE] = {0};
        QueryRequestParams params = {};
        params.return_num = limit;
        
        int32_t query_result = trade_query_positions_sync(
            &account, market, 
            security_code.empty() ? nullptr : security_code.c_str(),
            &params, &results, &count, DEFAULT_TIMEOUT_MS, error_msg
        );

        // ATP特殊错误码处理：100010 - 未找到股份
        // 这种情况下返回空数据而不是错误
        if (query_result == 0 || query_result == ATP_ERROR_NOT_FOUND_SHARE) {
            json response_data = {
                {"login_id", login_id},
                {"total_count", count},
                {"positions", json::array()}
            };

            // 添加查询结果到数组中
            for (int i = 0; i < count; ++i) {
                json position_info = {
                    // 账户信息
                    {"cust_id", results[i].account.cust_id},
                    {"fund_account_id", results[i].account.fund_account_id},
                    {"branch_id", results[i].account.branch_id},
                    
                    // 证券信息
                    {"security_code", results[i].security_info.code},
                    {"security_name", results[i].security_info.name},
                    {"market", static_cast<int>(results[i].security_info.market)},
                    {"security_type", results[i].security_type},
                    
                    // 持仓信息
                    {"request_id", results[i].request_id},
                    {"total_qty", results[i].total_qty},
                    {"available_qty", results[i].available_qty},
                    {"frozen_qty", results[i].frozen_qty},
                    {"today_buy_qty", results[i].today_buy_qty},
                    {"today_sell_qty", results[i].today_sell_qty},
                    
                    // 价格和盈亏信息
                    {"cost_price", results[i].cost_price},
                    {"market_price", results[i].market_price},
                    {"market_value", results[i].market_value},
                    {"profit_loss", results[i].profit_loss},
                    {"profit_loss_ratio", results[i].profit_loss_ratio},
                    
                    // 时间信息
                    {"update_time", results[i].update_time},
                    {"timestamp", results[i].timestamp}
                };
                response_data["positions"].push_back(position_info);
            }

            if (!security_code.empty()) {
                response_data["security_code"] = security_code;
            }

            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_success_response(response_data, req);
        } else {
            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }
            
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                std::string(error_msg),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query positions failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse TradeApiHandler::handle_query_funds(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查交易服务状态
        if (trade_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Trade service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("login_id") || !url_result.has_param("market")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameters: login_id, market",
                req
            );
        }

        // 解析参数
        std::string login_id = url_result.get_param("login_id");
        MarketType market = static_cast<MarketType>(std::stoi(url_result.get_param("market")));
        std::string currency = url_result.get_param("currency", "CNY");

        // 获取账户信息
        AccountInfo account;
        if (!get_account_by_login_id(login_id, account)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Account not found or not accessible",
                req
            );
        }

        // 同步查询资金
        AccountData* results = nullptr;
        int count = 0;
        char error_msg[TRADE_ERROR_MSG_SIZE] = {0};
        
        int32_t query_result = trade_query_funds_sync(
            &account, market, currency.c_str(), nullptr, &results, &count, DEFAULT_TIMEOUT_MS, error_msg
        );

        if (query_result == 0) {
            json response_data = {
                {"login_id", login_id},
                {"total_count", count},
                {"funds", json::array()}
            };

            // 添加查询结果到数组中
            for (int i = 0; i < count; ++i) {
                json fund_info = {
                    // 账户信息
                    {"cust_id", results[i].account.cust_id},
                    {"fund_account_id", results[i].account.fund_account_id},
                    {"branch_id", results[i].account.branch_id},
                    {"current_account_id", results[i].current_account_id},
                    {"current_market", static_cast<int>(results[i].current_market)},
                    
                    // 资金信息
                    {"request_id", results[i].request_id},
                    {"currency", results[i].currency},
                    {"total_asset", results[i].total_asset},
                    {"cash_balance", results[i].cash_balance},
                    {"available_cash", results[i].available_cash},
                    {"frozen_cash", results[i].frozen_cash},
                    {"market_value", results[i].market_value},
                    {"buying_power", results[i].buying_power},
                    {"withdrawal_cash", results[i].withdrawal_cash},
                    {"profit_loss", results[i].profit_loss},
                    {"profit_loss_ratio", results[i].profit_loss_ratio},
                    
                    // 时间信息
                    {"update_time", results[i].update_time},
                    {"timestamp", results[i].timestamp}
                };
                response_data["funds"].push_back(fund_info);
            }

            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_success_response(response_data, req);
        } else {
            // 释放查询结果内存
            if (results) {
                trade_free_query_results(results, count);
            }

            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                std::string(error_msg),
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query funds failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

// ===== 私有辅助方法实现 =====

bool TradeApiHandler::get_account_by_login_id(const std::string& login_id, AccountInfo& account) {
    // 检查账户是否存在
    if (trade_has_account(login_id.c_str()) != 1) {
        NETWORK_LOG_ERROR("Account not found: {}", login_id);
        return false;
    }

    // 获取账户基本信息
    if (trade_get_account_info_by_id(login_id.c_str(), &account) != 0) {
        NETWORK_LOG_ERROR("Failed to get account info for: {}", login_id);
        return false;
    }

    return true;
}

bool TradeApiHandler::get_stock_info_from_quote(const std::string& security_code, DepthData& depth_data) {
    // 检查行情服务状态
    if (quote_is_running() != 1) {
        NETWORK_LOG_ERROR("Quote service is not running");
        return false;
    }

    // 获取完整深度数据（包含卖一到卖十价格、昨收盘价、市场信息等）
    if (quote_get_depth_data_by_code(security_code.c_str(), &depth_data) == 0) {
        return true;
    }

    // 如果无法获取，返回失败
    NETWORK_LOG_ERROR("Failed to get stock info for security: {}", security_code);
    return false;
}

TradeOrderRequest TradeApiHandler::build_order_request(
    const AccountInfo& account,
    const std::string& security_code,
    MarketType market,
    OrderSide side,
    int64_t quantity,
    double price) {
    
    TradeOrderRequest order = {};
    
    // 设置账户信息
    order.account = account;
    
    // 设置股票信息
    strncpy(order.security_code, security_code.c_str(), sizeof(order.security_code) - 1);
    order.market = market;
    
    // 设置订单信息
    order.side = side;
    order.qty = quantity;
    order.price = price;
    order.order_type = ORDER_TYPE_LIMIT;  // 统一使用限价单
    
    return order;
}

std::chrono::system_clock::time_point TradeApiHandler::parse_iso8601_time(const std::string& time_str) {
    std::istringstream ss(time_str);
    std::tm tm = {};
    
    // 尝试解析 ISO8601 格式: 2024-01-15T09:30:00
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    if (ss.fail()) {
        // 尝试其他格式: 2024-01-15 09:30:00
        ss.clear();
        ss.str(time_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        
        if (ss.fail()) {
            return std::chrono::system_clock::time_point{}; // 返回epoch表示解析失败
        }
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace network

NS_FT_END