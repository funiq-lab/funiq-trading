#ifndef QUOTE_SERVICE_H
#define QUOTE_SERVICE_H

#include "quote/quote_interface.h"
#include "quote/quote_parser.h"
#include "core/macros.h"
#include <string>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <nlohmann/json.hpp>

NS_FT_BEGIN

// 前向声明
class QuoteParserAdapter;

/**
 * 行情服务管理类
 * 
 * 作为行情系统的顶层服务管理器，提供统一的行情服务接口。
 * 主要负责配置管理、解析器生命周期管理和服务状态管理。
 * 
 * **核心功能**：
 * 1. **配置管理**：读取和解析JSON配置文件，自动选择活跃的解析器
 * 2. **解析器管理**：创建、配置和管理QuoteParserAdapter实例
 * 3. **生命周期管理**：统一管理服务的初始化、启动、停止和清理
 * 4. **状态监控**：提供服务运行状态和错误信息查询
 * 5. **C接口桥接**：为Python等语言提供C接口封装
 * 
 * **设计特点**：
 * - 单例模式：通过全局实例提供统一的服务入口
 * - 配置驱动：通过JSON配置文件灵活切换不同的行情数据源
 * - 线程安全：支持多线程环境下的并发访问
 * - 错误处理：完善的错误信息管理和状态反馈
 * - 资源管理：RAII模式确保资源正确释放
 * 
 * **使用流程**：
 * 1. 调用init()读取配置并初始化解析器
 * 2. 调用start()启动行情服务
 * 3. 通过get_parser()获取解析器实例进行数据操作
 * 4. 调用stop()停止服务
 * 5. 调用release()清理资源
 * 
 * **环境变量支持**：
 * - QUOTE_CONFIG_FILE: 指定配置文件路径
 * - 未设置时使用默认路径"./quote_config.json"
 */
class QuoteService {
public:
    /**
     * 构造函数
     * 
     * 初始化服务状态和成员变量。
     * 自动加载环境变量配置。
     */
    QuoteService();
    
    /**
     * 析构函数
     * 
     * 确保资源正确释放，自动调用release()方法。
     * 使用noexcept确保析构过程中的异常安全。
     */
    ~QuoteService() noexcept;
    
    // ===== 服务生命周期管理接口 =====
    
    /**
     * 初始化行情服务
     * 
     * 执行以下初始化步骤：
     * 1. 从环境变量或默认路径读取配置文件
     * 2. 解析JSON配置，查找标记为active的解析器配置
     * 3. 创建对应的QuoteParserAdapter实例
     * 4. 初始化解析器适配器
     * 
     * @return 0成功，负数失败
     * 
     * @post 成功时服务进入已初始化状态
     * @note 线程安全，可重复调用（已初始化时直接返回成功）
     * @note 失败时可通过get_last_error()获取详细错误信息
     * 
     * **环境变量**：
     * - QUOTE_CONFIG_FILE: 指定配置文件路径
     * - 未设置时使用默认路径"./quote_config.json"
     */
    int init();
    
    /**
     * 启动行情服务
     * 
     * 启动底层的行情解析器，开始接收和处理行情数据。
     * 会自动建立到数据源的连接。
     * 
     * @return 0成功，负数失败
     * 
     * @pre 必须先成功调用init()方法
     * @post 成功时服务进入运行状态，开始接收行情数据
     * @note 线程安全，重复调用是安全的
     * @note 失败时可通过get_last_error()获取详细错误信息
     */
    int start();
    
    /**
     * 停止行情服务
     * 
     * 停止数据接收和处理，断开数据源连接。
     * 保持初始化状态，可以重新启动。
     * 
     * @return 0成功，负数失败
     * 
     * @post 服务停止接收新的行情数据
     * @note 线程安全，重复调用是安全的
     * @note 停止后可以重新调用start()恢复服务
     */
    int stop();
    
    /**
     * 释放行情服务资源
     * 
     * 执行完整的资源清理：
     * 1. 停止所有数据处理
     * 2. 断开所有连接
     * 3. 销毁解析器实例
     * 4. 清理配置信息
     * 5. 重置所有状态标志
     * 
     * @post 服务回到未初始化状态
     * @note 线程安全，幂等操作，可以安全地多次调用
     * @note 析构函数会自动调用此方法
     */
    void release();
    
    // ===== 核心访问接口 =====
    
    /**
     * 获取当前运行的行情解析器实例
     * 
     * 这是最重要的接口，用于获取底层的行情解析器进行数据操作。
     * 返回的解析器支持所有标准的行情操作。
     * 
     * @return 行情解析器接口指针，未初始化时返回nullptr
     * 
     * @note 线程安全，返回的指针在服务生命周期内有效
     * @note 调用者不应直接delete返回的指针
     * @note 可以安全地在多个线程中并发使用返回的解析器
     * 
     * **使用示例**：
     * ```cpp
     * IQuoteParser* parser = service.get_parser();
     * if (parser) {
     *     DepthData data;
     *     if (parser->get_depth_data(&data) > 0) {
     *         // 处理行情数据
     *     }
     * }
     * ```
     */
    IQuoteParser* get_parser() const;
    
    // ===== 状态查询接口 =====
    
    /**
     * 检查服务是否正在运行
     * @return true服务运行中，false服务已停止或未启动
     * @note 线程安全，使用原子操作
     */
    bool is_running() const;
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串，无错误时返回空字符串
     * @note 线程安全，返回最近一次操作的错误信息
     * @note 返回的指针在下次错误发生前有效
     */
    const char* get_last_error() const;
    
    /**
     * 获取当前解析器标识符
     * @return 解析器ID字符串，未初始化时返回空字符串
     * @note 线程安全，返回配置文件中指定的解析器ID
     * @note 用于日志记录和调试
     */
    const std::string& get_parser_id() const;
    
private:
    // ===== 状态和配置私有成员 =====
    std::atomic<bool> _running;         ///< 运行状态标志（原子操作）
    std::atomic<bool> _initialized;     ///< 初始化状态标志（原子操作）
    std::string _config_file;           ///< 配置文件路径
    std::string _last_error;            ///< 最后错误信息
    mutable std::shared_mutex _mutex;   ///< 读写锁，保护共享数据
    
    // ===== 解析器管理私有成员 =====
    std::string _parser_id;             ///< 当前解析器标识符
    std::unique_ptr<QuoteParserAdapter> _parser_adapter; ///< 解析器适配器实例
    
    // ===== 配置管理私有方法 =====
    
    /**
     * 加载配置文件
     * @param config_file 配置文件路径
     * @param config 输出的JSON配置对象
     * @return true加载成功，false加载失败
     * @note 支持JSON格式的配置文件
     * @note 会验证配置文件的基本格式
     */
    bool _load_config(const char* config_file, nlohmann::json& config);
    
    /**
     * 查找活跃的解析器配置
     * @param config JSON配置对象
     * @return 活跃解析器配置的指针，未找到时返回nullptr
     * @note 查找parsers数组中active字段为true的配置项
     * @note 如果有多个active配置，返回第一个
     */
    nlohmann::json* _find_active_parser_config(const nlohmann::json& config);
};

NS_FT_END

#endif // QUOTE_SERVICE_H