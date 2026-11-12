#ifndef QUOTE_PARSER_H
#define QUOTE_PARSER_H

#include "types/quote_types.h"
#include "core/macros.h"

// ===== C 接口部分 - 全局命名空间 =====
#ifdef __cplusplus
extern "C" {
#endif

// 前向声明基类接口
typedef struct IQuoteParser IQuoteParser;

/**
 * 创建行情解析器实例
 * 
 * 这是动态库的工厂函数，用于创建具体的行情解析器实例。
 * 每个行情解析器动态库都必须实现这个函数。
 * 
 * @return 解析器实例指针，创建失败时返回NULL
 * 
 * @note 这是C接口，用于动态库加载
 * @note 返回的实例需要调用delete_quote_parser()释放
 * @note 函数名必须严格匹配，用于dlsym查找
 */
EXPORT_FLAG IQuoteParser* create_quote_parser(void);

/**
 * 销毁行情解析器实例
 * 
 * 释放由create_quote_parser()创建的解析器实例。
 * 会自动调用实例的release()方法清理资源。
 * 
 * @param parser 解析器实例指针的引用，销毁后会被设置为NULL
 * 
 * @note 这是C接口，用于动态库加载
 * @note 传入NULL指针是安全的（不执行任何操作）
 * @note 函数名必须严格匹配，用于dlsym查找
 */
EXPORT_FLAG void delete_quote_parser(IQuoteParser*& parser);

#ifdef __cplusplus
}

// ===== C++ 接口部分 - ft 命名空间 =====
#include <string>
#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <boost/unordered_map.hpp>
#include <boost/circular_buffer.hpp>

#include "eventpp/callbacklist.h"

NS_FT_BEGIN

/**
 * 行情解析器抽象接口类
 * 
 * 定义了所有行情解析器必须实现的标准接口，包括：
 * 
 * **核心功能**：
 * - 生命周期管理（初始化、启动、停止、清理）
 * - 连接管理（连接、断开、状态查询）
 * - 数据订阅（市场、数据类型、证券代码）
 * - 数据获取（轮询模式和回调模式）
 * - Mock模式（数据回放和测试）
 * - 性能统计（延迟和吞吐量监控）
 * 
 * **设计原则**：
 * - 接口与实现分离：具体解析器通过动态库实现
 * - 支持多种数据源：统一接口适配不同的行情数据源
 * - 线程安全：接口设计考虑多线程环境
 * - 高性能：支持高频数据处理和低延迟要求
 * 
 * **使用模式**：
 * 1. 通过动态库工厂函数创建实例
 * 2. 调用init()初始化配置
 * 3. 调用start()启动服务
 * 4. 调用connect()连接数据源
 * 5. 设置回调或轮询获取数据
 * 6. 调用disconnect()、stop()、release()清理
 */
class IQuoteParser {
public:
    /**
     * 虚析构函数
     * 确保派生类能够正确析构
     */
    virtual ~IQuoteParser() {}

    // ===== 生命周期管理接口 =====
    
    /**
     * 初始化行情解析器
     * 
     * 根据配置信息初始化解析器的各个组件，包括：
     * - 连接参数配置
     * - 性能统计配置
     * - Mock模式配置
     * - 数据输出配置
     * - 订阅配置
     * 
     * @param config 行情配置结构体指针，包含所有初始化参数
     * @return 0成功，负数失败
     * 
     * @pre config不能为NULL且必须包含有效配置
     * @post 成功时解析器进入已初始化状态
     * @note 可以重复调用，但只有第一次调用生效
     */
    virtual int init(const QuoteConfig* config) = 0;
    
    /**
     * 启动行情解析器服务
     * 
     * 启动内部处理线程和服务组件，准备接收数据。
     * 不会自动连接到数据源，需要单独调用connect()。
     * 
     * @return 0成功，负数失败
     * 
     * @pre 必须先成功调用init()
     * @post 成功时解析器进入运行状态
     * @note 重复调用是安全的
     */
    virtual int start() = 0;
    
    /**
     * 连接到行情数据源
     * 
     * 建立到行情服务器的网络连接，开始接收行情数据。
     * 支持断线重连机制。
     * 
     * @return 0成功，负数失败
     * 
     * @pre 必须先成功调用start()
     * @post 成功时开始接收行情数据
     * @note 支持重连，连接失败时可以重试
     */
    virtual int connect() = 0;
    
    /**
     * 断开行情数据源连接
     * 
     * 断开网络连接但保持服务运行状态。
     * 不影响已缓存的数据和配置。
     * 
     * @return 0成功，负数失败
     * 
     * @post 停止接收新的行情数据
     * @note 可以重新调用connect()恢复连接
     */
    virtual int disconnect() = 0;
    
    /**
     * 停止行情解析器服务
     * 
     * 停止所有处理线程，但保持初始化状态。
     * 会自动断开连接。
     * 
     * @return 0成功，负数失败
     * 
     * @post 解析器进入停止状态
     * @note 可以重新调用start()恢复服务
     */
    virtual int stop() = 0;
    
    /**
     * 释放行情解析器资源
     * 
     * 清理所有资源，包括：
     * - 停止所有线程
     * - 断开所有连接
     * - 释放内存缓冲区
     * - 清理配置信息
     * 
     * @post 解析器回到未初始化状态
     * @note 此方法是幂等的，可以安全地多次调用
     */
    virtual void release() = 0;

    // ===== 连接状态查询接口 =====
    
    /**
     * 检查是否已连接到数据源
     * @return true已连接，false未连接
     * @note 线程安全
     */
    virtual bool is_connected() const = 0;
    
    /**
     * 检查服务是否正在运行
     * @return true运行中，false已停止
     * @note 线程安全
     */
    virtual bool is_running() const = 0;

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     * @note 线程安全
     */
    virtual bool is_initialized() const = 0;
    
    /**
     * 获取当前运行状态
     * @return 状态枚举值
     * @note 线程安全，提供比布尔值更详细的状态信息
     */
    virtual Status get_status() = 0;
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     * @note 线程安全，返回最近一次操作的错误信息
     */
    virtual std::string get_last_error() const = 0;

    // ===== Mock模式管理接口 =====
    
    /**
     * 检查是否启用Mock模式
     * @return true启用Mock模式，false使用真实数据
     * @note Mock模式用于测试和开发，回放历史数据
     */
    virtual bool is_mock_mode() const = 0;
    
    /**
     * 加载Mock数据文件
     * @param file_path Mock数据文件路径
     * @return 0成功，负数失败
     * @note 支持多种格式的Mock数据文件
     */
    virtual int load_mock_data(const char* file_path) = 0;
    
    /**
     * 开始Mock数据回放
     * @return 0成功，负数失败
     * @pre 必须先成功调用load_mock_data()
     */
    virtual int start_mock_replay() = 0;
    
    /**
     * 停止Mock数据回放
     * @return 0成功，负数失败
     */
    virtual int stop_mock_replay() = 0;

    // ===== 订阅配置管理接口 =====
    
    /**
     * 设置订阅配置
     * @param config 订阅配置结构体指针
     * @return 0成功，负数失败
     * @note 会替换现有的所有订阅设置
     */
    virtual int subscribe(const SubscriptionConfig* config) = 0;
    
    /**
     * 取消所有订阅
     * @return 0成功，负数失败
     * @post 停止接收所有行情数据
     */
    virtual int unsubscribe() = 0;
    
    /**
     * 添加市场订阅
     * @param markets 市场类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     * @note 增量添加，不影响现有订阅
     */
    virtual int add_markets(MarketType* markets, int count) = 0;
    
    /**
     * 添加数据类型订阅
     * @param data_types 数据类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     * @note 增量添加，不影响现有订阅
     */
    virtual int add_data_types(FTDataType* data_types, int count) = 0;
    
    /**
     * 添加证券代码订阅
     * @param securities 证券信息数组
     * @param count 数组长度
     * @return 0成功，负数失败
     * @note 增量添加，不影响现有订阅
     */
    virtual int add_securities(SecurityInfo* securities, int count) = 0;
    
    /**
     * 移除市场订阅
     * @param markets 市场类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    virtual int remove_markets(MarketType* markets, int count) = 0;
    
    /**
     * 移除数据类型订阅
     * @param data_types 数据类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    virtual int remove_data_types(FTDataType* data_types, int count) = 0;
    
    /**
     * 移除证券代码订阅
     * @param securities 证券信息数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    virtual int remove_securities(SecurityInfo* securities, int count) = 0;
    
    /**
     * 更新订阅配置
     * @param config 新的订阅配置
     * @return 0成功，负数失败
     * @note 完全替换现有订阅配置
     */
    virtual int update_subscription(const SubscriptionConfig* config) = 0;
    
    /**
     * 检查市场是否已订阅
     * @param market 市场类型
     * @return true已订阅，false未订阅
     */
    virtual bool is_market_subscribed(MarketType market) const = 0;
    
    /**
     * 检查数据类型是否已订阅
     * @param data_type 数据类型
     * @return true已订阅，false未订阅
     */
    virtual bool is_data_type_subscribed(FTDataType data_type) const = 0;
    
    /**
     * 检查证券代码是否已订阅
     * @param code 证券代码字符串
     * @return true已订阅，false未订阅
     */
    virtual bool is_code_subscribed(const char* code) const = 0;
    
    /**
     * 判断是否应该处理指定数据
     * @param market 市场类型
     * @param data_type 数据类型
     * @param code 证券代码
     * @return true应该处理，false应该忽略
     * @note 综合考虑市场、数据类型、证券代码的订阅状态
     */
    virtual bool should_process_data(MarketType market, FTDataType data_type, const char* code) const = 0;

    // ===== 回调函数注册接口=====
    
    /**
     * 设置指数数据回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在数据接收线程中执行，应避免阻塞操作
     */
    virtual IndexDataCallbackHandle set_index_data_callback(IndexDataCallbackType callback) = 0;
    
    /**
     * 移除指数数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_index_data_callback(IndexDataCallbackHandle handle) = 0;
    
    /**
     * 设置深度数据回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在数据接收线程中执行，应避免阻塞操作
     */
    virtual DepthDataCallbackHandle set_depth_data_callback(DepthDataCallbackType callback) = 0;
    
    /**
     * 移除深度数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_depth_data_callback(DepthDataCallbackHandle handle) = 0;
    
    /**
     * 设置50档深度数据回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在数据接收线程中执行，应避免阻塞操作
     */
    virtual DepthFiftyDataCallbackHandle set_depth_fifty_data_callback(DepthFiftyDataCallbackType callback) = 0;
    
    /**
     * 移除50档深度数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_depth_fifty_data_callback(DepthFiftyDataCallbackHandle handle) = 0;
    
    /**
     * 设置委托数据回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在数据接收线程中执行，应避免阻塞操作
     */
    virtual EntrustDataCallbackHandle set_entrust_data_callback(EntrustDataCallbackType callback) = 0;
    
    /**
     * 移除委托数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_entrust_data_callback(EntrustDataCallbackHandle handle) = 0;
    
    /**
     * 设置成交数据回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在数据接收线程中执行，应避免阻塞操作
     */
    virtual TradeDataCallbackHandle set_trade_data_callback(TradeDataCallbackType callback) = 0;
    
    /**
     * 移除成交数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_trade_data_callback(TradeDataCallbackHandle handle) = 0;
    
    /**
     * 设置状态变化回调函数
     * @param callback 回调函数指针，NULL表示取消回调
     * @return 回调句柄，可用于后续删除回调
     * @note 用于监控连接状态、错误信息等
     */
    virtual StatusCallbackHandle set_status_callback(QuoteStatusCallbackType callback) = 0;
    
    /**
     * 移除状态变化回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_status_callback(StatusCallbackHandle handle) = 0;

    // ===== 行情数据获取接口（轮询模式）=====
    
    /**
     * 获取最新指数行情数据
     * 
     * 用于轮询模式获取指数数据，包括指数价格、涨跌幅等信息。
     * 
     * @param data 输出数据指针，不能为NULL
     * @return 1成功获取新数据，0无新数据，-1失败
     * 
     * @note 线程安全，可以在多个线程中并发调用
     * @note 返回的是最新一条数据的拷贝
     * @note 适用于低频查询场景
     */
    virtual int get_index_data(IndexData* data) = 0;
    
    /**
     * 获取最新竞价行情快照数据
     * 
     * 用于轮询模式获取Level-1行情数据，包括买卖五档、成交量等。
     * 
     * @param data 输出数据指针，不能为NULL
     * @return 1成功获取新数据，0无新数据，-1失败
     * 
     * @note 线程安全，可以在多个线程中并发调用
     * @note 返回的是最新一条数据的拷贝
     * @note 这是最常用的行情数据类型
     */
    virtual int get_depth_data(DepthData* data) = 0;
    
    /**
     * 获取最新竞价行情队列数据（50档）
     * 
     * 用于轮询模式获取Level-2深度行情数据，包括买卖50档队列。
     * 
     * @param data 输出数据指针，不能为NULL
     * @return 1成功获取新数据，0无新数据，-1失败
     * 
     * @note 线程安全，可以在多个线程中并发调用
     * @note 数据量较大，建议按需订阅
     * @note 不是所有数据源都支持50档数据
     */
    virtual int get_depth_fifty_data(DepthFiftyData* data) = 0;
    
    /**
     * 获取最新逐笔委托数据
     * 
     * 用于轮询模式获取逐笔委托数据，反映市场的委托意向。
     * 
     * @param data 输出数据指针，不能为NULL
     * @return 1成功获取新数据，0无新数据，-1失败
     * 
     * @note 线程安全，可以在多个线程中并发调用
     * @note 数据频率很高，建议使用回调模式
     * @note 主要用于高频交易和市场微观结构分析
     */
    virtual int get_entrust_data(EntrustData* data) = 0;
    
    /**
     * 获取最新逐笔成交数据
     * 
     * 用于轮询模式获取逐笔成交数据，反映实际的交易情况。
     * 
     * @param data 输出数据指针，不能为NULL
     * @return 1成功获取新数据，0无新数据，-1失败
     * 
     * @note 线程安全，可以在多个线程中并发调用
     * @note 数据频率很高，建议使用回调模式
     * @note 用于成交量价分析和实时监控
     */
    virtual int get_trade_data(TradeData* data) = 0;

     /**
     * @brief 根据证券代码获取最新指数数据
     * @param code 证券代码
     * @param data 输出的指数数据
     * @return 0 成功，-1 失败（未找到数据或其他错误）
     */
    virtual int get_index_data_by_code(const char* code, IndexData* data) = 0;

    /**
     * @brief 根据证券代码获取最新深度数据
     * @param code 证券代码
     * @param data 输出的深度数据
     * @return 0 成功，-1 失败
     */
    virtual int get_depth_data_by_code(const char* code, DepthData* data) = 0;

    /**
     * @brief 根据证券代码获取最新50档数据
     * @param code 证券代码
     * @param data 输出的50档数据
     * @return 0 成功，-1 失败
     */
    virtual int get_depth_fifty_data_by_code(const char* code, DepthFiftyData* data) = 0;

    /**
     * @brief 根据证券代码获取最新委托数据
     * @param code 证券代码
     * @param data 输出的委托数据
     * @return 0 成功，-1 失败
     */
    virtual int get_entrust_data_by_code(const char* code, EntrustData* data) = 0;

    /**
     * @brief 根据证券代码获取最新成交数据
     * @param code 证券代码
     * @param data 输出的成交数据
     * @return 0 成功，-1 失败
     */
    virtual int get_trade_data_by_code(const char* code, TradeData* data) = 0;

    // ===== 性能统计接口 =====
    
    /**
     * 获取延迟统计信息
     * @param avg_latency_us 输出平均延迟（微秒），可为NULL
     * @param max_latency_us 输出最大延迟（微秒），可为NULL
     * @param min_latency_us 输出最小延迟（微秒），可为NULL
     * @note 用于监控数据处理性能，所有参数都可以为NULL
     */
    virtual void get_latency_stats(double* avg_latency_us, double* max_latency_us, double* min_latency_us) = 0;
    
    /**
     * 重置性能统计数据
     * @note 清零所有统计计数器，重新开始统计
     */
    virtual void reset_performance_stats() = 0;

protected:
    // ===== 子类实现的数据推送接口（内部使用）=====
    
    /**
     * 指数数据接收处理（内部接口）
     * @param data 指数数据引用
     * @note 子类在接收到指数数据时调用，触发回调和缓存更新
     */
    virtual void on_index_data_received(const IndexData& data) = 0;
    
    /**
     * 深度数据接收处理（内部接口）
     * @param data 深度数据引用
     * @note 子类在接收到深度数据时调用，触发回调和缓存更新
     */
    virtual void on_depth_data_received(const DepthData& data) = 0;
    
    /**
     * 50档深度数据接收处理（内部接口）
     * @param data 50档深度数据引用
     * @note 子类在接收到50档数据时调用，触发回调和缓存更新
     */
    virtual void on_depth_fifty_data_received(const DepthFiftyData& data) = 0;
    
    /**
     * 委托数据接收处理（内部接口）
     * @param data 委托数据引用
     * @note 子类在接收到委托数据时调用，触发回调和缓存更新
     */
    virtual void on_entrust_data_received(const EntrustData& data) = 0;
    
    /**
     * 成交数据接收处理（内部接口）
     * @param data 成交数据引用
     * @note 子类在接收到成交数据时调用，触发回调和缓存更新
     */
    virtual void on_trade_data_received(const TradeData& data) = 0;
    
    /**
     * 状态变化接收处理（内部接口）
     * @param status 新状态
     * @param message 状态消息
     * @note 子类在状态发生变化时调用，触发状态回调
     */
    virtual void on_quote_status_received(Status status, const char* message) = 0;
};

/**
 * 解析器工厂函数类型定义
 * 
 * 用于动态库中的工厂函数类型定义：
 * - CreateQuoteParserFuncType: 创建解析器实例的函数类型
 * - DeleteQuoteParserFuncType: 销毁解析器实例的函数类型
 * 
 * @note 这些函数类型用于dlsym查找动态库中的导出函数
 */
typedef IQuoteParser* (*CreateQuoteParserFuncType)();    ///< 创建解析器函数类型
typedef void (*DeleteQuoteParserFuncType)(IQuoteParser*&); ///< 销毁解析器函数类型

NS_FT_END

#endif // __cplusplus

#endif // QUOTE_PARSER_H