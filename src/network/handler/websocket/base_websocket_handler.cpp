#include "network/handlers/websocket/base_websocket_handler.h"
#include "logger/logger.hpp"
#include <algorithm>

NS_FT_BEGIN

namespace network {

// ===== BaseWebSocketHandler 实现 =====

BaseWebSocketHandler::BaseWebSocketHandler(const std::string& path) 
    : _path(path) {
    if (!_path.empty() && _path[0] != '/') {
        _path = "/" + _path;
    }
    
    NETWORK_LOG_INFO("BaseWebSocketHandler created for path: {}", _path);
}

BaseWebSocketHandler::~BaseWebSocketHandler() {
    close_all_connections("Handler shutdown");
    NETWORK_LOG_INFO("BaseWebSocketHandler for path {} destroyed", _path);
}

void BaseWebSocketHandler::handle_event(WebSocketEvent event, 
                                       std::shared_ptr<IWebSocketConnection> connection,
                                       const std::string& data) {
    if (!connection) {
        NETWORK_LOG_ERROR("Null connection in handle_event for path: {}", _path);
        return;
    }

    try {
        switch (event) {
            case WebSocketEvent::CONNECT:
                add_connection(connection);
                on_connect(connection);
                break;
                
            case WebSocketEvent::DISCONNECT:
                on_disconnect(connection, data);
                remove_connection(connection);
                break;
                
            case WebSocketEvent::MESSAGE:
                on_message(connection, data);
                break;
                
            case WebSocketEvent::ERROR:
                on_error(connection, data);
                // 错误时也要移除连接
                remove_connection(connection);
                break;
                
            default:
                NETWORK_LOG_WARN("Unknown WebSocket event type for path: {}", _path);
                break;
        }
    } catch (const std::exception& e) {
        NETWORK_LOG_ERROR("Exception in handle_event for path {}: {}", _path, e.what());
        on_error(connection, "Internal error: " + std::string(e.what()));
    }
}

size_t BaseWebSocketHandler::broadcast_text(const std::string& message) {
    std::lock_guard<std::mutex> lock(_connections_mutex);
    
    size_t success_count = 0;
    
    // 遍历所有连接并发送消息
    for (auto it = _connections.begin(); it != _connections.end();) {
        auto connection = *it;
        
        if (!connection || !connection->is_connected()) {
            // 连接已失效，从集合中移除
            it = _connections.erase(it);
            continue;
        }
        
        if (connection->send_text(message)) {
            success_count++;
        } else {
            NETWORK_LOG_WARN("Failed to send text to connection {} on path {}", 
                            connection->get_connection_id(), _path);
        }
        
        ++it;
    }
    
    return success_count;
}

size_t BaseWebSocketHandler::broadcast_json(const json& data) {
    std::lock_guard<std::mutex> lock(_connections_mutex);
    
    size_t success_count = 0;
    
    // 遍历所有连接并发送JSON消息
    for (auto it = _connections.begin(); it != _connections.end();) {
        auto connection = *it;
        
        if (!connection || !connection->is_connected()) {
            // 连接已失效，从集合中移除
            it = _connections.erase(it);
            continue;
        }
        
        if (connection->send_json(data)) {
            success_count++;
        } else {
            NETWORK_LOG_WARN("Failed to send JSON to connection {} on path {}", 
                            connection->get_connection_id(), _path);
        }
        
        ++it;
    }
    
    return success_count;
}

size_t BaseWebSocketHandler::get_connection_count() const {
    std::lock_guard<std::mutex> lock(_connections_mutex);
    return _connections.size();
}

void BaseWebSocketHandler::close_all_connections(const std::string& reason) {
    std::lock_guard<std::mutex> lock(_connections_mutex);
    
    NETWORK_LOG_INFO("Closing {} connections on path {}: {}", 
                     _connections.size(), _path, reason);
    
    for (auto& connection : _connections) {
        if (connection && connection->is_connected()) {
            try {
                connection->close(reason);
            } catch (const std::exception& e) {
                NETWORK_LOG_ERROR("Exception closing connection {}: {}", 
                                 connection->get_connection_id(), e.what());
            }
        }
    }
    
    _connections.clear();
}

// ===== 受保护的虚函数默认实现 =====

void BaseWebSocketHandler::on_connect(std::shared_ptr<IWebSocketConnection> connection) {
    NETWORK_LOG_INFO("WebSocket client {} connected to path {}", 
                     connection->get_connection_id(), _path);
}

void BaseWebSocketHandler::on_disconnect(std::shared_ptr<IWebSocketConnection> connection, 
                                        const std::string& reason) {
    NETWORK_LOG_INFO("WebSocket client {} disconnected from path {}: {}", 
                     connection->get_connection_id(), _path, reason);
}

void BaseWebSocketHandler::on_error(std::shared_ptr<IWebSocketConnection> connection,
                                   const std::string& error) {
    NETWORK_LOG_ERROR("WebSocket error for client {} on path {}: {}", 
                      connection->get_connection_id(), _path, error);
}

// ===== 私有方法实现 =====

void BaseWebSocketHandler::add_connection(std::shared_ptr<IWebSocketConnection> connection) {
    if (!connection) {
        NETWORK_LOG_ERROR("Attempted to add null connection to path: {}", _path);
        return;
    }
    
    std::lock_guard<std::mutex> lock(_connections_mutex);
    _connections.insert(connection);

}

void BaseWebSocketHandler::remove_connection(std::shared_ptr<IWebSocketConnection> connection) {
    if (!connection) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(_connections_mutex);
    auto removed_count = _connections.erase(connection);
}

void BaseWebSocketHandler::cleanup_connections() {
    std::lock_guard<std::mutex> lock(_connections_mutex);
    
    size_t initial_count = _connections.size();
    
    // 移除所有无效的连接
    for (auto it = _connections.begin(); it != _connections.end();) {
        auto connection = *it;
        
        if (!connection || !connection->is_connected()) {
            it = _connections.erase(it);
        } else {
            ++it;
        }
    }
    
    size_t cleaned_count = initial_count - _connections.size();
    if (cleaned_count > 0) {
        NETWORK_LOG_INFO("Cleaned up {} invalid connections from path {}, remaining: {}", 
                         cleaned_count, _path, _connections.size());
    }
}

} // namespace network

NS_FT_END