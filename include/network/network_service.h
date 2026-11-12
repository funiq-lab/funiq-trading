#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <queue>
#include <nlohmann/json.hpp>
#include "logger/logger.hpp"
#include "core/macros.h"
#include "utils/url_parser.h"
#include "network/handlers/websocket/base_websocket_handler.h"

NS_FT_BEGIN

namespace network
{

    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    namespace asio = boost::asio;
    using tcp = boost::asio::ip::tcp;
    using json = nlohmann::json;

    using HttpResponse = http::response<http::string_body>;                  ///< HTTP响应类型定义
    using HttpRequest = http::request<http::string_body>;                    ///< HTTP请求类型定义
    using HttpHandler = std::function<HttpResponse(const HttpRequest &req, const UrlParseResult &url_result)>; ///< HTTP处理函数类型定义，包含URL解析结果

    using WebSocketMessageHandler = std::function<void(std::shared_ptr<IWebSocketConnection>, const std::string &)>; ///< WebSocket消息处理函数类型定义

    // 前向声明
    class BaseApiHandler;

    /**
     * @brief 网络服务配置结构
     *
     * 包含HTTP/WebSocket服务器的所有配置参数，支持从环境变量自动加载配置。
     * 提供合理的默认值，确保在没有配置的情况下也能正常运行。
     *
     * 配置项说明：
     * - address: 服务器绑定地址，支持IPv4和IPv6
     * - port: 服务器监听端口，范围1-65535
     * - threads: 工作线程数，建议设置为CPU核心数的1-2倍
     *
     */
    struct NetworkConfig
    {
        std::string address = "0.0.0.0"; ///< 服务器监听地址，默认绑定所有网卡
        unsigned short port = 8080;      ///< 服务器监听端口，默认8080
        int threads = 4;                 ///< 工作线程数量，默认4个线程
        bool enable_request_logging = false; ///< 是否启用请求日志记录，默认关闭
        std::vector<std::string> log_sensitive_fields; ///< 日志脱敏字段列表

        /**
         * @brief 从环境变量加载配置
         *
         * 读取预定义的环境变量并更新配置参数。环境变量值会覆盖默认值，
         * 但不会覆盖已手动设置的值。
         *
         * 支持的环境变量：
         * - HTTP_SERVER_ADDRESS: 服务器地址（如"127.0.0.1", "0.0.0.0"）
         * - HTTP_SERVER_PORT: 服务器端口（如"8080", "9090"）
         * - HTTP_SERVER_THREADS: 工作线程数（如"4", "8"）
         * - HTTP_REQUEST_LOGGING: 是否启用请求日志（"1", "true", "yes"）
         * - HTTP_LOG_SENSITIVE_FIELDS: 日志脱敏字段列表，逗号分隔（如"password,token,secret"）
         */
        void load_from_env();
    };

    /**
     * @brief 路由信息结构
     *
     * 记录已注册的HTTP路由信息，包含HTTP方法、路径、处理函数和描述。
     * 用于路由匹配、API文档生成和调试信息输出。
     *
     * 字段说明：
     * - method: HTTP方法，如GET、POST、PUT、DELETE等
     * - path: 完整的路由路径，包含Handler前缀
     * - handler: 实际的请求处理函数
     * - description: 路由功能描述，用于API文档
     *
     * 使用场景：
     * - 路由匹配和请求分发
     * - 自动生成API文档
     * - 调试和监控路由状态
     * - 路由冲突检测
     */
    struct Route
    {
        std::string method;      ///< HTTP方法（GET、POST、PUT、DELETE等）
        std::string path;        ///< 路由路径（如"/api/health"）
        HttpHandler handler;     ///< 处理函数
        std::string description; ///< 路由描述信息
    };

    /**
     * @brief 网络服务类 - 高性能HTTP/WebSocket服务器
     *
     * 基于 Boost.Beast 实现的异步HTTP/WebSocket服务器，专注于网络层功能，
     * 业务逻辑通过模块化的Handler系统进行管理。支持HTTP和WebSocket协议，
     * 提供完整的请求处理、连接管理和错误处理机制。
     *
     * 核心特性：
     * - 异步HTTP服务器（支持Keep-Alive、多线程处理、高并发）
     * - 完整WebSocket支持（升级检测、连接管理、消息路由）
     * - 模块化Handler系统（HTTP Handler + WebSocket Handler）
     * - 灵活的路由管理（支持GET、POST、PUT、DELETE等HTTP方法）
     * - 标准化响应格式（统一的JSON格式和错误码）
     * - 完善的错误处理和日志记录
     * - 环境变量配置支持
     * - 优雅启动和关闭
     *
     * 架构设计理念：
     * - 单一职责：NetworkService专注网络层，业务逻辑分离到Handler
     * - 模块化：通过Handler系统实现业务模块的解耦和可插拔
     * - 高性能：基于异步I/O和多线程设计，支持大量并发连接
     * - 易用性：提供类似Express.js/FastAPI的简洁路由注册接口
     * - 扩展性：支持Handler插件式扩展，便于功能迭代
     * - 协议支持：同时支持HTTP和WebSocket，满足不同场景需求
     *
     * 线程安全性：
     * - 路由注册接口线程安全
     * - Handler注册接口线程安全
     * - WebSocket Handler注册线程安全
     * - 服务状态查询接口线程安全
     * - Handler执行在多线程环境中，Handler内部需保证线程安全
     * - 连接管理和消息路由线程安全
     *
     * 性能特征：
     * - 支持数千个并发HTTP连接
     * - 支持数百个并发WebSocket连接
     * - 异步I/O避免线程阻塞
     * - 连接池复用减少资源开销
     * - 智能路由匹配，O(1)时间复杂度
     * - 内存使用优化，避免不必要的拷贝
     *
     * 协议支持详解：
     * - HTTP/1.1: 完整支持，包括Keep-Alive、chunked编码等
     * - WebSocket: RFC 6455标准，支持文本和二进制消息
     * - 协议升级: 自动检测WebSocket升级请求
     * - 错误处理: 符合HTTP状态码规范
     *
     * 使用示例：
     * @code
     * // 创建网络服务
     * auto service = std::make_unique<NetworkService>();
     *
     * // 注册HTTP Handler
     * auto api_handler = std::make_shared<MyApiHandler>();
     * service->register_handler(api_handler);
     *
     * // 注册WebSocket Handler
     * auto ws_handler = std::make_shared<MyWebSocketHandler>();
     * service->register_websocket_handler(ws_handler);
     *
     * // 设置服务依赖
     * service->set_service_instances(quote_parser, trade_handler, strategy_service);
     *
     * // 启动服务
     * if (service->start()) {
     *     LOGGER_INFO("Server started on {}:{}", service->get_address(), service->get_port());
     *     // 服务运行中...
     *
     *     // 优雅关闭
     *     service->stop();
     * }
     * @endcode
     *
     * 错误处理策略：
     * - 启动失败：记录详细错误信息，提供诊断建议
     * - 连接异常：自动清理，不影响其他连接
     * - 请求处理异常：返回500错误，记录异常信息
     * - 内存不足：拒绝新连接，保护服务稳定性
     * - 配置错误：提供明确的错误提示和修复建议
     */
    class NetworkService
    {
    public:
        /**
         * @brief 构造函数
         *
         * 初始化网络服务实例，设置默认配置参数。构造函数只进行基本的
         * 成员变量初始化，不执行复杂的初始化操作，避免构造异常。
         *
         * 初始化内容：
         * - 设置默认网络配置
         * - 初始化原子状态变量
         * - 创建空的Handler容器
         * - 初始化互斥锁
         *
         * @note 构造函数不会抛出异常
         * @note 构造后处于未初始化状态，需要调用start()才能提供服务
         * @note 构造函数执行速度很快，适合频繁创建销毁
         * @note 实际的网络资源在start()中分配
         */
        NetworkService();

        /**
         * @brief 析构函数
         *
         * 自动停止服务并清理所有资源。析构过程会等待所有工作线程结束，
         * 确保资源正确释放，避免内存泄漏和资源竞争。
         *
         * 清理流程：
         * 1. 调用stop()停止服务
         * 2. 关闭所有HTTP连接
         * 3. 关闭所有WebSocket连接
         * 4. 等待工作线程结束
         * 5. 释放网络资源
         * 6. 清理Handler注册
         *
         * @note 析构函数会自动调用stop()
         * @note 析构过程可能需要一定时间等待线程结束
         * @note 强制析构可能导致连接非优雅关闭
         * @note 析构函数不会抛出异常
         */
        ~NetworkService();

        // 禁用拷贝构造和赋值操作
        NetworkService(const NetworkService &) = delete;
        NetworkService &operator=(const NetworkService &) = delete;

        // ===== 服务生命周期管理接口 =====

        /**
         * @brief 启动网络服务
         *
         * 执行完整的服务启动流程，包括配置加载、资源初始化、监听器启动
         * 和工作线程创建。启动成功后服务即可处理HTTP和WebSocket请求。
         *
         * 启动流程详解：
         * 1. 从环境变量加载配置参数
         * 2. 验证配置参数的有效性（端口范围、地址格式等）
         * 3. 创建异步I/O上下文（io_context）
         * 4. 注册系统内置Handler（健康检查、API文档等）
         * 5. 创建并启动TCP监听器
         * 6. 启动指定数量的工作线程池
         * 7. 设置服务状态为运行中
         *
         * 失败场景处理：
         * - 端口被占用：返回false，错误信息包含端口号
         * - 地址格式错误：返回false，提供正确格式示例
         * - 权限不足：返回false，提示权限要求
         * - 内存不足：返回false，建议减少线程数
         *
         * @return true 启动成功，false 启动失败
         * @note 线程安全，可重复调用（already running时返回false）
         * @note 启动失败时，可通过get_last_error()获取详细错误信息
         * @note 必须在Handler注册完成后调用，否则缺少业务处理能力
         * @note 启动过程通常在几毫秒内完成
         * @note 启动成功后立即可以处理客户端请求
         */
        bool start();

        /**
         * @brief 停止网络服务
         *
         * 执行优雅的服务停止流程，确保正在处理的请求能够完成，
         * 新的连接请求会被拒绝。停止过程包括连接清理和资源释放。
         *
         * 停止流程详解：
         * 1. 设置服务状态为停止中
         * 2. 停止TCP监听器，拒绝新连接
         * 3. 向所有WebSocket客户端发送关闭消息
         * 4. 等待当前HTTP请求处理完成
         * 5. 停止异步I/O上下文
         * 6. 等待所有工作线程结束
         * 7. 清理网络资源和内存
         * 8. 设置服务状态为已停止
         *
         * 超时处理：
         * - 正常情况下等待所有连接优雅关闭
         * - 超时后强制关闭剩余连接
         * - 确保停止过程在合理时间内完成
         *
         * @return true 停止成功，false 停止失败
         * @note 线程安全，可重复调用
         * @note 停止过程可能需要一定时间（通常几秒内）
         * @note 停止后可以重新调用start()重新启动服务
         * @note 强制停止可能导致部分请求处理中断
         */
        bool stop();

        /**
         * @brief 检查服务是否正在运行
         *
         * @return true 服务运行中，false 服务已停止
         * @note 线程安全，原子操作
         * @note 运行状态表示服务已成功启动且正在处理请求
         * @note 状态可能在调用间隙发生变化
         * @note 用于健康检查和监控
         */
        bool is_running() const { return _running.load(); }

        /**
         * @brief 获取服务器监听地址
         *
         * @return 服务器监听的IP地址字符串
         * @note 线程安全
         */
        std::string get_address() const { return _config.address; }

        /**
         * @brief 获取服务器监听端口
         *
         * @return 服务器监听的端口号
         */
        unsigned short get_port() const { return _config.port; }

        /**
         * @brief 获取最后的错误信息
         *
         * @return 最后发生的错误描述字符串
         */
        const std::string &get_last_error() const { return _last_error; }

        // ===== HTTP路由注册接口 =====

        /**
         * @brief 注册GET路由
         *
         * 注册HTTP GET方法的路由处理函数。GET请求通常用于数据查询，
         * 应该是幂等和安全的操作。
         *
         * @param path 路由路径（如"/api/users", "/health"）
         * @param handler HTTP请求处理函数，接收HttpRequest返回HttpResponse
         * @param description 路由描述信息（可选，用于API文档生成）
         * @note 线程安全，可在服务运行时调用
         * @note 路径必须以'/'开头，如"/api/users"
         * @note 相同路径的路由会被覆盖，后注册的覆盖先注册的
         * @note Handler函数会在多线程环境中并发调用
         * @note 建议GET请求不修改服务器状态
         */
        void get(const std::string &path, HttpHandler handler, const std::string &description = "");

        /**
         * @brief 注册POST路由
         *
         * 注册HTTP POST方法的路由处理函数。POST请求通常用于创建资源，
         * 可以修改服务器状态，不要求幂等性。
         *
         * @param path 路由路径（如"/api/users", "/api/orders"）
         * @param handler HTTP请求处理函数，接收HttpRequest返回HttpResponse
         * @param description 路由描述信息（可选，用于API文档生成）
         * @note 线程安全，可在服务运行时调用
         * @note 路径必须以'/'开头
         * @note 相同路径的路由会被覆盖
         * @note Handler需要解析请求体中的数据
         * @note 适用于创建、提交、上传等操作
         */
        void post(const std::string &path, HttpHandler handler, const std::string &description = "");

        /**
         * @brief 注册PUT路由
         *
         * 注册HTTP PUT方法的路由处理函数。PUT请求通常用于更新或创建资源，
         * 应该是幂等的操作。
         *
         * @param path 路由路径（如"/api/users/123", "/api/config"）
         * @param handler HTTP请求处理函数，接收HttpRequest返回HttpResponse
         * @param description 路由描述信息（可选，用于API文档生成）
         * @note 线程安全，可在服务运行时调用
         * @note 路径必须以'/'开头
         * @note 相同路径的路由会被覆盖
         * @note PUT操作应该是幂等的
         * @note 适用于更新、替换整个资源
         */
        void put(const std::string &path, HttpHandler handler, const std::string &description = "");

        /**
         * @brief 注册DELETE路由
         *
         * 注册HTTP DELETE方法的路由处理函数。DELETE请求用于删除资源，
         * 应该是幂等的操作。
         *
         * @param path 路由路径（如"/api/users/123", "/api/sessions/abc"）
         * @param handler HTTP请求处理函数，接收HttpRequest返回HttpResponse
         * @param description 路由描述信息（可选，用于API文档生成）
         * @note 线程安全，可在服务运行时调用
         * @note 路径必须以'/'开头
         * @note 相同路径的路由会被覆盖
         * @note DELETE操作应该是幂等的
         * @note 删除不存在的资源通常返回404或204
         */
        void del(const std::string &path, HttpHandler handler, const std::string &description = "");

        /**
         * @brief 获取所有已注册的路由信息
         *
         * 返回当前已注册的所有HTTP路由信息的副本，用于API文档生成、
         * 调试和路由状态监控。
         *
         * @return 路由信息向量的副本
         * @note 线程安全，使用锁保护
         * @note 返回副本，对返回值的修改不会影响实际路由
         * @note 用于API文档生成和调试
         * @note 包含路由路径、HTTP方法、描述等信息
         * @note 路由顺序与注册顺序一致
         */
        std::vector<Route> get_routes() const;

        // ===== Handler注册接口 =====

        /**
         * @brief 注册HTTP API处理器
         *
         * 将HTTP Handler实例注册到网络服务中，Handler会自动注册其管理的
         * 所有路由。支持多个Handler共存，实现业务模块的分离和解耦。
         *
         * 注册流程：
         * 1. 将Handler加入管理列表
         * 2. 调用Handler的register_routes()方法
         * 3. Handler内部调用service的路由注册方法
         * 4. 建立路径到Handler的映射关系
         *
         * @param handler Handler实例的共享指针
         * @note 线程安全，建议在start()之前调用
         * @note Handler会立即调用register_routes()注册其管理的路由
         * @note 相同Handler可以重复注册（但路由会被覆盖）
         * @note Handler的生命周期由NetworkService管理
         * @note 支持动态注册，运行时添加新的业务模块
         */
        void register_handler(std::shared_ptr<BaseApiHandler> handler);

        /**
         * @brief 设置服务实例到所有已注册的Handler
         *
         * 将外部服务实例（quote_parser、trade_handler、strategy_service）
         * 传递给所有已注册的Handler，使Handler能够访问这些核心业务服务。
         * 实现依赖注入模式，解耦网络层和业务层。
         *
         * 依赖注入流程：
         * 1. 遍历所有已注册的HTTP Handler
         * 2. 调用每个Handler的set_service_instances()方法
         * 3. Handler内部保存服务实例指针
         * 4. Handler可以在请求处理中使用这些服务
         *
         * @param quote_parser 行情解析器实例指针，NULL表示不提供
         * @param trade_handler 交易处理器实例指针，NULL表示不提供
         * @param strategy_service 策略服务实例指针，NULL表示不提供
         * @param database_manager 数据库管理器实例指针，NULL表示不提供
         * @note 线程安全
         * @note 可以传递NULL指针（Handler需要处理NULL情况）
         * @note 建议在start()之前调用，确保Handler能正确初始化
         * @note 会对所有已注册和后续注册的Handler生效
         * @note 支持动态更新，运行时可以更换服务实例
         */
        void set_service_instances(
            void *quote_parser,
            void *trade_handler,
            void *strategy_service,
            void *database_manager);

        // ===== WebSocket支持接口 =====

        /**
         * @brief 注册WebSocket处理器
         *
         * 注册WebSocket Handler到网络服务，Handler负责处理特定路径的
         * WebSocket连接和消息。支持多个WebSocket Handler并存。
         *
         * 注册要求：
         * - Handler路径必须唯一，重复路径会导致注册失败
         * - 建议路径以'/ws/'开头，如'/ws/orders'、'/ws/chat'
         * - Handler必须实现BaseWebSocketHandler接口
         *
         * @param handler WebSocket处理器实例的共享指针
         * @note 线程安全
         * @note Handler的路径不能重复，重复注册会覆盖前一个
         * @note WebSocket Handler与HTTP Handler使用不同的路由空间
         * @note Handler的生命周期由NetworkService管理
         * @note 支持运行时动态注册新的WebSocket服务
         */
        void register_websocket_handler(std::shared_ptr<BaseWebSocketHandler> handler);

    private:
        /**
         * @brief 初始化服务
         *
         * 执行服务初始化的内部逻辑，包括配置加载、参数验证、
         * 异步I/O上下文创建等。在start()中被调用。
         *
         * @return true 初始化成功，false 初始化失败
         * @note 非线程安全，仅在start()中调用
         * @note 支持重复调用（已初始化时直接返回true）
         * @note 初始化失败的原因会保存到_last_error
         */
        bool _init();

        /**
         * @brief 注册默认Handler
         *
         * 注册系统内置的Handler，如健康检查、API文档等。
         * 在init()过程中被调用。
         *
         * @note 非线程安全，仅在init()中调用
         * @note 内置Handler包括：健康检查、路由列表、WebSocket升级等
         */
        void _register_default_handlers();

        /**
         * @brief 查找HTTP路由处理函数
         *
         * 根据HTTP方法和路径查找对应的处理函数。
         * 用于HTTP请求的路由分发。
         *
         * @param method HTTP方法字符串（如"GET"、"POST"）
         * @param path 请求路径字符串（如"/api/users"）
         * @return 对应的处理函数，未找到时返回nullptr
         * @note 线程安全
         * @note 使用精确匹配，不支持路径参数或通配符
         * @note 路径匹配区分大小写
         */
        HttpHandler _find_handler(const std::string &method, const std::string &path) const;


        /**
         * @brief 查找WebSocket处理器
         *
         * 根据WebSocket连接路径查找对应的处理器，用于WebSocket升级
         * 请求的路由分发。
         *
         * @param path WebSocket连接路径（如"/ws/orders"）
         * @return 对应的处理器实例，未找到时返回nullptr
         * @note 线程安全
         * @note 使用精确路径匹配，不支持通配符
         * @note 路径匹配区分大小写
         * @note 用于WebSocket连接建立时的Handler分发
         */
        std::shared_ptr<BaseWebSocketHandler> _find_websocket_handler(const std::string &path) const;

        /**
         * @brief 创建标准JSON响应
         *
         * 创建带有标准头部的JSON格式HTTP响应。自动设置Content-Type、
         * Server等标准头部字段，确保响应格式的一致性。
         *
         * 设置的HTTP头部：
         * - Content-Type: application/json
         * - Server: FT-Trading-Server
         * - Content-Length: 自动计算
         * - Connection: Keep-Alive（根据请求决定）
         *
         * @param status HTTP状态码（如200、404、500等）
         * @param body JSON响应体对象
         * @param req 原始HTTP请求（用于Keep-Alive等连接管理）
         * @return 构造好的HTTP响应对象
         * @note 线程安全
         * @note 自动处理Keep-Alive连接设置
         * @note JSON序列化失败时会返回500错误响应
         * @note 响应体自动UTF-8编码
         */
        HttpResponse _create_json_response(http::status status, const json &body, const HttpRequest &req);

        /**
         * @brief 创建404 Not Found响应
         *
         * 创建标准的404错误响应，当请求的路径没有对应的Handler时使用。
         * 响应包含请求路径信息，便于调试。
         *
         * @param req 原始HTTP请求
         * @return 404错误响应对象
         * @note 线程安全
         * @note 响应体包含请求路径信息，格式：{"error": "Not Found", "path": "/invalid/path"}
         * @note 自动设置Content-Type为application/json
         * @note 用于路由不匹配的情况
         */
        HttpResponse _handle_not_found(const HttpRequest &req);

        /**
         * @brief 创建500 Internal Server Error响应
         *
         * 创建标准的500错误响应，当Handler处理过程中发生异常时使用。
         * 响应包含错误描述信息。
         *
         * @param req 原始HTTP请求
         * @param error 错误描述信息，会包含在响应体中
         * @return 500错误响应对象
         * @note 线程安全
         * @note 错误信息会包含在响应体中，格式：{"error": "Internal Server Error", "message": "具体错误"}
         * @note 敏感信息会被过滤，避免信息泄露
         * @note 同时会记录详细错误到日志系统
         */
        HttpResponse _handle_server_error(const HttpRequest &req, const std::string &error);

        /**
         * @brief 检测WebSocket升级请求
         *
         * 检查HTTP请求是否为WebSocket升级请求，根据请求头判断。
         *
         * @param req HTTP请求对象
         * @return true 是WebSocket升级请求，false 普通HTTP请求
         * @note 线程安全
         * @note 检查Connection和Upgrade头部字段
         */
        bool _is_websocket_upgrade(const HttpRequest &req) const;

        /**
         * @brief 处理WebSocket升级
         *
         * 将HTTP连接升级为WebSocket连接，创建WebSocketSession并启动。
         *
         * @param socket TCP套接字
         * @param req 原始HTTP升级请求
         * @note 异步处理，不阻塞调用线程
         * @note 升级失败会关闭连接
         */
        void _handle_websocket_upgrade(tcp::socket &&socket, const HttpRequest &req);

        /**
         * @brief 处理健康检查请求
         */
        HttpResponse _handle_health(const HttpRequest& req);
        
        /**
         * @brief 处理API列表请求  
         */
        HttpResponse _handle_api_list(const HttpRequest& req);

        /**
         * @brief 记录HTTP请求日志
         * 
         * 当启用请求日志时，记录接收到的HTTP请求信息，包括方法、路径、参数和请求体。
         * 
         * @param req HTTP请求对象
         * @param method HTTP方法字符串
         * @param path 请求路径
         * @param url_result URL解析结果
         * @note 仅在enable_request_logging为true时输出日志
         * @note 敏感信息（如密码字段）会被自动脱敏
         */
        void _log_request(const HttpRequest& req, const std::string& method, 
                         const std::string& path, const UrlParseResult& url_result);

        // ===== 网络层实现类 =====

        /**
         * @brief HTTP会话类
         *
         * 处理单个HTTP连接的生命周期，包括请求接收、处理、响应发送，
         * 以及WebSocket升级检测。基于异步I/O实现，支持Keep-Alive连接。
         *
         * 生命周期：
         * 1. 接受连接后创建Session实例
         * 2. 异步读取HTTP请求
         * 3. 检测是否为WebSocket升级请求
         * 4. 调用对应的Handler处理请求
         * 5. 异步发送HTTP响应
         * 6. 根据Keep-Alive决定是否继续处理下一个请求
         *
         * @note 每个连接对应一个Session实例
         * @note Session实例由shared_ptr管理生命周期
         * @note 支持HTTP/1.1的Keep-Alive机制
         * @note 支持自动WebSocket升级
         */
        class Session : public std::enable_shared_from_this<Session>
        {
        public:
            /**
             * @brief 构造HTTP会话
             *
             * @param socket TCP套接字（右值引用，转移所有权）
             * @param service 网络服务实例指针
             */
            Session(tcp::socket &&socket, NetworkService *service);

            /**
             * @brief 启动会话处理
             *
             * 开始异步读取HTTP请求，启动会话的处理循环。
             *
             * @note 必须在创建后立即调用
             * @note 自动管理会话生命周期
             */
            void run();

        private:
            /**
             * @brief 异步读取HTTP请求
             *
             * 开始异步读取操作，读取完成后调用on_read()。
             */
            void do_read();

            /**
             * @brief HTTP请求读取完成回调
             *
             * @param ec 错误码
             * @param bytes_transferred 传输字节数
             */
            void on_read(beast::error_code ec, std::size_t bytes_transferred);

            /**
             * @brief HTTP响应发送完成回调
             *
             * @param res 响应对象的共享指针
             * @param close 是否关闭连接
             * @param ec 错误码
             * @param bytes_transferred 传输字节数
             */
            void on_write(
                std::shared_ptr<HttpResponse> res,
                bool close,
                beast::error_code ec,
                std::size_t bytes_transferred);

            /**
             * @brief 关闭连接
             *
             * 优雅关闭TCP连接。
             */
            void do_close();

            /**
             * @brief 处理HTTP请求
             *
             * 将HTTP请求分发给对应的Handler处理，或进行WebSocket升级。
             *
             * @param req HTTP请求对象（右值引用）
             * @return HTTP响应对象
             */
            HttpResponse handle_request(HttpRequest &&req);

            beast::tcp_stream _stream;  ///< TCP流对象
            beast::flat_buffer _buffer; ///< 接收缓冲区
            HttpRequest _req;           ///< HTTP请求对象
            NetworkService *_service;   ///< 网络服务指针
        };

        /**
         * @brief TCP监听器类
         *
         * 负责监听TCP端口，接受新的连接请求，为每个连接创建Session。
         * 基于异步I/O实现，能够高效处理大量并发连接。
         *
         * 工作流程：
         * 1. 绑定监听端口
         * 2. 异步等待连接
         * 3. 接受连接后创建Session
         * 4. 继续等待下一个连接
         *
         * @note 单个Listener实例处理所有连接
         * @note 连接处理分发到多个工作线程
         */
        class Listener : public std::enable_shared_from_this<Listener>
        {
        public:
            /**
             * @brief 构造TCP监听器
             *
             * @param ioc 异步I/O上下文引用
             * @param endpoint TCP监听端点（地址+端口）
             * @param service 网络服务实例指针
             * @throws std::runtime_error 监听器创建失败时抛出异常
             */
            Listener(asio::io_context &ioc, tcp::endpoint endpoint, NetworkService *service);

            /**
             * @brief 启动监听
             *
             * 开始异步接受连接请求。
             *
             * @note 必须在创建后调用
             * @note 监听器会持续工作直到I/O上下文停止
             */
            void run();

        private:
            /**
             * @brief 异步接受连接
             *
             * 开始异步接受操作，连接到达后调用on_accept()。
             */
            void do_accept();

            /**
             * @brief 连接接受完成回调
             *
             * @param ec 错误码
             * @param socket 新连接的套接字
             */
            void on_accept(beast::error_code ec, tcp::socket socket);

            asio::io_context &_ioc;   ///< 异步I/O上下文引用
            tcp::acceptor _acceptor;  ///< TCP接受器
            NetworkService *_service; ///< 网络服务指针
        };

        /**
         * @brief WebSocket会话类
         *
         * 处理WebSocket连接的生命周期，实现IWebSocketConnection接口。
         * 负责WebSocket协议处理、消息路由和连接管理。
         *
         * 主要功能：
         * - WebSocket握手协议处理
         * - 双向消息传输（文本和二进制）
         * - 连接状态管理
         * - 消息路由到对应Handler
         * - 连接错误处理和恢复
         *
         * 生命周期：
         * 1. HTTP连接升级为WebSocket
         * 2. 完成WebSocket握手
         * 3. 查找并绑定对应Handler
         * 4. 进入消息处理循环
         * 5. 处理连接关闭或错误
         */
        class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>,
                                 public IWebSocketConnection
        {
        public:
            /**
             * @brief 构造WebSocket会话
             *
             * @param ws WebSocket流对象（右值引用，转移所有权）
             * @param service 网络服务实例指针
             * @param path WebSocket连接路径
             */
            WebSocketSession(websocket::stream<beast::tcp_stream> &&ws,
                             NetworkService *service,
                             const std::string &path);

            /**
             * @brief 启动WebSocket会话
             *
             * 开始WebSocket消息处理循环。
             *
             * @note 必须在创建后立即调用
             * @note 自动管理会话生命周期
             */
            void run();

            // ===== IWebSocketConnection 接口实现 =====

            /**
             * @brief 获取连接唯一标识符
             *
             * @return 连接的唯一标识字符串
             * @note 线程安全
             */
            std::string get_connection_id() const override;

            /**
             * @brief 发送文本消息
             *
             * @param message 要发送的文本消息
             * @return true 发送成功，false 发送失败
             * @note 线程安全
             */
            bool send_text(const std::string &message) override;

            /**
             * @brief 发送JSON消息
             *
             * @param data 要发送的JSON数据
             * @return true 发送成功，false 发送失败
             * @note 线程安全
             */
            bool send_json(const json &data) override;

            /**
             * @brief 关闭WebSocket连接
             *
             * @param reason 关闭原因
             * @note 线程安全
             */
            void close(const std::string &reason = "") override;

            /**
             * @brief 检查连接状态
             *
             * @return true 连接有效，false 连接已断开
             * @note 线程安全
             */
            bool is_connected() const override;

        private:
            /**
             * @brief 异步读取WebSocket消息
             */
            void do_read();

            /**
             * @brief 关闭WebSocket连接
             */
            void do_close();

            /**
             * @brief 异步写入实现
             */
            void do_write();

            /**
             * @brief 消息读取完成回调
             *
             * @param ec 错误码
             * @param bytes_transferred 传输字节数
             */
            void on_read(beast::error_code ec, std::size_t bytes_transferred);

            /**
             * @brief 消息写入完成回调
             *
             * @param ec 错误码
             * @param bytes_transferred 传输字节数
             */
            void on_write(beast::error_code ec, std::size_t bytes_transferred);

            websocket::stream<beast::tcp_stream> _ws;       ///< WebSocket流对象
            beast::flat_buffer _buffer;                     ///< 接收缓冲区
            NetworkService *_service;                       ///< 网络服务指针
            std::string _path;                              ///< WebSocket路径
            std::string _connection_id;                     ///< 连接ID
            std::shared_ptr<BaseWebSocketHandler> _handler; ///< WebSocket处理器
            std::atomic<bool> _connected{true};             ///< 连接状态
            std::mutex _write_mutex;                        ///< 写入互斥锁
            std::queue<std::string> _write_msgs;            ///< 写入消息队列
        };

        // ===== 成员变量 =====

        NetworkConfig _config;                 ///< 网络服务配置
        std::atomic<bool> _running{false};     ///< 服务运行状态
        std::atomic<bool> _initialized{false}; ///< 服务初始化状态
        std::string _last_error;               ///< 最后的错误信息

        // 路由管理
        std::vector<Route> _routes;       ///< 已注册的路由列表
        mutable std::mutex _routes_mutex; ///< 路由列表保护锁

        // Handler管理
        std::vector<std::shared_ptr<BaseApiHandler>> _handlers; ///< 已注册的Handler列表
        mutable std::mutex _handlers_mutex;                     ///< Handler列表保护锁

        // WebSocket 管理
        std::unordered_map<std::string, std::shared_ptr<BaseWebSocketHandler>> _websocket_handlers_map; ///< WebSocket处理器映射
        std::vector<std::shared_ptr<BaseWebSocketHandler>> _websocket_handlers;                         ///< WebSocket处理器列表

        // boost::asio 相关
        std::unique_ptr<asio::io_context> _ioc; ///< 异步I/O上下文
        std::vector<std::thread> _threads;      ///< 工作线程池
        std::shared_ptr<Listener> _listener;    ///< TCP监听器

        // 友元类声明，允许访问私有成员
        friend class Session;
        friend class Listener;
        friend class BaseApiHandler;
    };

} // namespace network

NS_FT_END