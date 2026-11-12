#pragma once

#include "base_api_handler.h"
#include "utils/url_parser.h"

NS_FT_BEGIN

namespace network {

/**
 * @brief 系统API处理器
 * 
 * 专门处理交易系统级别的管理和监控API，提供系统整体的健康检查、
 * 服务启停控制、状态监控、账号信息查询等核心管理功能。
 * 
 * 功能范围：
 * - 系统健康检查（综合健康状态、服务可用性检查）
 * - 服务生命周期管理（启动、停止、重启服务）
 * - 系统状态监控（服务状态、连接状态、性能指标）
 * - 账号信息管理（账户列表、账户详情、资金状态）
 * - API文档生成（端点列表、接口说明）
 * 
 * 路径前缀：/system
 */
class SystemApiHandler : public BaseApiHandler {
public:
    /**
     * @brief 构造系统API处理器
     */
    SystemApiHandler();
    
    /**
     * @brief 析构函数
     */
    ~SystemApiHandler() override = default;

    /**
     * @brief 注册系统管理相关路由
     * 
     * @param service 网络服务实例指针，用于注册路由
     */
    void register_routes(NetworkService* service) override;

private:
    // ===== 系统监控API处理函数 =====

    /**
     * @brief 处理系统健康检查请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含系统健康状态
     */
    HttpResponse handle_health_check(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理启动所有服务请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含启动结果
     */
    HttpResponse handle_start_all_services(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理停止所有服务请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含停止结果
     */
    HttpResponse handle_stop_all_services(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理服务状态查询请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含详细的服务状态
     */
    HttpResponse handle_service_status(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理获取所有账户信息请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含账户列表（不含密码）
     */
    HttpResponse handle_get_accounts(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理获取指定账户详情请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数）
     * @return HTTP响应对象，包含账户详细信息（不含密码）
     */
    HttpResponse handle_get_account_detail(const HttpRequest& req, const UrlParseResult& url_result);

    /**
     * @brief 处理获取指定时间日志请求
     *
     * @param req HTTP请求对象
     * @param url_result 已解析的URL信息（包含查询参数，需要date和logger参数）
     *                   - date: 日期格式 YYYY-MM-DD
     *                   - logger: 日志名称（quote/scheduler/server/strategy/trade）
     * @return HTTP响应对象，包含日志内容字符串或无日志提示
     */
    HttpResponse handle_get_logs(const HttpRequest& req, const UrlParseResult& url_result);

    // ===== 服务控制私有方法 =====

    /**
     * @brief 启动行情服务
     * @return true 启动成功，false 启动失败
     */
    bool start_quote_service();

    /**
     * @brief 停止行情服务
     * @return true 停止成功，false 停止失败
     */
    bool stop_quote_service();

    /**
     * @brief 启动交易服务
     * @return true 启动成功，false 启动失败
     */
    bool start_trade_service();

    /**
     * @brief 停止交易服务
     * @return true 停止成功，false 停止失败
     */
    bool stop_trade_service();

    /**
     * @brief 启动策略服务
     * @return true 启动成功，false 启动失败
     */
    bool start_strategy_service();

    /**
     * @brief 停止策略服务
     * @return true 停止成功，false 停止失败
     */
    bool stop_strategy_service();
};

} // namespace network

NS_FT_END