#ifndef TRADE_HANDLER_ADAPTER_H
#define TRADE_HANDLER_ADAPTER_H

#include "types/trade_types.h"
#include "trade/trade_handler.h"
#include "core/macros.h"
#include <string>
#include <memory>
#include <atomic>
#include <boost/core/noncopyable.hpp>
#include <nlohmann/json.hpp>
#include "utils/dll_helper.hpp"

NS_FT_BEGIN

/**
 * 交易处理器适配器类
 * 
 * 作为交易处理器的适配层，提供以下核心功能：
 * 1. **动态库管理**：负责加载和卸载交易处理器动态库
 * 2. **实例管理**：创建、配置和销毁交易处理器实例
 * 3. **配置解析**：将JSON配置转换为处理器所需的C结构体
 * 4. **生命周期管理**：统一管理处理器的初始化、启动、停止和清理
 * 5. **多账户管理**：解析和管理多个交易账户的配置信息
 * 6. **性能配置**：解析性能统计和监控相关的配置参数
 * 7. **数据管理**：配置数据落地和输出相关的参数
 * 
 * **设计模式**：
 * - 适配器模式：将JSON配置适配为C结构体配置
 * - 工厂模式：通过动态库工厂函数创建处理器实例
 * - RAII模式：自动管理资源生命周期
 * - 外观模式：为复杂的交易系统提供简化接口
 * 
 * **线程安全性**：
 * - 基本线程安全，使用原子变量保护状态
 * - 不支持并发初始化和释放操作
 * - 处理器实例的线程安全性由具体实现决定
 * 
 * **使用流程**：
 * 1. 调用init()加载配置和创建处理器实例
 * 2. 调用run()启动交易处理器服务
 * 3. 通过get_handler()获取处理器进行交易操作
 * 4. 调用release()清理资源
 */
class TradeHandlerAdapter: private boost::noncopyable {
public:
    /**
     * 构造函数
     * 
     * 初始化适配器，设置默认状态和成员变量。
     */
    TradeHandlerAdapter();
    
    /**
     * 析构函数
     * 
     * 自动清理资源，确保动态库正确卸载和处理器实例正确销毁。
     * 使用RAII模式保证资源安全释放。
     */
    ~TradeHandlerAdapter();

    // ===== 生命周期管理接口 =====
    
    /**
     * 初始化交易处理器适配器
     * 
     * 执行完整的初始化流程：
     * 1. 解析和验证JSON配置文件
     * 2. 加载指定的交易处理器动态库
     * 3. 创建处理器实例并获取工厂函数
     * 4. 将JSON配置转换为C结构体配置
     * 5. 初始化处理器实例
     * 6. 验证配置参数的有效性
     * 
     * @param id 处理器标识符，用于日志记录和错误追踪
     * @param config JSON配置对象指针，包含处理器所有配置信息
     * @return true初始化成功，false初始化失败
     * 
     * @note 配置要求：
     * - 必须包含"module"字段指定动态库路径
     * - 必须包含"connection"配置节指定连接参数
     * - 可选包含"accounts"数组指定交易账户
     * - 可选包含"performance"、"data"等配置节
     * 
     * @warning 此方法不是线程安全的，不应并发调用
     * @warning 失败时可通过get_last_error()获取详细错误信息
     */
    bool init(const char* id, const nlohmann::json* config);
    
    /**
     * 启动交易处理器
     * 
     * 执行以下启动操作：
     * 1. 启动处理器内部处理线程
     * 2. 建立到交易服务器的连接
     * 3. 执行登录认证流程
     * 4. 开始接收和处理交易数据
     * 5. 查询初始账户状态
     * 
     * @return true启动成功，false启动失败
     * 
     * @pre 必须先成功调用init()方法
     * @post 成功时处理器进入运行状态，可以进行交易操作
     * @note 如果已经在运行状态，重复调用将返回true
     * @note 失败时可通过get_last_error()获取详细错误信息
     */
    bool run();
    
    /**
     * 释放交易处理器资源
     * 
     * 执行完整的清理操作：
     * 1. 停止所有数据处理和交易操作
     * 2. 断开到交易服务器的连接
     * 3. 停止内部处理线程
     * 4. 销毁处理器实例
     * 5. 卸载动态库
     * 6. 清理内存资源和配置信息
     * 
     * @post 适配器回到未初始化状态，可以重新初始化
     * @note 此方法是幂等的，可以安全地多次调用
     * @note 析构函数会自动调用此方法
     * @note 线程安全，但不应与其他生命周期方法并发调用
     */
    void release();

    /**
     * @brief 停止交易处理器（断开连接并停止运行）
     * @return 成功返回true，失败返回false
     */
    bool stop();
    
    /**
     * @brief 检查处理器是否正在运行
     * @return 运行中返回true，否则返回false
     */
    bool is_running() const;

    // ===== 状态查询接口 =====
    
    /**
     * 检查是否已连接到交易服务器
     * @return true已连接，false未连接
     * @note 线程安全，委托给底层处理器实现
     * @note 连接状态独立于运行状态
     */
    bool is_connected() const;
    
    /**
     * 获取处理器标识符
     * @return 处理器ID字符串
     * @note 线程安全，返回的指针在对象生命周期内有效
     * @note 用于日志记录和调试识别
     */
    const char* get_id() const { return _id.c_str(); }
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     * @note 线程安全，返回的指针在下次错误发生前有效
     * @note 用于诊断初始化、启动或运行时的错误
     */
    const char* get_last_error() const { return _last_error.c_str(); }

    // ===== 核心访问接口 =====
    
    /**
     * 获取交易处理器实例
     * 
     * 这是最重要的接口，用于获取底层的交易处理器进行交易操作。
     * 返回的处理器支持所有标准的交易功能。
     * 
     * @return 交易处理器接口指针，未初始化时返回nullptr
     * 
     * @note 线程安全，返回的指针在适配器生命周期内有效
     * @note 调用者不应直接delete返回的指针
     * @note 可以安全地在多个线程中并发使用返回的处理器
     * 
     */
    ITradeHandler* get_handler() const { return _handler_api; }

private:
    // ===== 基本信息私有成员 =====
    std::string _id;                    ///< 处理器标识符
    std::string _last_error;            ///< 最后错误信息

    // ===== 处理器管理私有成员 =====
    ITradeHandler* _handler_api;        ///< 处理器实例指针
    DllHandle _handler_lib;             ///< 动态库句柄
    CreateTradeHandlerFuncType _create_handler_func;  ///< 创建处理器函数指针
    DeleteTradeHandlerFuncType _destroy_handler_func; ///< 销毁处理器函数指针

    // ===== 状态管理私有成员 =====
    std::atomic<bool> _running;         ///< 运行状态标志（原子操作）
    bool _stopped;                      ///< 停止状态标志

    // ===== 配置私有成员 =====
    nlohmann::json _config;             ///< JSON配置对象

    // ===== 动态库管理私有方法 =====
    
    /**
     * 加载处理器动态库
     * @param module_path 动态库文件路径
     * @return true加载成功，false加载失败
     * @note 会自动查找创建和销毁函数，验证函数签名
     */
    bool _load_handler_library(const std::string& module_path);
    
    /**
     * 创建处理器实例
     * @return true创建成功，false创建失败
     * @note 调用动态库的工厂函数创建实例
     */
    bool _create_handler_instance();
    
    /**
     * 销毁处理器实例
     * @note 调用动态库的销毁函数释放实例，设置指针为nullptr
     */
    void _destroy_handler_instance();
    
    /**
     * 卸载处理器动态库
     * @note 释放动态库句柄和函数指针，清理相关资源
     */
    void _unload_handler_library();

    // ===== 配置解析私有方法 =====
    
    /**
     * 构建交易配置结构体
     * @return 完整的TradeConfig结构体
     * @note 将JSON配置转换为C结构体格式，分配必要的内存
     */
    TradeConfig _build_trade_config();
    
    /**
     * 解析数据配置节
     * @param config JSON配置对象
     * @param data_config 输出的数据配置结构体
     * @note 解析数据落地、回调模式、缓冲区大小等配置
     */
    void _parse_data_config(const nlohmann::json& config, DataConfig& data_config);
    
    /**
     * 解析性能配置节
     * @param config JSON配置对象
     * @param perf_config 输出的性能配置结构体
     * @note 解析延迟统计、吞吐量统计、报告间隔等配置
     */
    void _parse_performance_config(const nlohmann::json& config, PerformanceConfig& perf_config);
    
    /**
     * 解析单个账户配置
     * @param config JSON账户配置对象
     * @param account 输出的账户信息结构体
     * @note 解析登陆账号、客户号、资金账户、沪深账户等信息
     */
    void _parse_account_config(const nlohmann::json& config, AccountInfo& account);
    
    /**
     * 解析账户列表配置
     * @param config JSON配置对象
     * @param account_list 输出的账户列表结构体
     * @return true解析成功，false解析失败
     * @note 解析accounts数组，分配内存存储多个账户信息
     */
    bool _parse_account_list_config(const nlohmann::json& config, AccountList& account_list);
    
    /**
     * 释放交易配置结构体中的动态内存
     * @param config 交易配置结构体引用
     * @note 释放所有动态分配的字符串和数组内存，防止内存泄漏
     */
    void _free_trade_config_memory(TradeConfig& config);

    // ===== 配置验证私有方法 =====
    
    /**
     * 验证性能配置的有效性
     * @param config 性能配置结构体
     * @return true配置有效，false配置无效
     * @note 检查报告间隔、统计开关等参数的合理性
     */
    bool _validate_performance_config(const PerformanceConfig& config);
    
    /**
     * 验证账户配置的有效性
     * @param account 账户信息结构体
     * @return true配置有效，false配置无效
     * @note 检查客户号、账户ID等字段的格式和长度
     */
    bool _validate_account_config(const AccountInfo& account);

    // ===== 辅助功能私有方法 =====
    
    /**
     * 记录配置摘要信息
     * @note 将重要配置信息写入日志，便于调试和监控
     * @note 包括处理器ID、动态库路径、账户数量等关键信息
     */
    void _log_configuration_summary();
};

NS_FT_END

#endif // TRADE_HANDLER_ADAPTER_H