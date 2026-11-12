#include "network/handlers/http/strategy_api_handler.h"
#include "network/network_service.h"
#include "types/strategy_types.h"
#include "strategy/strategy_interface.h"
#include "network/error/error.h"
#include "utils/url_parser.h"
#include "logger/logger.hpp"
#include <chrono>

NS_FT_BEGIN

namespace network {

StrategyApiHandler::StrategyApiHandler() : BaseApiHandler("/strategy") {
    NETWORK_LOG_INFO("StrategyApiHandler created with prefix: {}", get_path_prefix());
}

void StrategyApiHandler::register_routes(NetworkService* service) {
    // ===== 策略挂单相关路由 =====
    
    // 创建策略挂单
    service->post(get_full_path("/pending-orders"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_create_pending_order(req, url_result); },
        "Create strategy pending order");

    // 批量创建策略挂单
    service->post(get_full_path("/batch-pending-orders"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_batch_create_pending_orders(req, url_result); },
        "Batch create strategy pending orders");

    // 批量查询策略挂单列表
    service->get(get_full_path("/pending-orders"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_batch_query_pending_orders(req, url_result); },
        "Batch query strategy pending orders");

    // 根据ID查询、更新、删除单个策略挂单
    service->get(get_full_path("/pending-order"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_pending_order_by_id(req, url_result); },
        "Query strategy pending order by ID");

    service->put(get_full_path("/pending-order"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_update_pending_order(req, url_result); },
        "Update strategy pending order by ID");

    service->put(get_full_path("/pending-order/cancel"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_cancel_pending_order(req, url_result); },
        "Cancel strategy pending order by ID");

    service->del(get_full_path("/pending-order"),
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_delete_pending_order(req, url_result); },
        "Delete strategy pending order by ID");

    // ===== 策略订单相关路由 =====
    
    // 批量查询策略订单列表
    service->get(get_full_path("/orders"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_batch_query_orders(req, url_result); },
        "Batch query strategy orders");

    // 根据order_id查询策略订单
    service->get(get_full_path("/order"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_order_by_order_id(req, url_result); },
        "Query strategy order by order_id");

    // 根据策略挂单ID查询订单列表
    service->get(get_full_path("/orders-by-pending-order"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_orders_by_pending_order_id(req, url_result); },
        "Query orders by pending order ID");

    // ===== 联表查询路由 =====
    
    // 查询策略挂单及其关联订单
    service->get(get_full_path("/pending-orders-with-orders"), 
        [this](const HttpRequest& req, const UrlParseResult& url_result) { return handle_query_pending_orders_with_orders(req, url_result); },
        "Query pending orders with associated orders");

    NETWORK_LOG_INFO("StrategyApiHandler routes registered");
}

// ===== 策略挂单API处理函数实现 =====

HttpResponse StrategyApiHandler::handle_create_pending_order(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
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
        if (!request_data.contains("strategy_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: strategy_id",
                req
            );
        }

        std::string strategy_id = request_data["strategy_id"];
        
        // 检查策略实例是否存在且活跃
        if (!validate_strategy_instance(strategy_id, parse_error)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                parse_error,
                req
            );
        }

        // 构造挂单数据（只包含pending_order_info）
        json order_data;
        if (request_data.contains("pending_order_info")) {
            order_data = request_data["pending_order_info"];
        } else {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: pending_order_info",
                req
            );
        }

        // 通过策略服务API创建挂单
        std::string order_json = order_data.dump();
        int result = strategy_add_order_queue(strategy_id.c_str(), order_json.c_str());
        
        if (result != 0) {
            std::string error_msg = strategy_get_last_error();
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to create pending order: " + error_msg,
                req
            );
        }

        // 返回成功响应
        json response_data = {
            {"message", "Strategy pending order created successfully"},
            {"strategy_id", strategy_id}
        };

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Create pending order failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_update_pending_order(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 从传入的 url_result 获取ID
        if (!url_result.has_param("id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: id",
                req
            );
        }

        int64_t order_id = 0;
        try {
            order_id = std::stoll(url_result.get_param("id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid order ID format",
                req
            );
        }

        if (order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Order ID must be greater than 0",
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
        if (!request_data.contains("strategy_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: strategy_id",
                req
            );
        }

        std::string strategy_id = request_data["strategy_id"];
        
        // 检查策略实例是否存在且活跃
        if (!validate_strategy_instance(strategy_id, parse_error)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                parse_error,
                req
            );
        }

        // 构造更新数据
        json order_data;
        if (request_data.contains("pending_order_info")) {
            order_data = request_data["pending_order_info"];
        } else {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: pending_order_info",
                req
            );
        }

        // 通过策略服务API更新挂单
        std::string order_json = order_data.dump();
        int result = strategy_update_order_queue(strategy_id.c_str(), order_id, order_json.c_str());
        
        if (result != 0) {
            std::string error_msg = strategy_get_last_error();
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to update pending order: " + error_msg,
                req
            );
        }

        // 返回成功响应
        json response_data = {
            {"message", "Strategy pending order updated successfully"},
            {"strategy_id", strategy_id},
            {"order_id", order_id}
        };

        NETWORK_LOG_INFO("Updated strategy pending order via strategy service: strategy_id={}, order_id={}", 
                        strategy_id, order_id);
        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Update pending order failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_cancel_pending_order(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 从传入的 url_result 获取ID
        if (!url_result.has_param("id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: id",
                req
            );
        }

        int64_t order_id = 0;
        try {
            order_id = std::stoll(url_result.get_param("id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid order ID format",
                req
            );
        }

        if (order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Order ID must be greater than 0",
                req
            );
        }

        // 需要先从数据库查询挂单信息获取strategy_id
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 查询挂单信息
        auto existing_order = db_manager->query_strategy_pending_order_by_id(order_id);
        if (!existing_order) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Pending order not found",
                req
            );
        }

        if(existing_order->status == PendingOrderStatus::CANCELLED || existing_order->status == PendingOrderStatus::EXECUTED) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::OPERATION_NOT_ALLOWED,
                "Pending order is not active",
                req
            );
        }

        std::string strategy_id = existing_order->strategy_id;
        
        // 检查策略实例是否存在且活跃
        std::string parse_error;
        if (!validate_strategy_instance(strategy_id, parse_error)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                parse_error,
                req
            );
        }

        // 通过策略服务API取消挂单
        int result = strategy_cancel_order_queue(strategy_id.c_str(), order_id);
        
        if (result != 0) {
            std::string error_msg = strategy_get_last_error();
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to cancel pending order: " + error_msg,
                req
            );
        }

        // 返回成功响应
        json response_data = {
            {"message", "Strategy pending order cancelled successfully"},
            {"strategy_id", strategy_id},
            {"cancelled_order_id", order_id}
        };

        NETWORK_LOG_INFO("Cancelled strategy pending order via strategy service: strategy_id={}, order_id={}", 
                        strategy_id, order_id);
        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Cancel pending order failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_delete_pending_order(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 从传入的 url_result 获取ID
        if (!url_result.has_param("id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: id",
                req
            );
        }

        int64_t order_id = 0;
        try {
            order_id = std::stoll(url_result.get_param("id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid order ID format",
                req
            );
        }

        if (order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Order ID must be greater than 0",
                req
            );
        }

        // 需要先从数据库查询挂单信息获取strategy_id
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 查询挂单信息
        auto existing_order = db_manager->query_strategy_pending_order_by_id(order_id);
        if (!existing_order) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Pending order not found",
                req
            );
        }

        std::string strategy_id = existing_order->strategy_id;
        
        // 检查策略实例是否存在且活跃
        std::string parse_error;
        if (!validate_strategy_instance(strategy_id, parse_error)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                parse_error,
                req
            );
        }

        // 通过策略服务API删除挂单
        int result = strategy_remove_order_queue(strategy_id.c_str(), order_id);
        
        if (result != 0) {
            std::string error_msg = strategy_get_last_error();
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to delete pending order: " + error_msg,
                req
            );
        }

        // 返回成功响应
        json response_data = {
            {"message", "Strategy pending order deleted successfully"},
            {"strategy_id", strategy_id},
            {"deleted_order_id", order_id}
        };

        NETWORK_LOG_INFO("Deleted strategy pending order via strategy service: strategy_id={}, order_id={}", 
                        strategy_id, order_id);
        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Delete pending order failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_query_pending_order_by_id(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 从传入的 url_result 获取ID
        if (!url_result.has_param("id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: id",
                req
            );
        }

        int64_t order_id = 0;
        try {
            order_id = std::stoll(url_result.get_param("id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid order ID format",
                req
            );
        }

        if (order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Order ID must be greater than 0",
                req
            );
        }

        // 查询挂单
        auto order_opt = db_manager->query_strategy_pending_order_by_id(order_id);
        if (!order_opt) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Pending order not found",
                req
            );
        }

        // 直接返回挂单信息
        json response_data = pending_order_to_json(*order_opt);

        NETWORK_LOG_INFO("Queried pending order by ID: id={}, strategy_id={}", 
                        order_id, order_opt->strategy_id);

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query pending order by ID failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_batch_query_pending_orders(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 使用传入的 url_result 构建批量查询条件
        StrategyPendingOrderQuery query;
        query.strategy_id = url_result.get_param("strategy_id");

        // 解析时间范围参数
        if (url_result.has_param("start_time")) {
            try {
                query.start_time = std::stoull(url_result.get_param("start_time"));
            } catch (...) {
                query.start_time = 0;
            }
        }

        if (url_result.has_param("end_time")) {
            try {
                query.end_time = std::stoull(url_result.get_param("end_time"));
            } catch (...) {
                query.end_time = 0;
            }
        }

        // 解析分页参数
        if (url_result.has_param("limit")) {
            try {
                query.limit = std::stoi(url_result.get_param("limit"));
                if (query.limit <= 0 || query.limit > MAX_LIMIT) {
                    query.limit = DEFAULT_LIMIT;
                }
            } catch (...) {
                query.limit = DEFAULT_LIMIT;
            }
        }

        if (url_result.has_param("offset")) {
            try {
                query.offset = std::stoi(url_result.get_param("offset"));
                if (query.offset < 0) {
                    query.offset = 0;
                }
            } catch (...) {
                query.offset = 0;
            }
        }

        // 解析排序参数
        if (url_result.has_param("order_by")) {
            query.order_by = url_result.get_param("order_by");
        }

        if (url_result.has_param("desc")) {
            std::string desc_str = url_result.get_param("desc");
            query.desc = (desc_str == "true" || desc_str == "1");
        }

        // 执行批量查询
        StrategyPendingOrderBatchResult result = db_manager->batch_query_strategy_pending_orders(query);

        // 检查查询是否成功
        if (result.total_count == 0 && result.returned_count == 0 && !db_manager->get_last_error().empty()) {
            std::string db_error = db_manager->get_last_error();
            NETWORK_LOG_ERROR("Failed to batch query pending orders: {}", db_error);
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::DATABASE_ERROR,
                "Failed to query pending orders: " + db_error,
                req
            );
        }

        // 直接返回 BatchQueryResult 对应的 JSON 结构
        json response_data = pending_order_batch_result_to_json(result);

        NETWORK_LOG_INFO("Batch queried pending orders: total={}, returned={}, strategy_id={}", 
                        result.total_count, result.returned_count, query.strategy_id);

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Batch query pending orders failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_batch_create_pending_orders(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
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
        if (!request_data.contains("strategy_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: strategy_id",
                req
            );
        }

        if (!request_data.contains("pending_orders")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required field: pending_orders",
                req
            );
        }

        std::string strategy_id = request_data["strategy_id"];
        
        // 检查策略实例是否存在且活跃
        if (!validate_strategy_instance(strategy_id, parse_error)) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                parse_error,
                req
            );
        }

        // 验证pending_orders是数组
        if (!request_data["pending_orders"].is_array()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "pending_orders must be an array",
                req
            );
        }

        auto pending_orders = request_data["pending_orders"];
        if (pending_orders.empty()) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "pending_orders array cannot be empty",
                req
            );
        }

        // 限制批量创建的数量
        if (pending_orders.size() > 1000) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Cannot create more than 1000 pending orders at once",
                req
            );
        }

        // 通过策略服务API批量创建挂单
        std::string orders_json = pending_orders.dump();
        int ret = strategy_batch_add_order_queue(strategy_id.c_str(), orders_json.c_str());
        
        if (ret < 0) {
            std::string error_msg = strategy_get_last_error();
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::INTERNAL_SERVER_ERROR,
                "Failed to batch create pending orders: " + error_msg,
                req
            );
        }

        // 构造响应数据
        json response_data = {
            {"strategy_id", strategy_id},
            {"total", pending_orders.size()}
        };

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Batch create pending orders failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

// ===== 策略订单API处理函数实现 =====

HttpResponse StrategyApiHandler::handle_query_order_by_order_id(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 从传入的 url_result 获取order_id
        if (!url_result.has_param("order_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: order_id",
                req
            );
        }

        int64_t order_id = 0;
        try {
            order_id = std::stoll(url_result.get_param("order_id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid order_id format",
                req
            );
        }

        if (order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Order ID must be greater than 0",
                req
            );
        }

        // 查询订单
        auto order_opt = db_manager->query_strategy_order_by_order_id(order_id);
        if (!order_opt) {
            return create_error_response(
                http::status::not_found,
                ErrorCode::RESOURCE_NOT_FOUND,
                "Strategy order not found",
                req
            );
        }

        // 直接返回订单信息
        json response_data = strategy_order_to_json(*order_opt);

        NETWORK_LOG_INFO("Queried strategy order by order_id: order_id={}, strategy_id={}", 
                        order_id, order_opt->strategy_id);

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query strategy order by order_id failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_batch_query_orders(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 使用传入的 url_result 构建批量查询条件
        StrategyOrderQuery query;
        query.strategy_id = url_result.get_param("strategy_id");
        query.security_code = url_result.get_param("security_code");

        // 解析pending_order_id参数
        if (url_result.has_param("pending_order_id")) {
            try {
                query.pending_order_id = std::stoll(url_result.get_param("pending_order_id"));
            } catch (...) {
                query.pending_order_id = 0;
            }
        }

        // 解析market参数
        if (url_result.has_param("market")) {
            try {
                query.market = std::stoi(url_result.get_param("market"));
            } catch (...) {
                query.market = 0; // 默认所有市场
            }
        }

        // 解析status参数
        if (url_result.has_param("status")) {
            try {
                query.status = static_cast<OrderStatus>(std::stoi(url_result.get_param("status")));
            } catch (...) {
                query.status = ORDER_STATUS_UNKNOWN;
            }
        }

        // 解析时间范围参数
        if (url_result.has_param("start_time")) {
            try {
                query.start_time = std::stoull(url_result.get_param("start_time"));
            } catch (...) {
                query.start_time = 0;
            }
        }

        if (url_result.has_param("end_time")) {
            try {
                query.end_time = std::stoull(url_result.get_param("end_time"));
            } catch (...) {
                query.end_time = 0;
            }
        }

        // 解析分页参数
        if (url_result.has_param("limit")) {
            try {
                query.limit = std::stoi(url_result.get_param("limit"));
                if (query.limit <= 0 || query.limit > MAX_LIMIT) {
                    query.limit = DEFAULT_LIMIT;
                }
            } catch (...) {
                query.limit = DEFAULT_LIMIT;
            }
        }

        if (url_result.has_param("offset")) {
            try {
                query.offset = std::stoi(url_result.get_param("offset"));
                if (query.offset < 0) {
                    query.offset = 0;
                }
            } catch (...) {
                query.offset = 0;
            }
        }

        // 解析排序参数
        if (url_result.has_param("order_by")) {
            query.order_by = url_result.get_param("order_by");
        }

        if (url_result.has_param("desc")) {
            std::string desc_str = url_result.get_param("desc");
            query.desc = (desc_str == "true" || desc_str == "1");
        }

        // 执行批量查询
        StrategyOrderBatchResult result = db_manager->batch_query_strategy_orders(query);

        // 检查查询是否成功
        if (result.total_count == 0 && result.returned_count == 0 && !db_manager->get_last_error().empty()) {
            std::string db_error = db_manager->get_last_error();
            NETWORK_LOG_ERROR("Failed to batch query strategy orders: {}", db_error);
            return create_error_response(
                http::status::internal_server_error,
                ErrorCode::DATABASE_ERROR,
                "Failed to query strategy orders: " + db_error,
                req
            );
        }

        // 直接返回 BatchQueryResult 对应的 JSON 结构
        json response_data = strategy_order_batch_result_to_json(result);

        NETWORK_LOG_INFO("Batch queried strategy orders: total={}, returned={}, strategy_id={}", 
                        result.total_count, result.returned_count, query.strategy_id);

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Batch query strategy orders failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

HttpResponse StrategyApiHandler::handle_query_orders_by_pending_order_id(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 从传入的 url_result 获取pending_order_id
        if (!url_result.has_param("pending_order_id")) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Missing required parameter: pending_order_id",
                req
            );
        }

        int64_t pending_order_id = 0;
        try {
            pending_order_id = std::stoll(url_result.get_param("pending_order_id"));
        } catch (...) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid pending_order_id format",
                req
            );
        }

        if (pending_order_id <= 0) {
            return create_error_response(
                http::status::bad_request,
                ErrorCode::INVALID_ARGUMENT,
                "Pending order ID must be greater than 0",
                req
            );
        }

        // 查询关联的订单
        auto orders = db_manager->query_strategy_orders_by_pending_order_id(pending_order_id);

        // 转换为JSON数组
        json orders_array = json::array();
        for (const auto& order : orders) {
            orders_array.push_back(strategy_order_to_json(order));
        }

        json response_data = {
            {"pending_order_id", pending_order_id},
            {"orders", orders_array},
            {"count", orders.size()}
        };

        NETWORK_LOG_INFO("Queried orders by pending order ID: pending_order_id={}, count={}", 
                        pending_order_id, orders.size());

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query orders by pending order ID failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

// ===== 联表查询API处理函数实现 =====

HttpResponse StrategyApiHandler::handle_query_pending_orders_with_orders(const HttpRequest& req, const UrlParseResult& url_result) {
    try {
        // 获取数据库管理器
        DatabaseManager* db_manager = get_db_manager();
        if (!db_manager) {
            return create_error_response(
                http::status::service_unavailable,
                ErrorCode::SERVICE_UNAVAILABLE,
                "Database service is not available",
                req
            );
        }

        // 使用传入的 url_result 获取查询参数
        std::string strategy_id = url_result.get_param("strategy_id");

        // 执行联表查询
        auto results = db_manager->query_strategy_pending_orders_with_orders(strategy_id);

        // 转换为JSON数组
        json results_array = json::array();
        for (const auto& result : results) {
            results_array.push_back(pending_order_with_orders_to_json(result));
        }

        json response_data = {
            {"pending_orders_with_orders", results_array},
            {"count", results.size()}
        };

        if (!strategy_id.empty()) {
            response_data["strategy_id"] = strategy_id;
        }

        NETWORK_LOG_INFO("Queried pending orders with orders: strategy_id={}, count={}", 
                        strategy_id.empty() ? "all" : strategy_id, results.size());

        return create_success_response(response_data, req);

    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Query pending orders with orders failed: {}", e.what());
        return create_error_response(
            http::status::internal_server_error,
            ErrorCode::INTERNAL_SERVER_ERROR,
            "Internal server error",
            req
        );
    }
}

// ===== 私有辅助方法实现 =====

bool StrategyApiHandler::json_to_pending_order(const json& json_data, StrategyPendingOrder& order) {
    try {
        // 必需字段：策略ID
        if (!json_data.contains("strategy_id")) {
            return false;
        }

        order.strategy_id = json_data["strategy_id"];

        // 处理策略信息JSON
        if (json_data.contains("pending_order_info")) {
            if (json_data["pending_order_info"].is_string()) {
                order.pending_order_info = json_data["pending_order_info"];
            } else {
                order.pending_order_info = json_data["pending_order_info"].dump();
            }
        } else {
            order.pending_order_info = "{}";
        }

        // 处理状态字段
        if (json_data.contains("status")) {
            int status_int = json_data["status"];
            if (status_int >= 1 && status_int <= 4) {
                order.status = static_cast<PendingOrderStatus>(status_int);
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

json StrategyApiHandler::pending_order_to_json(const StrategyPendingOrder& order) {
    json result = {
        {"id", order.id},
        {"strategy_id", order.strategy_id},
        {"status", static_cast<int>(order.status)},
        {"create_time", order.create_time},
        {"update_time", order.update_time}
    };

    // 解析并展开pending_order_info
    if (!order.pending_order_info.empty()) {
        try {
            json pending_info = json::parse(order.pending_order_info);
            result["pending_order_info"] = pending_info;
        } catch (...) {
            result["pending_order_info"] = order.pending_order_info;
        }
    } else {
        result["pending_order_info"] = json::object();
    }

    return result;
}

json StrategyApiHandler::strategy_order_to_json(const StrategyOrder& order) {
    json result = {
        {"id", order.id},
        {"pending_order_id", order.pending_order_id},
        {"strategy_id", order.strategy_id},
        {"pending_submit_time", order.pending_submit_time},
        {"security_code", order.security_code},
        {"order_id", order.order_id},
        {"exchange_order_id", order.exchange_order_id},
        {"batch_id", order.batch_id},
        {"side", static_cast<int>(order.side)},
        {"sign", static_cast<int>(order.sign)},
        {"order_type", static_cast<int>(order.order_type)},
        {"status", static_cast<int>(order.status)},
        {"status_msg", order.status_msg},
        {"price", order.price},
        {"qty", order.qty},
        {"leaves_qty", order.leaves_qty},
        {"cum_qty", order.cum_qty},
        {"avg_price", order.avg_price},
        {"cum_amt", order.cum_amt},
        {"order_time", order.order_time},
        {"update_time", order.update_time},
        {"create_time", order.create_time},
        {"error_code", order.error_code},
        {"error_msg", order.error_msg}
    };

    // 添加账户信息
    if (!order.login_id.empty()) result["login_id"] = order.login_id;
    if (!order.cust_id.empty()) result["cust_id"] = order.cust_id;
    if (!order.fund_account_id.empty()) result["fund_account_id"] = order.fund_account_id;
    if (!order.account_id.empty()) result["account_id"] = order.account_id;

    // 解析并展开pending_order_info
    if (!order.pending_order_info.empty()) {
        try {
            json pending_info = json::parse(order.pending_order_info);
            result["pending_order_info"] = pending_info;
        } catch (...) {
            result["pending_order_info"] = order.pending_order_info;
        }
    } else {
        result["pending_order_info"] = json::object();
    }

    return result;
}

json StrategyApiHandler::pending_order_batch_result_to_json(const StrategyPendingOrderBatchResult& result) {
    json orders_array = json::array();
    for (const auto& order : result.records) {
        orders_array.push_back(pending_order_to_json(order));
    }

    return json{
        {"records", orders_array},
        {"total_count", result.total_count},
        {"returned_count", result.returned_count},
        {"has_more", result.has_more}
    };
}

json StrategyApiHandler::strategy_order_batch_result_to_json(const StrategyOrderBatchResult& result) {
    json orders_array = json::array();
    for (const auto& order : result.records) {
        orders_array.push_back(strategy_order_to_json(order));
    }

    return json{
        {"records", orders_array},
        {"total_count", result.total_count},
        {"returned_count", result.returned_count},
        {"has_more", result.has_more}
    };
}

json StrategyApiHandler::pending_order_with_orders_to_json(const StrategyPendingOrderWithOrders& result) {
    json orders_array = json::array();
    for (const auto& order : result.orders) {
        orders_array.push_back(strategy_order_to_json(order));
    }

    return json{
        {"pending_order", pending_order_to_json(result.pending_order)},
        {"orders", orders_array},
        {"order_count", result.orders.size()}
    };
}

bool StrategyApiHandler::validate_pending_order(const StrategyPendingOrder& order, std::string& error_msg) {
    if (order.strategy_id.empty()) {
        error_msg = "Strategy ID cannot be empty";
        return false;
    }
    
    return true;
}

bool StrategyApiHandler::validate_strategy_instance(const std::string& strategy_id, std::string& error_msg) {
    if (!strategy_instance_exists(strategy_id.c_str())) {
        error_msg = "Strategy instance not found: " + strategy_id;
        return false;
    }
    
    if (!strategy_instance_is_active(strategy_id.c_str())) {
        error_msg = "Strategy instance is not active: " + strategy_id;
        return false;
    }
    
    return true;
}

DatabaseManager* StrategyApiHandler::get_db_manager() {
    return static_cast<DatabaseManager*>(_db_manager);
}

uint64_t StrategyApiHandler::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace network

NS_FT_END