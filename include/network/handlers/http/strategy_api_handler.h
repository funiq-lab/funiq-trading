#pragma once

#include "base_api_handler.h"
#include "database/database_manager.hpp"
#include "types/strategy_types.h"
#include "utils/url_parser.h"

NS_FT_BEGIN

namespace network {

/**
 * @brief 策略API处理器
 * 
 * 提供策略挂单和策略订单的管理功能，支持完整的生命周期管理。
 * 
 * 功能范围：
 * 1. 策略挂单管理：创建、更新、查询、删除、批量查询
 * 2. 策略订单查询：查询具体的订单执行信息
 * 3. 联表查询：查询策略挂单及其关联的订单信息
 * 
 * 路径前缀：/strategy
 */
class StrategyApiHandler : public BaseApiHandler {
public:
    /**
     * @brief 构造策略API处理器
     */
    StrategyApiHandler();
    
    /**
     * @brief 析构函数
     */
    ~StrategyApiHandler() override = default;

    /**
     * @brief 注册策略相关路由
     * 
     * @param service 网络服务实例指针，用于注册路由
     */
    void register_routes(NetworkService* service) override;

private:
    // ===== 策略挂单API处理函数 =====

    /**
     * @brief 处理创建策略挂单请求
     *
     * POST /strategy/pending-orders
     *
     * 请求体：{
     *   "strategy_id": "stop_loss_001",
     *   "pending_order_info": "{\"stop_price\": 11.0, \"sectors\": [\"金融\"]}"
     * }
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含创建结果和挂单ID
     */
    HttpResponse handle_create_pending_order(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理批量创建策略挂单请求
     *
     * POST /strategy/batch-pending-orders
     *
     * 请求体：{
     *   "strategy_id": "example_pending_order",
     *   "pending_orders": [
     *     { "example_note": "user-defined strategy payload" }
     *   ]
     * }
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含批量创建结果
     */
    HttpResponse handle_batch_create_pending_orders(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理更新策略挂单请求
     *
     * PUT /strategy/pending-order?id={id}
     *
     * 查询参数：
     * - id（必需）：挂单ID
     *
     * 请求体：{
     *   "strategy_id": "stop_loss_001",
     *   "pending_order_info": "{\"stop_price\": 12.0}"
     * }
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含更新结果
     */
    HttpResponse handle_update_pending_order(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理根据ID查询单个策略挂单请求
     *
     * GET /strategy/pending-order?id={id}
     *
     * 查询参数：
     * - id（必需）：挂单ID
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含单个挂单信息
     */
    HttpResponse handle_query_pending_order_by_id(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理取消策略挂单请求
     *
     * PUT /strategy/pending-order/cancel?id={id}
     *
     * 查询参数：
     * - id（必需）：挂单ID
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含取消结果
     */
    HttpResponse handle_cancel_pending_order(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理删除策略挂单请求
     *
     * DELETE /strategy/pending-order?id={id}
     *
     * 查询参数：
     * - id（必需）：挂单ID
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含删除结果
     */
    HttpResponse handle_delete_pending_order(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理批量查询策略挂单列表请求
     *
     * GET /strategy/pending-orders?strategy_id=xxx&limit=10&offset=0
     *
     * 查询参数：
     * - strategy_id（可选）：策略ID筛选
     * - start_time（可选）：开始时间戳
     * - end_time（可选）：结束时间戳
     * - limit（可选）：返回记录数限制，默认10，最大1000
     * - offset（可选）：偏移量，用于分页，默认0
     * - order_by（可选）：排序字段，默认submit_time
     * - desc（可选）：是否降序，默认true
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，直接返回 StrategyPendingOrderBatchResult 结构
     */
    HttpResponse handle_batch_query_pending_orders(const HttpRequest& req, const UrlParseResult& url_result);


    // ===== 策略订单API处理函数 =====

    /**
     * @brief 处理根据order_id查询策略订单请求
     *
     * GET /strategy/order?order_id={order_id}
     *
     * 查询参数：
     * - order_id（必需）：订单ID
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含单个订单信息
     */
    HttpResponse handle_query_order_by_order_id(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理批量查询策略订单列表请求
     *
     * GET /strategy/orders?strategy_id=xxx&pending_order_id=xxx&security_code=xxx&status=xxx
     *
     * 查询参数：
     * - strategy_id（可选）：策略ID筛选
     * - pending_order_id（可选）：策略挂单ID筛选
     * - security_code（可选）：股票代码筛选
     * - market（可选）：市场筛选（0=所有，1=上交所，2=深交所）
     * - status（可选）：订单状态筛选
     * - start_time（可选）：开始时间戳（基于create_time）
     * - end_time（可选）：结束时间戳（基于create_time）
     * - limit（可选）：返回记录数限制，默认10，最大100
     * - offset（可选）：偏移量，用于分页，默认0
     * - order_by（可选）：排序字段，默认create_time
     * - desc（可选）：是否降序，默认true
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，直接返回 StrategyOrderBatchResult 结构
     */
    HttpResponse handle_batch_query_orders(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理根据策略挂单ID查询策略订单列表请求
     *
     * GET /strategy/orders-by-pending-order?pending_order_id={pending_order_id}
     *
     * 查询参数：
     * - pending_order_id（必需）：策略挂单ID
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含该挂单关联的所有订单
     */
    HttpResponse handle_query_orders_by_pending_order_id(const HttpRequest& req, const UrlParseResult& url_result);

    // ===== 联表查询API处理函数 =====

    /**
     * @brief 处理查询策略挂单及其关联订单请求
     *
     * GET /strategy/pending-orders-with-orders?strategy_id=xxx
     *
     * 查询参数：
     * - strategy_id（可选）：策略ID筛选，为空则查询所有
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含挂单及其关联的订单信息
     */
    HttpResponse handle_query_pending_orders_with_orders(const HttpRequest& req, const UrlParseResult& url_result);

    // ===== 私有辅助方法 =====

    /**
     * @brief 从JSON对象构造StrategyPendingOrder
     * 
     * @param json_data JSON数据
     * @param order 输出的挂单对象
     * @return true 成功，false 失败
     */
    bool json_to_pending_order(const json& json_data, StrategyPendingOrder& order);

    /**
     * @brief 将StrategyPendingOrder转换为JSON对象
     * 
     * @param order 挂单对象
     * @return JSON对象
     */
    json pending_order_to_json(const StrategyPendingOrder& order);

    /**
     * @brief 将StrategyOrder转换为JSON对象
     * 
     * @param order 订单对象
     * @return JSON对象
     */
    json strategy_order_to_json(const StrategyOrder& order);

    /**
     * @brief 将StrategyPendingOrderBatchResult转换为JSON对象
     * 
     * @param result 批量查询结果
     * @return JSON对象
     */
    json pending_order_batch_result_to_json(const StrategyPendingOrderBatchResult& result);

    /**
     * @brief 将StrategyOrderBatchResult转换为JSON对象
     * 
     * @param result 批量查询结果
     * @return JSON对象
     */
    json strategy_order_batch_result_to_json(const StrategyOrderBatchResult& result);

    /**
     * @brief 将StrategyPendingOrderWithOrders转换为JSON对象
     * 
     * @param result 联表查询结果
     * @return JSON对象
     */
    json pending_order_with_orders_to_json(const StrategyPendingOrderWithOrders& result);

    /**
     * @brief 验证挂单数据的有效性
     * 
     * @param order 挂单对象
     * @param error_msg 输出的错误信息
     * @return true 有效，false 无效
     */
    bool validate_pending_order(const StrategyPendingOrder& order, std::string& error_msg);

    /**
     * @brief 获取数据库管理器实例
     * 
     * @return DatabaseManager指针，如果不可用则返回nullptr
     */
    DatabaseManager* get_db_manager();

    /**
     * @brief 获取当前时间戳（毫秒）
     * 
     * @return 当前时间戳
     */
    uint64_t get_current_timestamp();

    /**
     * @brief 验证策略实例是否存在且活跃
     * 
     * @param strategy_id 策略ID
     * @param error_msg 输出的错误信息
     * @return true 有效，false 无效
     */
    bool validate_strategy_instance(const std::string& strategy_id, std::string& error_msg);

private:
    static constexpr int DEFAULT_LIMIT = 10;        ///< 默认查询记录数限制
    static constexpr int MAX_LIMIT = 1000;           ///< 最大查询记录数限制
};

} // namespace network

NS_FT_END