#pragma once

#include <string>
#include <memory>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>
#include "core/macros.h"

NS_FT_BEGIN

namespace network {

namespace http = boost::beast::http;
using json = nlohmann::json;
using HttpResponse = http::response<http::string_body>;
using HttpRequest = http::request<http::string_body>;

// 前向声明
class NetworkService;

/**
 * @brief API处理器基类
 * 
 * 所有业务API处理器的基类，提供统一的接口规范和通用功能实现。
 * 支持路径前缀分组，实现业务逻辑的模块化管理，避免所有API
 * 耦合到NetworkService中。
 * 
 * 设计理念：
 * - 单一职责：每个Handler专注于特定业务领域的API
 * - 路径分组：通过路径前缀实现API的分类管理
 * - 代码复用：基类提供通用的响应创建、JSON解析等功能
 * - 依赖注入：支持外部服务实例的注入和访问
 * - 标准化：统一的错误处理和响应格式
 * 
 * 线程安全性：
 * - 基类方法线程安全
 * - 子类实现的处理方法需要保证线程安全
 * - 服务实例访问需要考虑并发安全
 * 
 * 使用示例：
 * @code
 * class MyApiHandler : public BaseApiHandler {
 * public:
 *     MyApiHandler() : BaseApiHandler("/my") {}
 *     
 *     void register_routes(NetworkService* service) override {
 *         service->get(get_full_path("/data"), 
 *             [this](const HttpRequest& req) { return handle_get_data(req); },
 *             "Get my data");
 *     }
 * private:
 *     HttpResponse handle_get_data(const HttpRequest& req) {
 *         json data = {"message", "Hello World"};
 *         return create_success_response(data, req);
 *     }
 * };
 * @endcode
 */
class BaseApiHandler {
public:
    /**
     * @brief 构造API處理器
     * 
     * @param path_prefix 路径前缀（如"/api/v1"），用于API分组
     * @note 路径前缀会自动标准化（确保以'/'开头，不以'/'结尾）
     * @note 空字符串表示无前缀（根路径）
     * @note 前缀不应包含通配符或正则表达式
     */
    BaseApiHandler(const std::string& path_prefix = "");
    
    /**
     * @brief 虚析构函数
     * 
     * 确保派生类能够正确析构。
     * 
     * @note 析构时不会自动注销路由（由NetworkService管理）
     */
    virtual ~BaseApiHandler() = default;

    /**
     * @brief 注册路由到网络服务
     * 
     * 子类必须实现此方法，在其中调用NetworkService的路由注册方法
     * 注册所有需要处理的API端点。
     * 
     * @param service 网络服务实例指针
     * @note 纯虚函数，子类必须实现
     * @note 通常在NetworkService::register_handler()时被调用
     * @note 建议使用get_full_path()生成完整路径
     * @note 注册顺序不影响路由匹配
     */
    virtual void register_routes(NetworkService* service) = 0;

    /**
     * @brief 获取路径前缀
     * 
     * @return 当前Handler的路径前缀字符串
     * @note 线程安全
     * @note 返回的是标准化后的前缀（以'/'开头，不以'/'结尾）
     */
    const std::string& get_path_prefix() const { return _path_prefix; }

    /**
     * @brief 设置服务实例
     * 
     * 将外部服务实例传递给Handler，使Handler能够访问交易系统的
     * 各个核心服务。子类可以重写此方法进行特定的初始化操作。
     * 
     * @param quote_parser 行情解析器实例指针，可为NULL
     * @param trade_handler 交易处理器实例指针，可为NULL
     * @param strategy_service 策略服务实例指针，可为NULL
     * @param database_manager 数据库管理器实例指针，可为NULL
     * @note 虚函数，子类可以重写
     * @note 线程安全
     * @note 可以多次调用以更新服务实例
     * @note Handler应该能够处理NULL指针的情况
     */
    virtual void set_service_instances(
        void* quote_parser,
        void* trade_handler, 
        void* strategy_service,
        void* database_manager
    );

    /**
     * @brief 设置网络服务实例
     * 
     * @param network_service 网络服务实例指针
     */
    virtual void set_network_service(NetworkService* network_service);

protected:
    // ===== 通用响应创建方法 =====

    /**
     * @brief 创建标准JSON响应
     * 
     * 创建带有统一头部信息的JSON格式HTTP响应。
     * 自动设置Content-Type、Server等标准头部。
     * 
     * @param status HTTP状态码
     * @param body JSON响应体对象
     * @param req 原始HTTP请求对象
     * @return 构造好的HTTP响应对象
     * @note 线程安全
     * @note 自动处理Keep-Alive连接设置
     * @note JSON序列化异常会被捕获但不会抛出
     */
    HttpResponse create_json_response(
        http::status status, 
        const json& body, 
        const HttpRequest& req
    ) const;

    /**
     * @brief 创建错误响应
     * 
     * 创建标准格式的错误响应，使用业务错误码。
     * 
     * @param status HTTP状态码
     * @param error_code 业务错误码（如"E001", "E002"等）
     * @param message 错误信息
     * @param req 原始HTTP请求对象
     * @param data 可选的错误详细数据，默认为null
     * @return 错误响应对象
     * @note 线程安全
     * @note 响应格式：{"code": "E001", "message": "...", "data": null}
     */
    HttpResponse create_error_response(
        http::status status,
        const std::string& error_code,
        const std::string& message,
        const HttpRequest& req,
        const json& data = nullptr
    ) const;
    
    /**
     * @brief 创建成功响应
     * 
     * 创建成功响应，使用默认的成功消息。
     * 
     * @param data 响应数据
     * @param req 原始HTTP请求对象
     * @return 成功响应对象
     * @note 线程安全
     * @note 响应格式：{"code": "0", "message": "Success", "data": {...}}
     */
    HttpResponse create_success_response(
        const json& data,
        const HttpRequest& req
    ) const;

    // ===== 请求处理辅助方法 =====

    /**
     * @brief 解析请求体中的JSON数据
     * 
     * 安全地解析HTTP请求体中的JSON数据，提供详细的错误信息。
     * 
     * @param req HTTP请求对象
     * @param out_json 输出解析后的JSON对象
     * @param error 输出错误信息字符串
     * @return true 解析成功，false 解析失败
     * @note 线程安全
     * @note 解析失败时，out_json内容未定义，error包含详细错误信息
     * @note 空请求体会被视为解析失败
     * @note 支持标准的JSON格式
     */
    bool parse_json_body(const HttpRequest& req, json& out_json, std::string& error) const;

    /**
     * @brief 获取完整路径
     * 
     * 将相对路径与路径前缀组合，生成完整的API路径。
     * 
     * @param path 相对路径（如"/users"）
     * @return 完整路径（如"/api/v1/users"）
     * @note 线程安全
     * @note 自动处理路径分隔符，确保路径格式正确
     * @note 相对路径可以以'/'开头或不以'/'开头
     * @note 建议在register_routes()中使用此方法
     */
    std::string get_full_path(const std::string& path) const;

    // ===== 服务实例访问接口 =====

    /**
     * @brief 获取网络服务实例
     * 
     * @return NetworkService指针，如果未设置则返回nullptr
     * @note 通常在register_routes()调用后才可用
     * @note 子类可以使用此方法访问网络服务的功能
     */
    NetworkService* get_network_service() const;

    void* _quote_parser{nullptr};      ///< 行情解析器实例指针（可为NULL）
    void* _trade_handler{nullptr};     ///< 交易处理器实例指针（可为NULL）
    void* _strategy_service{nullptr};  ///< 策略服务实例指针（可为NULL）
    void* _db_manager{nullptr};  ///< 数据库管理器实例指针（可为NULL）

private:
    std::string _path_prefix;          ///< 路径前缀（标准化格式）
    NetworkService* _network_service{nullptr}; ///< 网络服务实例指针

    friend class NetworkService; // 允许NetworkService访问私有成员
};

} // namespace network

NS_FT_END