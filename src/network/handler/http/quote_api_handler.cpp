#include "network/handlers/http/quote_api_handler.h"
#include "network/network_service.h"
#include "quote/quote_interface.h"
#include "utils/url_parser.h"
#include "logger/logger.hpp"
#include "network/error/error.h"

NS_FT_BEGIN

namespace network {

QuoteApiHandler::QuoteApiHandler() : BaseApiHandler("/quote") {
    NETWORK_LOG_INFO("QuoteApiHandler created");
}

void QuoteApiHandler::register_routes(NetworkService* service) {
    // 深度行情查询
    service->get(get_full_path("/depth"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_depth_data(req, url_result); },
        "Get depth quote data by security code");

    // 指数数据查询
    service->get(get_full_path("/index"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_index_data(req, url_result); },
        "Get index data by security code");

    // 五十档深度查询
    service->get(get_full_path("/depth-fifty"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_depth_fifty_data(req, url_result); },
        "Get fifty-level depth data by security code");

    // 委托数据查询
    service->get(get_full_path("/entrust"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_entrust_data(req, url_result); },
        "Get entrust data by security code");

    // 成交数据查询
    service->get(get_full_path("/trade"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_get_trade_data(req, url_result); },
        "Get trade data by security code");

    NETWORK_LOG_INFO("QuoteApiHandler routes registered");
}

HttpResponse QuoteApiHandler::handle_get_depth_data(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查行情服务状态
        if (quote_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Quote service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("security_code")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: security_code",
                req
            );
        }

        // 解析参数
        std::string security_code = url_result.get_param("security_code");

        // 验证股票代码
        if (!is_valid_security_code(security_code)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid security code format",
                req
            );
        }

        // 获取深度行情数据
        DepthData depth_data = {};
        int32_t result = quote_get_depth_data_by_code(security_code.c_str(), &depth_data);

        if (result == 0) {
            json response_data = {
                // 证券信息
                {"security_code", depth_data.security_info.code},
                {"security_name", depth_data.security_info.name},
                {"market", static_cast<int>(depth_data.security_info.market)},
                
                // 时间信息
                {"timestamp", depth_data.timestamp},
                {"update_time", depth_data.update_time},
                {"trading_phase", static_cast<int>(depth_data.trading_phase)},
                {"instrument_status", depth_data.instrument_status},
                
                // 价格信息
                {"last_price", depth_data.last_price},
                {"pre_close_price", depth_data.pre_close_price},
                {"open_price", depth_data.open_price},
                {"high_price", depth_data.high_price},
                {"low_price", depth_data.low_price},
                {"close_price", depth_data.close_price},
                {"upper_limit_price", depth_data.upper_limit_price},
                {"lower_limit_price", depth_data.lower_limit_price},
                {"avg_price", depth_data.avg_price},
                
                // 成交信息
                {"total_volume", depth_data.total_volume},
                {"total_value", depth_data.total_value},
                {"num_trades", depth_data.num_trades},
                
                // 委托信息
                {"total_bid_qty", depth_data.total_bid_qty},
                {"total_ask_qty", depth_data.total_ask_qty},
                {"weighted_avg_bid_price", depth_data.weighted_avg_bid_price},
                {"weighted_avg_ask_price", depth_data.weighted_avg_ask_price},
                {"total_bid_number", depth_data.total_bid_number},
                {"total_ask_number", depth_data.total_ask_number},
                
                // 十档行情
                {"bid_price", json::array()},
                {"bid_qty", json::array()},
                {"ask_price", json::array()},
                {"ask_qty", json::array()},
                
                // 特有字段
                {"total_position", depth_data.total_position},
                {"reference_price", depth_data.reference_price},
                {"iopv", depth_data.iopv},
                {"pre_iopv", depth_data.pre_iopv},
                
                // 技术字段
                {"channel_no", depth_data.channel_no},
                {"sequence", depth_data.sequence}
            };

            // 添加十档买盘数据
            for (int i = 0; i < 10; ++i) {
                response_data["bid_price"].push_back(depth_data.bid_price[i]);
                response_data["bid_qty"].push_back(depth_data.bid_qty[i]);
            }

            // 添加十档卖盘数据
            for (int i = 0; i < 10; ++i) {
                response_data["ask_price"].push_back(depth_data.ask_price[i]);
                response_data["ask_qty"].push_back(depth_data.ask_qty[i]);
            }

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Get depth data failed",
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get depth data failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse QuoteApiHandler::handle_get_index_data(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查行情服务状态
        if (quote_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Quote service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("security_code")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: security_code",
                req
            );
        }

        // 解析参数
        std::string security_code = url_result.get_param("security_code");

        // 验证股票代码
        if (!is_valid_security_code(security_code)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid security code format",
                req
            );
        }

        // 获取指数数据
        IndexData index_data = {};
        int32_t result = quote_get_index_data_by_code(security_code.c_str(), &index_data);

        if (result == 0) {
            json response_data = {
                // 证券信息
                {"security_code", index_data.security_info.code},
                {"security_name", index_data.security_info.name},
                {"market", static_cast<int>(index_data.security_info.market)},
                
                // 时间信息
                {"timestamp", index_data.timestamp},
                {"update_time", index_data.update_time},
                {"trading_phase", static_cast<int>(index_data.trading_phase)},
                
                // 指数信息
                {"last_index", index_data.last_index},
                {"pre_close_index", index_data.pre_close_index},
                {"open_index", index_data.open_index},
                {"high_index", index_data.high_index},
                {"low_index", index_data.low_index},
                {"close_index", index_data.close_index},
                
                // 成交信息
                {"total_volume", index_data.total_volume},
                {"total_value", index_data.total_value},
                {"num_trades", index_data.num_trades},
                
                // 技术字段
                {"sequence", index_data.sequence}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Get index data failed",
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get index data failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse QuoteApiHandler::handle_get_depth_fifty_data(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查行情服务状态
        if (quote_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Quote service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("security_code")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: security_code",
                req
            );
        }

        // 解析参数
        std::string security_code = url_result.get_param("security_code");

        // 验证股票代码
        if (!is_valid_security_code(security_code)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid security code format",
                req
            );
        }

        // 获取五十档深度数据
        DepthFiftyData depth_fifty_data = {};
        int32_t result = quote_get_depth_fifty_data_by_code(security_code.c_str(), &depth_fifty_data);

        if (result == 0) {
            json response_data = {
                // 证券信息
                {"security_code", depth_fifty_data.security_info.code},
                {"security_name", depth_fifty_data.security_info.name},
                {"market", static_cast<int>(depth_fifty_data.security_info.market)},
                
                // 时间信息
                {"timestamp", depth_fifty_data.timestamp},
                {"update_time", depth_fifty_data.update_time},
                
                // 买一档信息
                {"bid_price", depth_fifty_data.bid_price},
                {"bid_qty", depth_fifty_data.bid_qty},
                {"bid_num_orders", depth_fifty_data.bid_num_orders},
                {"bid_show_orders", depth_fifty_data.bid_show_orders},
                
                // 卖一档信息
                {"ask_price", depth_fifty_data.ask_price},
                {"ask_qty", depth_fifty_data.ask_qty},
                {"ask_num_orders", depth_fifty_data.ask_num_orders},
                {"ask_show_orders", depth_fifty_data.ask_show_orders},
                
                // 50笔委托明细
                {"orders", json::array()},
                
                // 技术字段
                {"channel_no", depth_fifty_data.channel_no},
                {"sequence", depth_fifty_data.sequence}
            };

            // 添加100笔委托明细(买卖各50笔)
            for (int i = 0; i < 100; ++i) {
                response_data["orders"].push_back(depth_fifty_data.orders[i]);
            }

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Get depth fifty data failed",
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get depth fifty data failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse QuoteApiHandler::handle_get_entrust_data(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查行情服务状态
        if (quote_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Quote service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("security_code")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: security_code",
                req
            );
        }

        // 解析参数
        std::string security_code = url_result.get_param("security_code");

        // 验证股票代码
        if (!is_valid_security_code(security_code)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid security code format",
                req
            );
        }

        // 获取委托数据
        EntrustData entrust_data = {};
        int32_t result = quote_get_entrust_data_by_code(security_code.c_str(), &entrust_data);

        if (result == 0) {
            json response_data = {
                // 证券信息
                {"security_code", entrust_data.security_info.code},
                {"security_name", entrust_data.security_info.name},
                {"market", static_cast<int>(entrust_data.security_info.market)},
                
                // 时间信息
                {"timestamp", entrust_data.timestamp},
                {"transact_time", entrust_data.transact_time},
                
                // 委托信息
                {"order_type", entrust_data.order_type},
                {"side", entrust_data.side},
                {"bs_flag", entrust_data.bs_flag},
                {"order_no", entrust_data.order_no},
                {"order_index", entrust_data.order_index},
                {"price", entrust_data.price},
                {"qty", entrust_data.qty},
                {"balance", entrust_data.balance},
                
                // 技术字段
                {"channel_no", entrust_data.channel_no},
                {"biz_index", entrust_data.biz_index},
                {"appl_seq_num", entrust_data.appl_seq_num},
                {"sequence", entrust_data.sequence}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Get entrust data failed",
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get entrust data failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse QuoteApiHandler::handle_get_trade_data(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 检查行情服务状态
        if (quote_is_running() != 1) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Quote service is not running",
                req
            );
        }
        
        // 验证必需字段
        if (!url_result.has_param("security_code")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: security_code",
                req
            );
        }

        // 解析参数
        std::string security_code = url_result.get_param("security_code");

        // 验证股票代码
        if (!is_valid_security_code(security_code)) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid security code format",
                req
            );
        }

        // 获取成交数据
        TradeData trade_data = {};
        int32_t result = quote_get_trade_data_by_code(security_code.c_str(), &trade_data);

        if (result == 0) {
            json response_data = {
                // 证券信息
                {"security_code", trade_data.security_info.code},
                {"security_name", trade_data.security_info.name},
                {"market", static_cast<int>(trade_data.security_info.market)},
                
                // 时间信息
                {"timestamp", trade_data.timestamp},
                {"transact_time", trade_data.transact_time},
                
                // 成交信息
                {"trade_id", trade_data.trade_id},
                {"trade_index", trade_data.trade_index},
                {"price", trade_data.price},
                {"qty", trade_data.qty},
                {"value", trade_data.value},
                
                // 订单信息
                {"bid_order_no", trade_data.bid_order_no},
                {"ask_order_no", trade_data.ask_order_no},
                {"bid_appl_seq_num", trade_data.bid_appl_seq_num},
                {"ask_appl_seq_num", trade_data.ask_appl_seq_num},
                
                // 成交标志
                {"exec_type", trade_data.exec_type},
                {"trade_flag", trade_data.trade_flag},
                
                // 技术字段
                {"channel_no", trade_data.channel_no},
                {"biz_index", trade_data.biz_index},
                {"appl_seq_num", trade_data.appl_seq_num},
                {"sequence", trade_data.sequence}
            };

            return create_success_response(response_data, req);
        } else {
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Get trade data failed",
                req
            );
        }

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get trade data failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

// ===== 私有辅助方法实现 =====

bool QuoteApiHandler::is_valid_security_code(const std::string& security_code) {
    // 简单的股票代码验证：6位数字
    if (security_code.length() != 6) {
        return false;
    }
    
    for (char c : security_code) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    
    return true;
}

} // namespace network

NS_FT_END