#pragma once

#include "base_api_handler.h"
#include "types/quote_types.h"
#include "utils/url_parser.h"

NS_FT_BEGIN

namespace network {

/**
 * @brief 行情API处理器
 * 
 * 提供简化的行情查询接口，根据股票代码获取各种行情信息。
 * 
 * 功能范围：
 * - 根据代码获取深度行情
 * - 根据代码获取指数数据
 * - 根据代码获取五十档深度
 * - 根据代码获取委托数据
 * - 根据代码获取成交数据
 * 
 * 路径前缀：/quote
 */
class QuoteApiHandler : public BaseApiHandler {
public:
    /**
     * @brief 构造行情API处理器
     */
    QuoteApiHandler();
    
    /**
     * @brief 析构函数
     */
    ~QuoteApiHandler() override = default;

    /**
     * @brief 注册行情相关路由
     * 
     * @param service 网络服务实例指针，用于注册路由
     */
    void register_routes(NetworkService* service) override;

private:
    // ===== 行情API处理函数 =====

    /**
     * @brief 处理深度行情查询请求
     * 
     * 请求体：{
     *   "security_code": "000001"
     * }
     * 
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含深度行情数据
     */
    HttpResponse handle_get_depth_data(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理指数数据查询请求
     * 
     * 请求体：{
     *   "security_code": "000001"
     * }
     * 
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含指数数据
     */
    HttpResponse handle_get_index_data(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理五十档深度查询请求
     * 
     * 请求体：{
     *   "security_code": "000001"
     * }
     * 
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含五十档深度数据
     */
    HttpResponse handle_get_depth_fifty_data(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理委托数据查询请求
     * 
     * 请求体：{
     *   "security_code": "000001"
     * }
     * 
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含委托数据
     */
    HttpResponse handle_get_entrust_data(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理成交数据查询请求
     * 
     * 请求体：{
     *   "security_code": "000001"
     * }
     * 
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含成交数据
     */
    HttpResponse handle_get_trade_data(const HttpRequest& req, const UrlParseResult& url_result);

    // ===== 私有辅助方法 =====

    /**
     * @brief 验证股票代码格式
     * 
     * @param security_code 股票代码
     * @return true 有效，false 无效
     */
    bool is_valid_security_code(const std::string& security_code);

private:
    static constexpr int DEFAULT_TIMEOUT_MS = 30000;  ///< 默认超时时间（毫秒）
};

} // namespace network

NS_FT_END