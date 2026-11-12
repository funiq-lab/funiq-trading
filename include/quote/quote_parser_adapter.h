#ifndef QUOTE_PARSER_ADAPTER_H
#define QUOTE_PARSER_ADAPTER_H

#include "types/quote_types.h"
#include "quote/quote_parser.h"
#include "core/macros.h"
#include <string>
#include <memory>
#include <atomic>
#include <boost/core/noncopyable.hpp>
#include <nlohmann/json.hpp>
#include "utils/dll_helper.hpp"

NS_FT_BEGIN

/**
 * 行情解析器适配器类
 * 
 * 作为行情解析器的适配层，提供以下核心功能：
 * 1. **动态库管理**：负责加载和卸载行情解析器动态库
 * 2. **实例管理**：创建、配置和销毁行情解析器实例
 * 3. **配置解析**：将JSON配置转换为解析器所需的C结构体
 * 4. **生命周期管理**：统一管理解析器的初始化、启动、停止和清理
 * 5. **错误处理**：统一的错误信息管理和状态查询
 * 6. **配置验证**：确保配置参数的有效性和一致性
 * 
 * 设计模式：
 * - 适配器模式：将JSON配置适配为C结构体配置
 * - 工厂模式：通过动态库工厂函数创建解析器实例
 * - RAII模式：自动管理资源生命周期
 * 
 * 线程安全性：
 * - 基本线程安全，使用原子变量保护状态
 * - 不支持并发初始化和释放操作
 * - 解析器实例的线程安全性由具体实现决定
 */
class QuoteParserAdapter: private boost::noncopyable {
public:
    /**
     * 构造函数
     * 初始化适配器，设置默认状态
     */
    QuoteParserAdapter();
    
    /**
     * 析构函数
     * 自动清理资源，确保动态库正确卸载
     */
    ~QuoteParserAdapter();
    
    // ===== 生命周期管理接口 =====
    
    /**
     * 初始化行情解析器适配器
     * 
     * 执行以下步骤：
     * 1. 解析JSON配置文件
     * 2. 加载指定的行情解析器动态库
     * 3. 创建解析器实例
     * 4. 将JSON配置转换为C结构体配置
     * 5. 初始化解析器实例
     * 
     * @param id 解析器标识符，用于日志和错误追踪
     * @param config JSON配置对象指针，包含解析器所有配置信息
     * @return true初始化成功，false初始化失败
     * 
     * @note 配置格式要求：
     * - 必须包含"module"字段指定动态库路径
     * - 可选包含"connection"、"performance"、"mock"、"data"等配置节
     * - 具体配置格式参考各解析器的文档
     * 
     * @warning 此方法不是线程安全的，不应并发调用
     */
    bool init(const char* id, const nlohmann::json* config);
    
    /**
     * 启动行情解析器
     * 
     * 执行以下操作：
     * 1. 启动解析器内部处理线程
     * 2. 建立到数据源的连接
     * 3. 开始接收和处理行情数据
     * 
     * @return true启动成功，false启动失败
     * 
     * @pre 必须先成功调用init()方法
     * @note 如果已经在运行状态，重复调用将返回true
     */
    bool run();

    /**
     * @brief 停止行情解析器（断开连接并停止运行）
     * @return 成功返回true，失败返回false
     */
    bool stop();
    
    /**
     * @brief 检查解析器是否正在运行
     * @return 运行中返回true，否则返回false
     */
    bool is_running() const;
    
    /**
     * 释放行情解析器资源
     * 
     * 执行以下清理操作：
     * 1. 停止数据接收和处理
     * 2. 断开数据源连接
     * 3. 销毁解析器实例
     * 4. 卸载动态库
     * 5. 清理内存资源
     * 
     * @note 此方法是幂等的，可以安全地多次调用
     * @note 析构函数会自动调用此方法
     */
    void release();
    
    // ===== 状态查询接口 =====
    
    /**
     * 获取解析器标识符
     * @return 解析器ID字符串
     * @note 线程安全，返回的指针在对象生命周期内有效
     */
    const char* get_id() const { return _id.c_str(); }
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     * @note 线程安全，返回的指针在下次错误发生前有效
     */
    const char* get_last_error() const { return _last_error.c_str(); }
    
    // ===== 解析器访问接口 =====
    
    /**
     * 获取行情解析器实例
     * @return 解析器接口指针，未初始化时返回nullptr
     * @note 返回的指针在适配器生命周期内有效
     * @warning 调用者不应直接delete返回的指针
     */
    IQuoteParser* get_parser() const { return _parser_api; }
private:
    // ===== 基本信息私有成员 =====
    std::string _id;                    ///< 解析器标识符
    std::string _last_error;            ///< 最后错误信息
    
    // ===== 解析器管理私有成员 =====
    IQuoteParser* _parser_api;          ///< 解析器实例指针
    DllHandle _parser_lib;              ///< 动态库句柄
    CreateQuoteParserFuncType _create_parser_func;  ///< 创建解析器函数指针
    DeleteQuoteParserFuncType _destroy_parser_func; ///< 销毁解析器函数指针
    
    // ===== 状态管理私有成员 =====
    std::atomic<bool> _running;         ///< 运行状态标志（原子操作）
    bool _stopped;                      ///< 停止状态标志
    
    // ===== 配置私有成员 =====
    nlohmann::json _config;             ///< JSON配置对象
    
    // ===== 动态库管理私有方法 =====
    
    /**
     * 加载解析器动态库
     * @param module_path 动态库文件路径
     * @return true加载成功，false加载失败
     * @note 会自动查找创建和销毁函数
     */
    bool _load_parser_library(const std::string& module_path);
    
    /**
     * 创建解析器实例
     * @return true创建成功，false创建失败
     * @note 调用动态库的工厂函数创建实例
     */
    bool _create_parser_instance();
    
    /**
     * 销毁解析器实例
     * @note 调用动态库的销毁函数释放实例
     */
    void _destroy_parser_instance();
    
    /**
     * 卸载解析器动态库
     * @note 释放动态库句柄和函数指针
     */
    void _unload_parser_library();

  /**
   * 确保动态库与解析器实例已加载（当已释放后再次run时使用）
   * @return true 成功可用，false 失败
   */
  bool _ensure_loaded();

  /**
   * 完整卸载：停止、release、销毁实例并卸载动态库（供 stop/release 复用）
   */
  void _full_unload();
    
    // ===== 配置解析私有方法 =====
    
    /**
     * 构建行情配置结构体
     * @return 完整的QuoteConfig结构体
     * @note 将JSON配置转换为C结构体格式
     */
    QuoteConfig _build_quote_config();
    
    /**
     * 解析数据配置节
     * @param config JSON配置对象
     * @param data_config 输出的数据配置结构体
     * @note 解析数据落地、回调模式等配置
     */
    void _parse_data_config(const nlohmann::json& config, DataConfig& data_config);
    
    /**
     * 解析性能配置节
     * @param config JSON配置对象
     * @param perf_config 输出的性能配置结构体
     * @note 解析延迟统计、吞吐量统计等配置
     */
    void _parse_performance_config(const nlohmann::json& config, PerformanceConfig& perf_config);
    
    /**
     * 解析Mock配置节
     * @param config JSON配置对象
     * @param mock_config 输出的Mock配置结构体
     * @note 解析Mock模式、数据文件路径等配置
     */
    void _parse_mock_config(const nlohmann::json& config, MockConfig& mock_config);

    /**
     * 解析订阅配置节
     * @param config JSON配置对象
     * @param sub_config 输出的订阅配置结构体
     * @note 解析订阅配置
     */
    void _parse_subscription_config(const nlohmann::json &config, SubscriptionConfig &sub_config);
    
    // ===== 配置验证私有方法 =====
    
    /**
     * 验证性能配置的有效性
     * @param config 性能配置结构体
     * @return true配置有效，false配置无效
     * @note 检查统计间隔、缓冲区大小等参数的合理性
     */
    bool _validate_performance_config(const PerformanceConfig& config);

    
    /**
     * 记录配置摘要信息
     * @note 将重要配置信息写入日志，便于调试和监控
     */
    void _log_configuration_summary();
    
    /**
     * 释放行情配置结构体中的动态内存
     * @param config 行情配置结构体引用
     * @note 释放所有动态分配的字符串和数组内存
     */
    void _free_quote_config_memory(QuoteConfig& config);
    
};

NS_FT_END

#endif // QUOTE_PARSER_ADAPTER_H