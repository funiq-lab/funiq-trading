#include "network/network_service.h"
#include "network/handlers/http/base_api_handler.h"
#include "network/error/error.h"
#include "utils/url_parser.h"
#include "logger/logger.hpp"
#include "laserpants/dotenv/dotenv.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <sstream>

NS_FT_BEGIN

namespace network {
void NetworkConfig::load_from_env() {
    // 使用dotenv获取环境变量，提供默认值
    address = dotenv::getenv("HTTP_SERVER_ADDRESS", address);

    std::string port_str = dotenv::getenv("HTTP_SERVER_PORT", "");
    if (!port_str.empty()) {
        port = static_cast<unsigned short>(std::atoi(port_str.c_str()));
    }

    std::string threads_str = dotenv::getenv("HTTP_SERVER_THREADS", "");
    if (!threads_str.empty()) {
        threads = std::atoi(threads_str.c_str());
    }

    std::string logging_str = dotenv::getenv("HTTP_REQUEST_LOGGING", "");
    if (!logging_str.empty()) {
        // 转换为小写
        std::transform(logging_str.begin(), logging_str.end(), logging_str.begin(), ::tolower);
        enable_request_logging = (logging_str == "1" || logging_str == "true" || logging_str == "yes");
    }

    // 加载敏感字段配置
    std::string sensitive_fields_str = dotenv::getenv("HTTP_LOG_SENSITIVE_FIELDS",
        "password,passwd,pwd,bank_password,fund_password,token,secret,key");

    if (!sensitive_fields_str.empty()) {
        log_sensitive_fields.clear();
        std::stringstream ss(sensitive_fields_str);
        std::string field;
        while (std::getline(ss, field, ',')) {
            // 去除前后空格
            field.erase(0, field.find_first_not_of(" \t\n\r"));
            field.erase(field.find_last_not_of(" \t\n\r") + 1);
            if (!field.empty()) {
                log_sensitive_fields.push_back(field);
            }
        }
    }
}

NetworkService::NetworkService() : _config(), _running(false), _initialized(false) {
}

NetworkService::~NetworkService() {
    stop();
}

bool NetworkService::start() {
    if (!_init()) {
        return false;
    }

    if (_running.load()) {
        _last_error = "Service already running";
        return false;
    }

    try {
        // 创建监听器
        auto const address = asio::ip::make_address(_config.address);
        auto const port = static_cast<unsigned short>(_config.port);
        
        _listener = std::make_shared<Listener>(*_ioc, tcp::endpoint{address, port}, this);
        _listener->run();

        // 启动工作线程
        _threads.clear();
        _threads.reserve(_config.threads);
        
        for (int i = 0; i < _config.threads; ++i) {
            _threads.emplace_back([this] {
                _ioc->run();
            });
        }

        _running.store(true);
        NETWORK_LOG_INFO("Network service started on {}:{} with {} threads", 
                        _config.address, _config.port, _config.threads);
        if (_config.enable_request_logging) {
            NETWORK_LOG_INFO("Request logging is enabled");
        }
        return true;
        
    } catch (const std::exception& e) {
        _last_error = "Failed to start service: " + std::string(e.what());
        NETWORK_LOG_ERROR(_last_error);
        return false;
    }
}

bool NetworkService::stop() {
    if (!_running.load()) {
        return true;
    }

    try {
        NETWORK_LOG_INFO("Stopping network service...");
        
        _ioc->stop();
        
        for (auto& t : _threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        _threads.clear();
        
        _listener.reset();
        _running.store(false);
        
        NETWORK_LOG_INFO("Network service stopped");
        return true;
        
    } catch (const std::exception& e) {
        _last_error = "Error stopping service: " + std::string(e.what());
        NETWORK_LOG_ERROR(_last_error);
        return false;
    }
}

bool NetworkService::_init() {
    if (_initialized.load()) {
        return true;
    }

    try {
        dotenv::init();
        // 从环境变量加载配置
        _config.load_from_env();
        
        // 验证配置
        if (_config.port == 0) {
            _last_error = "Invalid port number";
            return false;
        }
        
        if (_config.threads <= 0) {
            _config.threads = 4;
        }

        // 创建 io_context
        _ioc = std::make_unique<asio::io_context>(_config.threads);
        
        // 注册默认路由
        _register_default_handlers();
        
        _initialized.store(true);
        NETWORK_LOG_INFO("Network service initialized on {}:{}", _config.address, _config.port);
        return true;
        
    } catch (const std::exception& e) {
        _last_error = "Failed to initialize service: " + std::string(e.what());
        NETWORK_LOG_ERROR(_last_error);
        return false;
    }
}

void NetworkService::_register_default_handlers() {
    // 注册健康检查端点
    get("/health", [this](const HttpRequest& req, const UrlParseResult& url_result) -> HttpResponse {
        return _handle_health(req);
    }, "Health check endpoint");

    // 注册API列表端点
    get("/api", [this](const HttpRequest& req, const UrlParseResult& url_result) -> HttpResponse {
        return _handle_api_list(req);
    }, "List all API endpoints");
}

HttpResponse NetworkService::_handle_health(const HttpRequest& req) {
    json response_body = {
        {"code", ErrorCode::SUCCESS},
        {"message", ErrorCode::get_message(ErrorCode::SUCCESS)},
        {"data", {
            {"status", "healthy"},
            {"service", "FT-Trading-Server"},
            {"running", _running.load()},
            {"address", _config.address},
            {"port", _config.port}
        }}
    };
    return _create_json_response(http::status::ok, response_body, req);
}

HttpResponse NetworkService::_handle_api_list(const HttpRequest& req) {
    try {
        std::lock_guard<std::mutex> lock(_routes_mutex);
        
        json endpoints = json::object();
        int total_count = 0;
        
        for (const auto& route : _routes) {
            std::string key = route.method + " " + route.path;
            endpoints[key] = route.description.empty() ? "No description" : route.description;
            total_count++;
        }
        
        json data = {
            {"endpoints", endpoints},
            {"total_count", total_count}
        };
        
        json response_body = {
            {"code", ErrorCode::SUCCESS},
            {"message", ErrorCode::get_message(ErrorCode::SUCCESS)},
            {"data", data}
        };
        
        return _create_json_response(http::status::ok, response_body, req);
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Get API list failed: {}", e.what());
        
        json error_body = {
            {"code", ErrorCode::INTERNAL_SERVER_ERROR},
            {"message", ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR)},
            {"data", nullptr}
        };
        
        return _create_json_response(http::status::internal_server_error, error_body, req);
    }
}

void NetworkService::get(const std::string& path, HttpHandler handler, const std::string& description) {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    _routes.push_back({"GET", path, std::move(handler), description});
}

void NetworkService::post(const std::string& path, HttpHandler handler, const std::string& description) {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    _routes.push_back({"POST", path, std::move(handler), description});
}

void NetworkService::put(const std::string& path, HttpHandler handler, const std::string& description) {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    _routes.push_back({"PUT", path, std::move(handler), description});
}

void NetworkService::del(const std::string& path, HttpHandler handler, const std::string& description) {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    _routes.push_back({"DELETE", path, std::move(handler), description});
}

std::vector<Route> NetworkService::get_routes() const {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    return _routes;
}

HttpHandler NetworkService::_find_handler(const std::string& method, const std::string& path) const {
    std::lock_guard<std::mutex> lock(_routes_mutex);
    for (const auto& route : _routes) {
        if (route.method == method && route.path == path) {
            return route.handler;
        }
    }
    return nullptr;
}

void NetworkService::register_handler(std::shared_ptr<BaseApiHandler> handler) {
    std::lock_guard<std::mutex> lock(_handlers_mutex);
    _handlers.push_back(handler);
    
    // 立即注册Handler的路由
    handler->register_routes(this);
    handler->set_network_service(this);
    
    NETWORK_LOG_INFO("Handler registered with prefix: {}", handler->get_path_prefix());
}

void NetworkService::set_service_instances(
    void* quote_parser,
    void* trade_handler, 
    void* strategy_service,
    void* database_manager
) {
    std::lock_guard<std::mutex> lock(_handlers_mutex);
    
    // 传递给所有已注册的Handler
    for (auto& handler : _handlers) {
        handler->set_service_instances(quote_parser, trade_handler, strategy_service, database_manager);
    }
}

void NetworkService::register_websocket_handler(std::shared_ptr<BaseWebSocketHandler> handler) {
    std::lock_guard<std::mutex> lock(_handlers_mutex);
    
    std::string path = handler->get_path();
    _websocket_handlers_map[path] = handler;
    _websocket_handlers.push_back(handler);
    
    NETWORK_LOG_INFO("WebSocket handler registered for path: {}", path);
}

std::shared_ptr<BaseWebSocketHandler> NetworkService::_find_websocket_handler(const std::string& path) const {
    std::lock_guard<std::mutex> lock(_handlers_mutex);
    
    auto it = _websocket_handlers_map.find(path);
    if (it != _websocket_handlers_map.end()) {
        return it->second;
    }
    return nullptr;
}

HttpResponse NetworkService::_create_json_response(http::status status, const json& body, const HttpRequest& req) {
    HttpResponse res{status, req.version()};
    res.set(http::field::server, "FT-Trading-Server");
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.keep_alive(req.keep_alive());
    
    try {
        res.body() = body.dump();
    } catch (const std::exception& e) {
        // JSON序列化失败，返回错误响应
        NETWORK_LOG_ERROR("JSON serialization failed: {}", e.what());
        json error_body = {
            {"code", ErrorCode::INTERNAL_SERVER_ERROR},
            {"message", ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR)},
            {"data", nullptr}
        };
        res.result(http::status::internal_server_error);
        res.body() = error_body.dump();
    }
    
    res.prepare_payload();
    return res;
}

HttpResponse NetworkService::_handle_not_found(const HttpRequest& req) {
    json body = {
        {"code", ErrorCode::RESOURCE_NOT_FOUND},
        {"message", ErrorCode::get_message(ErrorCode::RESOURCE_NOT_FOUND)},
        {"data", {
            {"path", std::string(req.target())},
            {"method", std::string(req.method_string())}
        }}
    };
    return _create_json_response(http::status::not_found, body, req);
}

HttpResponse NetworkService::_handle_server_error(const HttpRequest& req, const std::string& error) {
    // 记录详细错误到日志
    NETWORK_LOG_ERROR("Server error processing request {}: {}", 
                     std::string(req.target()), error);
    
    json body = {
        {"code", ErrorCode::INTERNAL_SERVER_ERROR},
        {"message", ErrorCode::get_message(ErrorCode::INTERNAL_SERVER_ERROR)},
        {"data", {
            {"details", error},
            {"path", std::string(req.target())},
            {"method", std::string(req.method_string())}
        }}
    };
    return _create_json_response(http::status::internal_server_error, body, req);
}

void NetworkService::_log_request(const HttpRequest& req, const std::string& method,
                                   const std::string& path, const UrlParseResult& url_result) {
    // 构建完整的日志消息
    std::stringstream log_msg;
    log_msg << "API Request: " << method << " " << path;

    // 添加查询参数（如果有）
    if (!url_result.query_params.empty()) {
        std::string query_str;
        for (const auto& param : url_result.query_params) {
            if (!query_str.empty()) query_str += ", ";
            query_str += param.first + "=" + param.second;
        }
        log_msg << " | Query: {" << query_str << "}";
    }

    // 处理请求体（如果是POST/PUT/PATCH/DELETE等方法）
    if (!req.body().empty() && (method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE")) {
        try {
            // 尝试解析为JSON
            auto body_json = json::parse(req.body());

            // 脱敏处理：隐藏敏感字段
            if (body_json.is_object()) {
                for (const auto& field : _config.log_sensitive_fields) {
                    if (body_json.contains(field)) {
                        body_json[field] = "******";
                    }
                }
            }

            log_msg << " | Body: " << body_json.dump();

        } catch (const json::parse_error&) {
            // 如果不是JSON格式，输出body长度
            log_msg << " | Body: [" << req.body().size() << " bytes]";
        }
    }

    // 一次性输出完整日志
    NETWORK_LOG_INFO("{}", log_msg.str());
}

// ===== 网络层实现 =====

// Session 实现

#define SESSION_EXPIRY_SECONDS 30
NetworkService::Session::Session(tcp::socket&& socket, NetworkService* service)
    : _stream(std::move(socket)), _service(service) {
}

void NetworkService::Session::run() {
    _stream.expires_after(std::chrono::seconds(SESSION_EXPIRY_SECONDS));
    do_read();
}

void NetworkService::Session::do_read() {
    _req = {};
    _stream.expires_after(std::chrono::seconds(SESSION_EXPIRY_SECONDS));
    
    http::async_read(_stream, _buffer, _req,
        beast::bind_front_handler(&Session::on_read, shared_from_this()));
}

void NetworkService::Session::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == http::error::end_of_stream) {
        return do_close();
    }

    if (ec) {
        NETWORK_LOG_WARN("HTTP read error: {}", ec.message());
        return;
    }

    auto response = handle_request(std::move(_req));
    bool keep_alive = response.keep_alive();

    auto sp = std::make_shared<HttpResponse>(std::move(response));
    
    http::async_write(_stream, *sp,
        beast::bind_front_handler(&Session::on_write, shared_from_this(), sp, keep_alive));
}

void NetworkService::Session::on_write(
    std::shared_ptr<HttpResponse> res,
    bool keep_alive, 
    beast::error_code ec, 
    std::size_t bytes_transferred
) {
    boost::ignore_unused(bytes_transferred);
    boost::ignore_unused(res);

    if (ec) {
        NETWORK_LOG_WARN("HTTP write error: {}", ec.message());
        return;
    }

    if (!keep_alive) {
        return do_close();
    }

    do_read();
}

void NetworkService::Session::do_close() {
    beast::error_code ec;
    _stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

HttpResponse NetworkService::Session::handle_request(HttpRequest&& req) {
    try {
        std::string method = std::string(req.method_string());
        std::string full_url = std::string(req.target());

        // 使用URL解析器分离路径和查询参数
        auto url_result = UrlParser::parse(full_url);
        std::string path = url_result.path;  // 只使用路径部分进行路由匹配

        // 记录请求日志（如果启用）
        if (_service->_config.enable_request_logging) {
            _service->_log_request(req, method, path, url_result);
        }

        // 检查是否为WebSocket升级请求
        if (_service->_is_websocket_upgrade(req)) {
            
            // 查找WebSocket处理器
            auto ws_handler = _service->_find_websocket_handler(path);
            if (ws_handler) {
                tcp::socket socket = _stream.release_socket();
                _service->_handle_websocket_upgrade(std::move(socket), req);
                
                // 返回空响应（连接已转移，不会发送此响应）
                HttpResponse res{http::status::switching_protocols, req.version()};
                return res;
            } else {
                // 没有找到对应的WebSocket处理器
                json body = {
                    {"code", ErrorCode::RESOURCE_NOT_FOUND},
                    {"message", ErrorCode::get_message(ErrorCode::RESOURCE_NOT_FOUND)},
                    {"data", {
                        {"path", path},
                        {"type", "websocket"}
                    }}
                };
                return _service->_create_json_response(http::status::not_found, body, req);
            }
        }

        // 普通HTTP请求处理 - 使用路径部分进行匹配
        auto handler = _service->_find_handler(method, path);
        if (handler) {
            return handler(req, url_result);
        }

        return _service->_handle_not_found(req);
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Error handling request: {}", e.what());
        return _service->_handle_server_error(req, e.what());
    }
}

// 检测WebSocket升级请求
bool NetworkService::_is_websocket_upgrade(const HttpRequest& req) const {
    // 检查必需的WebSocket升级头部
    auto connection = req.find(http::field::connection);
    auto upgrade = req.find(http::field::upgrade);
    auto ws_key = req.find(http::field::sec_websocket_key);
    auto ws_version = req.find(http::field::sec_websocket_version);
    
    if (connection == req.end() || upgrade == req.end() || 
        ws_key == req.end() || ws_version == req.end()) {
        return false;
    }
    
    // 检查头部值
    std::string conn_val = std::string(connection->value());
    std::string upgrade_val = std::string(upgrade->value());
    std::string version_val = std::string(ws_version->value());
    
    // 转换为小写进行比较
    std::transform(conn_val.begin(), conn_val.end(), conn_val.begin(), ::tolower);
    std::transform(upgrade_val.begin(), upgrade_val.end(), upgrade_val.begin(), ::tolower);
    
    return (conn_val.find("upgrade") != std::string::npos) &&
           (upgrade_val == "websocket") &&
           (version_val == "13");
}

void NetworkService::_handle_websocket_upgrade(tcp::socket&& socket, const HttpRequest& req) {
    try {
        std::string path = std::string(req.target());
        
        // 直接创建WebSocket流，不使用unique_ptr
        auto ws = std::make_shared<websocket::stream<beast::tcp_stream>>(std::move(socket));
        
        // 接受WebSocket升级
        ws->async_accept(req, [this, ws, path](beast::error_code ec) {
            if (ec) {
                NETWORK_LOG_ERROR("WebSocket accept failed: {}", ec.message());
                return;
            }
            
            // 创建WebSocket会话
            auto session = std::make_shared<WebSocketSession>(std::move(*ws), this, path);
            session->run();
        });
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("WebSocket upgrade error: {}", e.what());
    }
}

// Listener 实现
NetworkService::Listener::Listener(asio::io_context& ioc, tcp::endpoint endpoint, NetworkService* service)
    : _ioc(ioc), _acceptor(asio::make_strand(ioc)), _service(service) {
    beast::error_code ec;

    _acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open acceptor: " + ec.message());
    }

    _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("Failed to set reuse_address: " + ec.message());
    }

    _acceptor.set_option(boost::asio::socket_base::reuse_address(true));
    _acceptor.bind(endpoint, ec);
    if (ec) {
        throw std::runtime_error("Failed to bind: " + ec.message());
    }

    _acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        throw std::runtime_error("Failed to listen: " + ec.message());
    }
}

void NetworkService::Listener::run() {
    do_accept();
}

void NetworkService::Listener::do_accept() {
    _acceptor.async_accept(
        asio::make_strand(_ioc),
        beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
}

void NetworkService::Listener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        NETWORK_LOG_WARN("Accept error: {}", ec.message());
    } else {
        std::make_shared<Session>(std::move(socket), _service)->run();
    }

    do_accept();
}

NetworkService::WebSocketSession::WebSocketSession(
    websocket::stream<beast::tcp_stream>&& ws,
    NetworkService* service,
    const std::string& path)
    : _ws(std::move(ws)), _service(service), _path(path) {
    
    // 生成唯一连接ID
    static std::atomic<uint64_t> next_id{1};
    _connection_id = "ws_" + std::to_string(next_id.fetch_add(1));
    
    // 查找对应的WebSocket处理器
    _handler = _service->_find_websocket_handler(_path);
}

void NetworkService::WebSocketSession::run() {
    if (_handler) {
        _handler->handle_event(WebSocketEvent::CONNECT, shared_from_this());
    }
    do_read();
}

std::string NetworkService::WebSocketSession::get_connection_id() const {
    return _connection_id;
}

bool NetworkService::WebSocketSession::send_text(const std::string& message) {
    if (!_connected.load()) {
        return false;
    }
    
    auto self = shared_from_this();
    
    // 使用strand确保线程安全
    asio::post(_ws.get_executor(), [self, message]() {
        bool write_in_progress = !self->_write_msgs.empty();
        self->_write_msgs.push(message);
        
        if (!write_in_progress) {
            self->do_write();
        }
    });
    
    return true;
}

bool NetworkService::WebSocketSession::send_json(const json& data) {
    try {
        return send_text(data.dump());
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("WebSocket send JSON failed: {}", e.what());
        return false;
    }
}

void NetworkService::WebSocketSession::close(const std::string& reason) {
    if (_connected.exchange(false)) {
        NETWORK_LOG_INFO("Closing WebSocket connection {}: {}", _connection_id, reason);
        
        if (_handler) {
            _handler->handle_event(WebSocketEvent::DISCONNECT, shared_from_this(), reason);
        }
        
        do_close();
    }
}

bool NetworkService::WebSocketSession::is_connected() const {
    return _connected.load();
}

void NetworkService::WebSocketSession::do_read() {
    _ws.async_read(_buffer, beast::bind_front_handler(
        &WebSocketSession::on_read, shared_from_this()));
}

void NetworkService::WebSocketSession::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        _connected.store(false);
        NETWORK_LOG_WARN("WebSocket read error: {}", ec.message());
        if (_handler) {
            _handler->handle_event(WebSocketEvent::DISCONNECT, shared_from_this(), ec.message());
        }
        return;
    }
    
    try {
        std::string message = beast::buffers_to_string(_buffer.data());
        _buffer.clear();
        
        if (_handler) {
            _handler->handle_event(WebSocketEvent::MESSAGE, shared_from_this(), message);
        }
        
        // 继续读取下一条消息
        do_read();
        
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("WebSocket message processing error: {}", e.what());
        if (_handler) {
            _handler->handle_event(WebSocketEvent::ERROR, shared_from_this(), e.what());
        }
    }
}

void NetworkService::WebSocketSession::do_write() {
    if (_write_msgs.empty()) {
        return;
    }
    
    _ws.text(true);
    _ws.async_write(asio::buffer(_write_msgs.front()),
        beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
}

void NetworkService::WebSocketSession::on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        _connected.store(false);
        NETWORK_LOG_WARN("WebSocket write error: {}", ec.message());
        if (_handler) {
            _handler->handle_event(WebSocketEvent::ERROR, shared_from_this(), ec.message());
        }
        return;
    }
    
    _write_msgs.pop();
    
    if (!_write_msgs.empty()) {
        do_write();
    }
}

void NetworkService::WebSocketSession::do_close() {
    try {
        _ws.async_close(websocket::close_code::normal,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    NETWORK_LOG_WARN("WebSocket close error: {}", ec.message());
                }
            });
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("WebSocket close error: {}", e.what());
    }
}

} // namespace network

NS_FT_END