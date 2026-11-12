#ifndef BASE_QUOTE_PARSER_H
#define BASE_QUOTE_PARSER_H

#include "quote/quote_parser.h"
#include "types/quote_types.h"
#include "data/data_manager.h"
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <shared_mutex>

NS_FT_BEGIN

#define MAX_RETRY_COUNT 3

// 前向声明
class MockDataGenerator;

/**
 * 类型化Mock数据结构
 * 用于存储不同数据类型的Mock数据记录
 */
struct TypedMockData {
    uint64_t timestamp;     ///< 数据时间戳（微秒）
    FTDataType data_type;   ///< 数据类型标识
    uint32_t data_size;     ///< 数据大小（字节）
    uint8_t data[4096];     ///< 原始数据缓冲区
    
    TypedMockData() : timestamp(0), data_type(DATA_TYPE_INDEX), data_size(0) {
        memset(data, 0, sizeof(data));
    }
};

/**
 * 行情解析器基类
 * 
 * 提供行情解析器的通用功能实现，包括：
 * - 生命周期管理（初始化、启动、停止、清理）
 * - 连接管理（连接、断开、状态查询）
 * - 性能统计（延迟统计、吞吐量统计）
 * - Mock模式（数据回放、合成数据生成）
 * - 订阅管理（市场、数据类型、证券代码订阅）
 * - 回调机制（数据推送回调）
 * - 数据获取（轮询模式数据获取）
 * - 数据落地（数据管理器集成）
 * 
 * 子类只需要实现核心的连接和数据解析功能，基类负责所有通用逻辑。
 */
class BaseQuoteParser : public IQuoteParser {
public:
    /**
     * 构造函数
     * 初始化基类成员变量和原子变量
     */
    BaseQuoteParser();
    
    /**
     * 析构函数
     * 确保资源正确释放
     */
    virtual ~BaseQuoteParser();

    // ===== 基类实现的通用接口 =====
    
    /**
     * 初始化行情解析器
     * @param config 行情配置信息，包含连接参数、性能配置、Mock配置等
     * @return 0成功，负数失败
     * @note 线程安全，可重复调用（已初始化时直接返回成功）
     */
    int init(const QuoteConfig* config) override final;
    
    /**
     * 释放行情解析器资源
     * 停止所有线程，关闭连接，清理内存
     * @note 线程安全，可重复调用
     */
    void release() override final;
    
    /**
     * 启动行情解析器
     * 启动数据接收和处理线程
     * @return 0成功，负数失败
     * @note 必须在init()成功后调用
     */
    int start() override final;
    
    /**
     * 连接到行情数据源
     * @return 0成功，负数失败
     * @note 线程安全，支持重连
     */
    int connect() override final;
    
    /**
     * 断开行情数据源连接
     * @return 0成功，负数失败
     * @note 线程安全，不影响其他功能
     */
    int disconnect() override final;
    
    /**
     * 停止行情解析器
     * 停止数据处理但保持连接
     * @return 0成功，负数失败
     */
    int stop() override final;
    
    /**
     * 检查是否已连接到数据源
     * @return true已连接，false未连接
     * @note 线程安全
     */
    bool is_connected() const override final;
    
    /**
     * 检查是否正在运行
     * @return true运行中，false已停止
     * @note 线程安全
     */
    bool is_running() const override final;

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     * @note 线程安全
     */
    bool is_initialized() const override final;
    
    /**
     * 获取当前状态
     * @return 状态枚举值
     * @note 线程安全
     */
    Status get_status() override final;
    
    /**
     * 获取最后的错误信息
     * @return 错误信息字符串
     * @note 线程安全
     */
    std::string get_last_error() const override final;
    
    // ===== 性能统计相关接口 =====
    
    /**
     * 获取延迟统计信息
     * @param avg_latency_us 输出平均延迟（微秒）
     * @param max_latency_us 输出最大延迟（微秒）
     * @param min_latency_us 输出最小延迟（微秒）
     * @note 所有参数可为NULL（跳过对应统计）
     */
    void get_latency_stats(double* avg_latency_us, double* max_latency_us, double* min_latency_us) override final;
    
    /**
     * 重置性能统计数据
     * 清零所有统计计数器和累计值
     */
    void reset_performance_stats() override final;

    // ===== Mock模式相关接口 =====
    
    /**
     * 检查是否启用Mock模式
     * @return true启用，false禁用
     */
    bool is_mock_mode() const override final;
    
    /**
     * 加载Mock数据文件
     * @param file_path Mock数据文件路径或目录路径
     * @return 0成功，负数失败
     * @note 支持单文件或分类型文件加载
     */
    int load_mock_data(const char* file_path) override final;
    
    /**
     * 开始Mock数据回放
     * @return 0成功，负数失败
     * @note 必须先调用load_mock_data()
     */
    int start_mock_replay() override final;
    
    /**
     * 停止Mock数据回放
     * @return 0成功，负数失败
     */
    int stop_mock_replay() override final;

    // ===== 订阅管理相关接口 =====
    
    /**
     * 设置订阅配置
     * @param config 订阅配置，包含市场、数据类型、证券代码等
     * @return 0成功，负数失败
     */
    int subscribe(const SubscriptionConfig* config) override final;
    
    /**
     * 取消所有订阅
     * @return 0成功，负数失败
     */
    int unsubscribe() override final;
    
    /**
     * 添加市场订阅
     * @param markets 市场类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int add_markets(MarketType* markets, int count) override final;
    
    /**
     * 添加数据类型订阅
     * @param data_types 数据类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int add_data_types(FTDataType* data_types, int count) override final;
    
    /**
     * 添加证券代码订阅
     * @param securities 证券信息数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int add_securities(SecurityInfo* securities, int count) override final;
    
    /**
     * 移除市场订阅
     * @param markets 市场类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int remove_markets(MarketType* markets, int count) override final;
    
    /**
     * 移除数据类型订阅
     * @param data_types 数据类型数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int remove_data_types(FTDataType* data_types, int count) override final;
    
    /**
     * 移除证券代码订阅
     * @param securities 证券信息数组
     * @param count 数组长度
     * @return 0成功，负数失败
     */
    int remove_securities(SecurityInfo* securities, int count) override final;
    
    /**
     * 更新订阅配置
     * @param config 新的订阅配置
     * @return 0成功，负数失败
     * @note 会替换现有的所有订阅设置
     */
    int update_subscription(const SubscriptionConfig* config) override final;
    
    /**
     * 检查市场是否已订阅
     * @param market 市场类型
     * @return true已订阅，false未订阅
     */
    bool is_market_subscribed(MarketType market) const override final;
    
    /**
     * 检查数据类型是否已订阅
     * @param data_type 数据类型
     * @return true已订阅，false未订阅
     */
    bool is_data_type_subscribed(FTDataType data_type) const override final;
    
    /**
     * 检查证券代码是否已订阅
     * @param code 证券代码
     * @return true已订阅，false未订阅
     */
    bool is_code_subscribed(const char* code) const override final;
    
    /**
     * 判断是否应该处理指定数据
     * @param market 市场类型
     * @param data_type 数据类型
     * @param code 证券代码
     * @return true应该处理，false应该忽略
     * @note 综合考虑市场、数据类型、证券代码的订阅状态
     */
    bool should_process_data(MarketType market, FTDataType data_type, const char* code) const override final;

    // ===== 回调函数注册接口 =====
    
    /**
     * 设置指数数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    IndexDataCallbackHandle set_index_data_callback(IndexDataCallbackType callback) override final;
    
    /**
     * 移除指数数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_index_data_callback(IndexDataCallbackHandle handle) override final;
    
    /**
     * 设置深度数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    DepthDataCallbackHandle set_depth_data_callback(DepthDataCallbackType callback) override final;
    
    /**
     * 移除深度数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_depth_data_callback(DepthDataCallbackHandle handle) override final;
    
    /**
     * 设置50档深度数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    DepthFiftyDataCallbackHandle set_depth_fifty_data_callback(DepthFiftyDataCallbackType callback) override final;
    
    /**
     * 移除50档深度数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_depth_fifty_data_callback(DepthFiftyDataCallbackHandle handle) override final;
    
    /**
     * 设置委托数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    EntrustDataCallbackHandle set_entrust_data_callback(EntrustDataCallbackType callback) override final;
    
    /**
     * 移除委托数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_entrust_data_callback(EntrustDataCallbackHandle handle) override final;
    
    /**
     * 设置成交数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    TradeDataCallbackHandle set_trade_data_callback(TradeDataCallbackType callback) override final;
    
    /**
     * 移除成交数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_trade_data_callback(TradeDataCallbackHandle handle) override final;
    
    /**
     * 设置状态变化回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     */
    StatusCallbackHandle set_status_callback(QuoteStatusCallbackType callback) override final;
    
    /**
     * 移除状态变化回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_status_callback(StatusCallbackHandle handle) override final;

    // ===== 数据获取接口（轮询模式）=====
    
    /**
     * 获取最新指数数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_index_data(IndexData* data) override final;
    
    /**
     * 获取最新深度数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_depth_data(DepthData* data) override final;
    
    /**
     * 获取最新50档深度数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_depth_fifty_data(DepthFiftyData* data) override final;
    
    /**
     * 获取最新委托数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_entrust_data(EntrustData* data) override final;
    
    /**
     * 获取最新成交数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_trade_data(TradeData* data) override final;

    /**
     * @brief 根据证券代码获取最新指数数据
     * @param code 证券代码
     * @param data 输出的指数数据
     * @return 0 成功，-1 失败（未找到数据或其他错误）
     */
    int get_index_data_by_code(const char* code, IndexData* data);

    /**
     * @brief 根据证券代码获取最新深度数据
     * @param code 证券代码
     * @param data 输出的深度数据
     * @return 0 成功，-1 失败
     */
    int get_depth_data_by_code(const char* code, DepthData* data);

    /**
     * @brief 根据证券代码获取最新50档数据
     * @param code 证券代码
     * @param data 输出的50档数据
     * @return 0 成功，-1 失败
     */
    int get_depth_fifty_data_by_code(const char* code, DepthFiftyData* data);

    /**
     * @brief 根据证券代码获取最新委托数据
     * @param code 证券代码
     * @param data 输出的委托数据
     * @return 0 成功，-1 失败
     */
    int get_entrust_data_by_code(const char* code, EntrustData* data);

    /**
     * @brief 根据证券代码获取最新成交数据
     * @param code 证券代码
     * @param data 输出的成交数据
     * @return 0 成功，-1 失败
     */
    int get_trade_data_by_code(const char* code, TradeData* data);

protected:
    // ===== 子类需要实现的核心接口 =====
    
    /**
     * 子类特定的初始化实现
     * @param config 行情配置信息
     * @return 0成功，负数失败
     * @note 在基类init()中调用，用于子类特定的初始化逻辑
     */
    virtual int init_impl(const QuoteConfig* config) = 0;
    
    /**
     * 子类特定的启动实现
     * @return 0成功，负数失败
     * @note 在基类start()中调用，用于启动子类特定的数据接收线程等
     */
    virtual int start_impl() = 0;
    
    /**
     * 子类特定的连接实现
     * @return 0成功，负数失败
     * @note 在基类connect()中调用，用于建立到数据源的连接
     */
    virtual int connect_impl() = 0;
    
    /**
     * 子类特定的断开实现
     * @return 0成功，负数失败
     * @note 在基类disconnect()中调用，用于断开数据源连接
     */
    virtual int disconnect_impl() = 0;
    
    /**
     * 子类特定的停止实现
     * @return 0成功，负数失败
     * @note 在基类stop()中调用，用于停止数据接收但保持连接
     */
    virtual int stop_impl() = 0;
    
    /**
     * 子类特定的清理实现
     * @note 在基类release()中调用，用于清理子类特定的资源
     */
    virtual void release_impl() = 0;

    // ===== 基类提供的辅助方法 =====
    
    /**
     * 设置错误信息
     * @param error 错误描述字符串
     * @note 线程安全，用于子类设置错误状态
     */
    void set_error(const std::string& error);
    
    /**
     * 配置性能统计参数
     * @param config 性能配置信息
     * @note 在init阶段调用，配置延迟统计、吞吐量统计等
     */
    void configure_performance_stats(const PerformanceConfig& config);
    
    /**
     * 配置Mock模式参数
     * @param config Mock配置信息
     * @note 在init阶段调用，配置Mock数据文件、回放参数等
     */
    void configure_mock_mode(const MockConfig& config);
    
    /**
     * 更新性能统计数据
     * @param processing_time_ns 处理时间（纳秒）
     * @note 子类在处理数据时调用，用于统计延迟
     */
    void update_performance_stats(uint64_t processing_time_ns);
    
    // ===== 通用数据解析辅助方法 =====
    
    /**
     * 解析上交所交易阶段代码
     * @param phase_code 交易阶段代码字符串
     * @return 交易阶段枚举值
     * @note 静态方法，可直接调用
     */
    static TradingPhase parse_sse_trading_phase(const char* phase_code);
    
    /**
     * 解析深交所交易阶段代码
     * @param phase_code 交易阶段代码字符串
     * @return 交易阶段枚举值
     * @note 静态方法，可直接调用
     */
    static TradingPhase parse_szse_trading_phase(const char* phase_code);
    
    // ===== 数据推送接口的基类实现 =====
    
    /**
     * 指数数据接收处理
     * @param data 指数数据
     * @note 子类调用此方法推送指数数据，基类负责回调和缓存
     */
    void on_index_data_received(const IndexData& data) override final;
    
    /**
     * 深度数据接收处理
     * @param data 深度数据
     * @note 子类调用此方法推送深度数据，基类负责回调和缓存
     */
    void on_depth_data_received(const DepthData& data) override final;
    
    /**
     * 50档深度数据接收处理
     * @param data 50档深度数据
     * @note 子类调用此方法推送50档深度数据，基类负责回调和缓存
     */
    void on_depth_fifty_data_received(const DepthFiftyData& data) override final;
    
    /**
     * 委托数据接收处理
     * @param data 委托数据
     * @note 子类调用此方法推送委托数据，基类负责回调和缓存
     */
    void on_entrust_data_received(const EntrustData& data) override final;
    
    /**
     * 成交数据接收处理
     * @param data 成交数据
     * @note 子类调用此方法推送成交数据，基类负责回调和缓存
     */
    void on_trade_data_received(const TradeData& data) override final;
    
    /**
     * 状态变化接收处理
     * @param status 新状态
     * @param message 状态消息
     * @note 子类调用此方法推送状态变化，基类负责回调
     */
    void on_quote_status_received(Status status, const char* message) override final;

    // ===== 数据落地方法 =====
    
    /**
     * 写入数据到数据管理器
     * @param data_type 数据类型
     * @param data 数据指针
     * @param size 数据大小
     * @return true成功，false失败
     * @note 用于数据持久化存储
     */
    bool write_to_data_manager(FTDataType data_type, const void* data, size_t size);
    
    // ===== 受保护的成员变量 =====
    mutable std::mutex _mutex;          ///< 保护共享数据的互斥锁
    std::string _last_error;            ///< 最后的错误信息
    
    // 状态管理
    std::atomic<bool> _running;         ///< 运行状态标志
    std::atomic<bool> _initialized;     ///< 初始化状态标志
    std::atomic<bool> _connected;       ///< 连接状态标志
    std::atomic<Status> _current_status; ///< 当前状态
    std::atomic<bool> _released;        ///< 资源释放标志

    // 回调模式
    std::atomic<bool> _callback_mode_enabled; ///< 回调模式启用标志

private:
    // ===== 性能统计相关私有成员 =====
    std::atomic<bool> _performance_enabled;    ///< 性能统计启用标志
    bool _latency_stats_enabled;               ///< 延迟统计启用标志
    uint32_t _report_interval_ms;              ///< 性能报告间隔（毫秒）
    
    std::atomic<uint64_t> _total_messages;     ///< 总消息数
    std::atomic<uint64_t> _dropped_messages;   ///< 丢弃消息数
    std::atomic<uint64_t> _messages_per_second; ///< 每秒消息数
    std::chrono::steady_clock::time_point _last_stats_time; ///< 上次统计时间
    
    uint64_t _total_latency_us;                ///< 总延迟（微秒）
    uint64_t _max_latency_us;                  ///< 最大延迟（微秒）
    uint64_t _min_latency_us;                  ///< 最小延迟（微秒）
    std::chrono::steady_clock::time_point _last_performance_report; ///< 上次性能报告时间
    std::thread _performance_report_thread;    ///< 性能报告线程

    // ===== Mock模式相关私有成员 =====
    std::atomic<bool> _mock_mode_enabled;      ///< Mock模式启用标志
    bool _mock_loop_replay;                    ///< 循环回放标志
    std::atomic<bool> _mock_data_loaded;       ///< Mock数据加载标志
    std::atomic<bool> _mock_replay_running;    ///< Mock回放运行标志
    char _mock_data_file[256];                 ///< Mock数据文件路径
    size_t _mock_current_index;                ///< 当前回放索引
    std::thread _mock_replay_thread;           ///< Mock回放线程
    
    // 分数据类型存储Mock数据
    std::unordered_map<FTDataType, std::vector<TypedMockData>> _typed_mock_data_buffers; ///< 分类型Mock数据缓冲区
    std::unordered_map<FTDataType, size_t> _typed_mock_indexes; ///< 每种数据类型的当前索引
    std::unordered_map<FTDataType, std::ifstream> _typed_mock_file_streams; ///< 分类型文件流
    
    bool _mock_use_synthetic_data;             ///< 使用合成数据标志
    std::unique_ptr<MockDataGenerator> _mock_generator; ///< Mock数据生成器
    bool _mock_generator_setup;                ///< Mock生成器设置标志

    // ===== 订阅配置私有成员 =====
    std::atomic<bool> _has_subscription;       ///< 订阅状态标志
    std::unordered_set<MarketType> _subscribed_markets;    ///< 已订阅市场集合
    std::unordered_set<FTDataType> _subscribed_data_types; ///< 已订阅数据类型集合
    std::unordered_set<std::string> _subscribed_codes;     ///< 已订阅证券代码集合
    
    // ===== 回调函数私有成员 =====
    IndexDataCallbackList _index_callbacks;
    DepthDataCallbackList _depth_callbacks;
    DepthFiftyDataCallbackList _depth_fifty_callbacks;
    EntrustDataCallbackList _entrust_callbacks;
    TradeDataCallbackList _trade_callbacks;
    StatusCallbackList _status_callbacks;

    // ===== 最新数据缓存私有成员 =====
    IndexData _latest_index_data;            ///< 最新指数数据缓存
    DepthData _latest_depth_data;            ///< 最新深度数据缓存
    DepthFiftyData _latest_depth_fifty_data; ///< 最新50档深度数据缓存
    EntrustData _latest_entrust_data;        ///< 最新委托数据缓存
    TradeData _latest_trade_data;            ///< 最新成交数据缓存

    // ===== 数据落地相关私有成员 =====
    std::unique_ptr<DataManager> _data_manager; ///< 数据管理器实例
    
    // ===== 按证券代码索引的数据缓存 =====
    mutable std::shared_mutex _code_cache_mutex;
    std::unordered_map<std::string, IndexData> _index_data_cache;
    std::unordered_map<std::string, DepthData> _depth_data_cache;
    std::unordered_map<std::string, DepthFiftyData> _depth_fifty_data_cache;
    std::unordered_map<std::string, EntrustData> _entrust_data_cache;
    std::unordered_map<std::string, TradeData> _trade_data_cache;

    // ===== 私有方法 =====
    
    /**
     * 初始化数据管理器
     * @param config 数据配置
     * @return true成功，false失败
     */
    bool _initialize_data_manager(const DataConfig& config);
    
    /**
     * 启动性能报告线程
     */
    void _start_performance_reporting();
    
    /**
     * 性能统计报告
     */
    void _report_performance_stats();
    
    // Mock数据加载相关私有方法
    
    /**
     * 加载Mock数据
     * @return true成功，false失败
     */
    bool _load_mock_data();
    
    /**
     * 启动Mock数据回放
     * @return 0成功，负数失败
     */
    int _start_mock_replay();
    
    /**
     * Mock数据回放循环
     */
    void _mock_replay_loop();
    
    /**
     * 生成Mock数据
     */
    void _generate_mock_data();
    
    // 类型化Mock数据相关私有方法
    
    /**
     * 获取分类型Mock文件路径
     * @param base_path 基础路径
     * @param data_type 数据类型
     * @return 完整文件路径
     */
    std::string _get_typed_mock_file_path(const std::string& base_path, FTDataType data_type);
    
    /**
     * 加载指定类型的Mock文件
     * @param data_type 数据类型
     * @param file_path 文件路径
     * @return true成功，false失败
     */
    bool _load_typed_mock_file(FTDataType data_type, const std::string& file_path);
    
    /**
     * 回放分类型Mock数据
     * @return true成功，false失败
     */
    bool _replay_typed_mock_data();
    
    /**
     * 回放单条分类型Mock记录
     * @param data_type 数据类型
     * @param mock_data Mock数据记录
     * @return true成功，false失败
     */
    bool _replay_typed_mock_record(FTDataType data_type, const TypedMockData& mock_data);
    
    /**
     * 重置分类型Mock索引
     */
    void _reset_typed_mock_indexes();
    
    /**
     * 关闭分类型Mock文件
     */
    void _close_typed_mock_files();
    
    // Mock生成器相关私有方法
    
    /**
     * 设置Mock生成器
     */
    void _setup_mock_generator();
    
    /**
     * Mock指数数据回调
     * @param data 指数数据
     */
    void _on_mock_index_data(const IndexData& data);
    
    /**
     * Mock深度数据回调
     * @param data 深度数据
     */
    void _on_mock_depth_data(const DepthData& data);
    
    /**
     * Mock 50档深度数据回调
     * @param data 50档深度数据
     */
    void _on_mock_depth_fifty_data(const DepthFiftyData& data);
    
    /**
     * Mock委托数据回调
     * @param data 委托数据
     */
    void _on_mock_entrust_data(const EntrustData& data);
    
    /**
     * Mock成交数据回调
     * @param data 成交数据
     */
    void _on_mock_trade_data(const TradeData& data);

    /**
     * @brief 更新证券代码缓存
     * @param code 证券代码
     * @param data 数据
     */
    void _update_index_cache(const std::string& code, const IndexData& data);

    /**
     * @brief 更新深度数据缓存
     * @param code 证券代码
     * @param data 数据
     */
    void _update_depth_cache(const std::string& code, const DepthData& data);

    /**
     * @brief 更新50档深度数据缓存
     * @param code 证券代码
     * @param data 数据
     */
    void _update_depth_fifty_cache(const std::string& code, const DepthFiftyData& data);

    /**
     * @brief 更新委托数据缓存
     * @param code 证券代码
     * @param data 数据
     */
    void _update_entrust_cache(const std::string& code, const EntrustData& data);

    /**
     * @brief 更新成交数据缓存
     * @param code 证券代码
     * @param data 数据
     */
    void _update_trade_cache(const std::string& code, const TradeData& data);

    // 数据读取相关私有方法
    
    /**
     * 从数据管理器获取数据
     * @param data_type 数据类型
     * @param data 输出数据指针
     * @param data_size 数据大小
     * @return 1成功获取，0无数据，-1失败
     */
    int _get_data_from_manager(FTDataType data_type, void* data, size_t data_size);
    
    /**
     * 从内存缓存获取数据
     * @param data_type 数据类型
     * @param data 输出数据指针
     * @param data_size 数据大小
     * @return 1成功获取，0无数据，-1失败
     */
    int _get_data_from_memory(FTDataType data_type, void* data, size_t data_size);

    /**
     * 初始化订阅配置
     * @param config 订阅配置
     * @note 在init阶段调用，用于设置初始订阅配置
     */
    void _initialize_subscription_config(const SubscriptionConfig* config);

    /**
     * 执行订阅更新的核心逻辑（不加锁）
     * @param config 订阅配置
     * @note 被subscribe和_initialize_subscription_config调用
     */
    void _do_subscription_update(const SubscriptionConfig* config);
};

NS_FT_END

#endif // BASE_QUOTE_PARSER_H