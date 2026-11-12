#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_set>
#include <mutex>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include "core/macros.h"

NS_FT_BEGIN

namespace network {

namespace websocket = boost::beast::websocket;
using json = nlohmann::json;

// 前向声明
class NetworkService;

/**
 * @brief WebSocket连接接口
 * 
 * 抽象WebSocket连接的基本操作，用于向客户端发送消息。
 * 每个WebSocket连接都有唯一的连接ID。
 */
class IWebSocketConnection {
public:
    virtual ~IWebSocketConnection() = default;
    
    /**
     * @brief 获取连接ID
     * @return 连接的唯一标识符
     */
    virtual std::string get_connection_id() const = 0;
    
    /**
     * @brief 发送文本消息
     * @param message 要发送的消息内容
     * @return true 发送成功，false 发送失败
     */
    virtual bool send_text(const std::string& message) = 0;
    
    /**
     * @brief 发送JSON消息
     * @param data JSON数据对象
     * @return true 发送成功，false 发送失败
     */
    virtual bool send_json(const json& data) = 0;
    
    /**
     * @brief 关闭连接
     * @param reason 关闭原因（可选）
     */
    virtual void close(const std::string& reason = "") = 0;
    
    /**
     * @brief 检查连接是否有效
     * @return true 连接有效，false 连接已断开
     */
    virtual bool is_connected() const = 0;
};

/**
 * @brief WebSocket事件类型
 */
enum class WebSocketEvent {
    CONNECT,    ///< 客户端连接
    DISCONNECT, ///< 客户端断开
    MESSAGE,    ///< 收到消息
    ERROR       ///< 发生错误
};

/**
 * @brief WebSocket处理器基类
 * 
 * 所有WebSocket业务处理器的基类，提供连接管理、消息广播等通用功能。
 * 支持路径匹配和事件处理，实现业务逻辑的模块化管理。
 * 
 * 设计理念：
 * - 事件驱动：基于连接、断开、消息等事件处理
 * - 连接管理：自动管理多个客户端连接
 * - 消息广播：支持向所有或特定客户端广播消息
 * - 路径匹配：支持WebSocket路径的精确匹配
 * - 线程安全：所有操作都是线程安全的
 * 
 * 使用示例：
 * @code
 * class OrderWebSocketHandler : public BaseWebSocketHandler {
 * public:
 *     OrderWebSocketHandler() : BaseWebSocketHandler("/ws/orders") {}
 *     
 * protected:
 *     void on_connect(std::shared_ptr<IWebSocketConnection> connection) override {
 *         // 客户端连接时的处理逻辑
 *         send_initial_data(connection);
 *     }
 *     
 *     void on_message(std::shared_ptr<IWebSocketConnection> connection, 
 *                     const std::string& message) override {
 *         // 处理客户端发送的消息
 *         handle_subscription(connection, message);
 *     }
 * };
 * @endcode
 */
class BaseWebSocketHandler {
public:
    /**
     * @brief 构造WebSocket处理器
     * 
     * @param path WebSocket路径（如"/ws/orders"）
     * @note 路径用于WebSocket连接的路由匹配
     * @note 路径应该以'/ws/'开头，遵循WebSocket约定
     */
    BaseWebSocketHandler(const std::string& path);
    
    /**
     * @brief 虚析构函数
     * 
     * 自动关闭所有活跃的WebSocket连接。
     */
    virtual ~BaseWebSocketHandler();

    /**
     * @brief 获取WebSocket路径
     * 
     * @return 当前Handler处理的WebSocket路径
     */
    const std::string& get_path() const { return _path; }

    /**
     * @brief 处理WebSocket事件
     * 
     * 网络层调用此方法分发WebSocket事件到具体的处理函数。
     * 
     * @param event 事件类型
     * @param connection WebSocket连接对象
     * @param data 事件相关数据（如消息内容、错误信息）
     */
    void handle_event(WebSocketEvent event, 
                     std::shared_ptr<IWebSocketConnection> connection,
                     const std::string& data = "");

    /**
     * @brief 向所有连接广播消息
     * 
     * @param message 要广播的消息内容
     * @return 成功发送的连接数量
     * @note 线程安全
     * @note 会自动清理无效的连接
     */
    size_t broadcast_text(const std::string& message);

    /**
     * @brief 向所有连接广播JSON消息
     * 
     * @param data 要广播的JSON数据
     * @return 成功发送的连接数量
     * @note 线程安全
     * @note 会自动清理无效的连接
     */
    size_t broadcast_json(const json& data);

    /**
     * @brief 获取当前连接数量
     * 
     * @return 活跃的WebSocket连接数量
     * @note 线程安全
     */
    size_t get_connection_count() const;

    /**
     * @brief 关闭所有连接
     * 
     * @param reason 关闭原因（可选）
     * @note 线程安全
     * @note 通常在Handler销毁时调用
     */
    void close_all_connections(const std::string& reason = "Server shutdown");

protected:
    /**
     * @brief 客户端连接事件处理
     * 
     * 当新的WebSocket客户端连接到此Handler时被调用。
     * 子类可以重写此方法实现自定义的连接处理逻辑。
     * 
     * @param connection 新建立的WebSocket连接
     * @note 默认实现只记录连接信息
     * @note 线程安全，可能在多个线程中并发调用
     */
    virtual void on_connect(std::shared_ptr<IWebSocketConnection> connection);

    /**
     * @brief 客户端断开事件处理
     * 
     * 当WebSocket客户端断开连接时被调用。
     * 子类可以重写此方法实现自定义的断开处理逻辑。
     * 
     * @param connection 断开的WebSocket连接
     * @param reason 断开原因
     * @note 默认实现只记录断开信息
     * @note 线程安全，可能在多个线程中并发调用
     */
    virtual void on_disconnect(std::shared_ptr<IWebSocketConnection> connection, 
                              const std::string& reason);

    /**
     * @brief 收到消息事件处理
     * 
     * 当从WebSocket客户端收到消息时被调用。
     * 子类必须重写此方法实现具体的消息处理逻辑。
     * 
     * @param connection 发送消息的WebSocket连接
     * @param message 收到的消息内容
     * @note 纯虚函数，子类必须实现
     * @note 线程安全，可能在多个线程中并发调用
     * @note 消息可能是文本或JSON格式，需要自行解析
     */
    virtual void on_message(std::shared_ptr<IWebSocketConnection> connection,
                           const std::string& message) = 0;

    /**
     * @brief 错误事件处理
     * 
     * 当WebSocket连接发生错误时被调用。
     * 子类可以重写此方法实现自定义的错误处理逻辑。
     * 
     * @param connection 发生错误的WebSocket连接
     * @param error 错误描述信息
     * @note 默认实现只记录错误信息
     * @note 线程安全，可能在多个线程中并发调用
     */
    virtual void on_error(std::shared_ptr<IWebSocketConnection> connection,
                         const std::string& error);

private:
    std::string _path;                                              ///< WebSocket路径
    std::unordered_set<std::shared_ptr<IWebSocketConnection>> _connections; ///< 活跃连接集合
    mutable std::mutex _connections_mutex;                          ///< 连接集合保护锁

    /**
     * @brief 添加连接到管理集合
     */
    void add_connection(std::shared_ptr<IWebSocketConnection> connection);

    /**
     * @brief 从管理集合移除连接
     */
    void remove_connection(std::shared_ptr<IWebSocketConnection> connection);

    /**
     * @brief 清理无效连接
     */
    void cleanup_connections();
};

} // namespace network

NS_FT_END