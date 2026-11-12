#ifndef TRADE_SERVICE_H
#define TRADE_SERVICE_H

#include "trade/trade_interface.h"
#include "trade/trade_handler.h"
#include "core/macros.h"
#include <string>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <nlohmann/json.hpp>

NS_FT_BEGIN

// 前向声明
class TradeHandlerAdapter;

/**
 * 交易服务管理类
 * 
 * 作为交易系统的顶层服务管理器，提供统一的交易服务接口。
 * 主要负责配置管理、交易处理器生命周期管理和服务状态管理。
 * 
 * **核心功能**：
 * 1. **配置管理**：读取和解析JSON配置文件，自动选择活跃的交易处理器
 * 2. **处理器管理**：创建、配置和管理TradeHandlerAdapter实例
 * 3. **生命周期管理**：统一管理服务的初始化、启动、停止和清理
 * 4. **状态监控**：提供服务运行状态和错误信息查询
 * 5. **C接口桥接**：为Python等语言提供C接口封装
 * 6. **多账户支持**：通过配置文件支持多个交易账户的统一管理
 * 
 * **设计特点**：
 * - 单例模式：通过全局实例提供统一的服务入口
 * - 配置驱动：通过JSON配置文件灵活切换不同的交易接口
 * - 线程安全：支持多线程环境下的并发访问
 * - 错误处理：完善的错误信息管理和状态反馈
 * - 资源管理：RAII模式确保资源正确释放
 * - 适配器模式：通过适配器统一不同交易接口的差异
 * 
 * **使用流程**：
 * 1. 调用init()读取配置并初始化交易处理器
 * 2. 调用start()启动交易服务
 * 3. 通过get_handler()获取处理器实例进行交易操作
 * 4. 调用stop()停止服务
 * 5. 调用release()清理资源
 * 
 * **环境变量支持**：
 * - TRADE_CONFIG_FILE: 指定配置文件路径
 * - 未设置时使用默认路径"./trade_config.json"
 */
class TradeService {
public:
    /**
     * 构造函数
     * 
     * 初始化服务状态和成员变量。
     * 自动加载环境变量配置。
     */
    TradeService();
    
    /**
     * 析构函数
     * 
     * 确保资源正确释放，自动调用release()方法。
     * 使用noexcept确保析构过程中的异常安全。
     */
    ~TradeService() noexcept;

    // ===== 服务生命周期管理接口 =====
    
    /**
     * 初始化交易服务
     * 
     * 执行以下初始化步骤：
     * 1. 从环境变量或默认路径读取配置文件
     * 2. 解析JSON配置，查找标记为active的交易处理器配置
     * 3. 验证配置文件的完整性和有效性
     * 4. 创建对应的TradeHandlerAdapter实例
     * 5. 初始化交易处理器适配器
     * 6. 加载多账户配置信息
     * 
     * @return 0成功，负数失败
     * 
     * @post 成功时服务进入已初始化状态
     * @note 线程安全，可重复调用（已初始化时直接返回成功）
     * @note 失败时可通过get_last_error()获取详细错误信息
     * 
     * **环境变量**：
     * - TRADE_CONFIG_FILE: 指定配置文件路径
     * - 未设置时使用默认路径"./trade_config.json"
     * 
     * **配置要求**：
     * - 必须包含"parsers"数组
     * - 至少有一个parser标记为"active": true
     * - 活跃的parser必须包含有效的"module"路径
     * - 连接配置必须完整且有效
     */
    int init();
    
    /**
     * 启动交易服务
     * 
     * 启动底层的交易处理器，开始接收和处理交易数据。
     * 会自动建立到交易系统的连接并执行登录认证。
     * 
     * @return 0成功，负数失败
     * 
     * @pre 必须先成功调用init()方法
     * @post 成功时服务进入运行状态，可以进行交易操作
     * @note 线程安全，重复调用是安全的
     * @note 失败时可通过get_last_error()获取详细错误信息
     * 
     * **启动流程**：
     * 1. 启动交易处理器内部线程
     * 2. 建立到交易服务器的网络连接
     * 3. 执行用户认证和登录流程
     * 4. 查询初始账户状态（资金、持仓等）
     * 5. 开始接收实时交易数据
     */
    int start();
    
    /**
     * 停止交易服务
     * 
     * 停止数据接收和处理，断开交易系统连接。
     * 保持初始化状态，可以重新启动。
     * 
     * @return 0成功，负数失败
     * 
     * @post 服务停止接收新的交易数据和处理交易请求
     * @note 线程安全，重复调用是安全的
     * @note 停止后可以重新调用start()恢复服务
     * 
     * **停止流程**：
     * 1. 停止接收新的交易请求
     * 2. 等待正在处理的交易完成
     * 3. 断开交易服务器连接
     * 4. 停止内部处理线程
     * 5. 保留配置和状态信息
     */
    int stop();
    
    /**
     * 释放交易服务资源
     * 
     * 执行完整的资源清理：
     * 1. 停止所有数据处理和交易操作
     * 2. 断开所有连接
     * 3. 停止所有处理线程
     * 4. 销毁交易处理器实例
     * 5. 清理配置信息和缓存数据
     * 6. 重置所有状态标志
     * 
     * @post 服务回到未初始化状态
     * @note 线程安全，幂等操作，可以安全地多次调用
     * @note 析构函数会自动调用此方法
     * @note 释放后需要重新调用init()才能使用服务
     */
    void release();

    // ===== 核心访问接口 =====
    
    /**
     * 获取当前运行的交易处理器实例
     * 
     * 这是最重要的接口，用于获取底层的交易处理器进行交易操作。
     * 返回的处理器支持所有标准的交易功能，包括多账户管理。
     * 
     * @return 交易处理器接口指针，未初始化时返回nullptr
     * 
     * @note 线程安全，返回的指针在服务生命周期内有效
     * @note 调用者不应直接delete返回的指针
     * @note 可以安全地在多个线程中并发使用返回的处理器
     * 
     * **使用示例**：
     * ```cpp
     * ITradeHandler* handler = service.get_handler();
     * if (handler && handler->is_login()) {
     *     // 获取账户信息
     *     AccountInfo account;
     *     if (handler->get_account_info_by_id("123456", &account) == 0) {
     *         // 下单操作
     *         TradeOrderData order = {...};
     *         handler->send_order(&order, 12345);
     *     }
     * }
     * ```
     * 
     * **多账户支持**：
     * 返回的处理器实例已经加载了配置文件中的所有账户信息，
     * 可以直接使用多账户相关的接口进行操作。
     */
    ITradeHandler* get_handler() const;

    // ===== 状态查询接口 =====
    
    /**
     * 检查服务是否正在运行
     * @return true服务运行中，false服务已停止或未启动
     * @note 线程安全，使用原子操作
     * @note 运行状态表示服务已启动且交易处理器正常工作
     */
    bool is_running() const;
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串，无错误时返回空字符串
     * @note 线程安全，返回最近一次操作的错误信息
     * @note 返回的指针在下次错误发生前有效
     * @note 用于诊断初始化、启动或运行时的问题
     */
    const char* get_last_error() const;
    
    /**
     * 获取当前交易接口标识符
     * @return 接口ID字符串，未初始化时返回空字符串
     * @note 线程安全，返回配置文件中指定的活跃处理器ID
     * @note 用于日志记录、监控和调试识别
     * @note 对应配置文件中active为true的parser的id字段
     */
    const std::string& get_handler_id() const;

private:
    // ===== 状态和配置私有成员 =====
    std::atomic<bool> _running;         ///< 运行状态标志（原子操作）
    std::atomic<bool> _initialized;     ///< 初始化状态标志（原子操作）
    std::string _config_file;           ///< 配置文件路径
    std::string _last_error;            ///< 最后错误信息
    mutable std::shared_mutex _mutex;   ///< 读写锁，保护共享数据

    // ===== 交易处理器管理私有成员 =====
    std::string _handler_id;          ///< 当前交易接口标识符
    std::unique_ptr<TradeHandlerAdapter> _handler_adapter; ///< 交易处理器适配器实例

    // ===== 配置管理私有方法 =====
    
    /**
     * 加载配置文件
     * @param config_file 配置文件路径
     * @param config 输出的JSON配置对象
     * @return true加载成功，false加载失败
     * 
     * @note 支持JSON格式的配置文件
     * @note 会验证配置文件的基本格式和必要字段
     * @note 失败时会设置详细的错误信息
     */
    bool _load_config(const char* config_file, nlohmann::json& config);
    
    /**
     * 查找活跃的交易处理器配置
     * @param config JSON配置对象
     * @return 活跃处理器配置的指针，未找到时返回nullptr
     * 
     * @note 查找parsers数组中active字段为true的配置项
     * @note 如果有多个active配置，返回第一个并记录警告
     * @note 如果没有找到active配置，返回nullptr并设置错误信息
     */
    nlohmann::json* _find_active_handler_config(const nlohmann::json& config);
};

NS_FT_END

#endif // TRADE_SERVICE_H