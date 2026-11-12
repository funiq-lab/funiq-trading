#ifndef STRATEGY_SERVICE_H
#define STRATEGY_SERVICE_H

#include "strategy/strategy_interface.h"
#include "types/strategy_types.h"
#include "core/macros.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

NS_FT_BEGIN

// 前向声明
class StrategyAdapter;
class IQuoteParser;
class ITradeHandler;
class TradingTimeManager;
class TwapAlgorithm;

/**
 * 数据获取模式枚举
 */
enum class DataMode {
    CALLBACK = 0,   ///< 回调模式：注册回调函数获取数据
    POLLING = 1     ///< 轮询模式：通过DataManager轮询获取数据
};

/**
 * 策略服务类
 * 
 * 负责策略系统的整体管理和协调，支持多策略并发运行：
 * 1. 多策略生命周期管理：创建、启动、停止、销毁多个策略实例
 * 2. 数据流管理：行情和交易数据的并发分发
 * 3. 依赖服务管理：行情解析器和交易处理器的注入
 * 4. 配置管理：从配置文件加载策略设置
 * 5. 线程池管理：支持自定义线程数量的并发执行
 */
class StrategyService {
public:
    /**
     * 构造函数
     */
    StrategyService();
    
    /**
     * 析构函数
     */
    ~StrategyService() noexcept;
    
    // 禁用拷贝和赋值
    StrategyService(const StrategyService&) = delete;
    StrategyService& operator=(const StrategyService&) = delete;
    
    // ===== 服务生命周期管理 =====
    
    /**
     * 初始化策略服务
     * @return 0成功，负数失败
     */
    int init();
    
    /**
     * 启动策略服务（不启动具体策略）
     * @return 0成功，负数失败
     */
    int start_service();
    
    /**
     * 停止策略服务
     * @return 0成功，负数失败
     */
    int stop_service();
    
    /**
     * 释放策略服务资源
     */
    void release();

    // ===== 多策略实例管理 =====
    
    /**
     * 创建策略实例
     * @param strategy_id 策略ID
     * @param config 策略配置，nullptr使用默认配置
     * @return 0成功，负数失败
     */
    int create_strategy_instance(const char* strategy_id, const char* config = nullptr);
    
    /**
     * 启动指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int start_strategy_instance(const char* strategy_id);
    
    /**
     * 停止指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int stop_strategy_instance(const char* strategy_id);
    
    /**
     * 销毁指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int destroy_strategy_instance(const char* strategy_id);
    
    /**
     * 检查策略实例是否存在
     * @param strategy_id 策略ID
     * @return true存在，false不存在
     */
    bool strategy_instance_exists(const char* strategy_id) const;
    
    /**
     * 检查策略实例是否活跃
     * @param strategy_id 策略ID
     * @return true活跃，false未活跃
     */
    bool strategy_instance_is_active(const char* strategy_id) const;
    
    /**
     * 获取策略实例状态
     * @param strategy_id 策略ID
     * @return 状态值，-1表示策略不存在
     */
    int get_strategy_instance_status(const char* strategy_id) const;
    
    // ===== 批量策略操作 =====
    
    /**
     * 启动所有策略实例
     * @return 成功启动的策略数量
     */
    int start_all_strategy_instances();
    
    /**
     * 停止所有活跃策略实例
     * @return 成功停止的策略数量
     */
    int stop_all_strategy_instances();
    
    /**
     * 销毁所有策略实例
     * @return 成功销毁的策略数量
     */
    int destroy_all_strategy_instances();
    
    // ===== 状态查询接口 =====
    
    /**
     * 获取所有已注册策略ID列表
     * @return 策略ID列表
     */
    std::vector<std::string> get_registered_strategy_ids() const;
    
    /**
     * 获取所有活跃策略ID列表
     * @return 策略ID列表
     */
    std::vector<std::string> get_active_strategy_ids() const;
    
    /**
     * 获取活跃策略实例数量
     * @return 策略实例数量
     */
    size_t get_active_strategy_count() const;
    
    /**
     * 获取线程池状态信息
     * @return JSON格式的线程池状态
     */
    std::string get_thread_pool_status() const;
    
    /**
     * 获取策略适配器
     * @return 策略适配器指针
     */
    StrategyAdapter* get_strategy_adapter() const;
    
    // ===== 依赖服务管理 =====
    
    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器接口
     */
    void set_quote_parser(IQuoteParser* quote_parser);
    
    /**
     * 设置交易处理器
     * @param trade_handler 交易处理器接口
     */
    void set_trade_handler(ITradeHandler* trade_handler);

    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     */
    void set_db_manager(DatabaseManager* db_manager);

    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     */
    void set_trading_time_manager(TradingTimeManager* trading_time_manager);
    /**
     * 获取数据库管理器
     * @return 数据库管理器指针
     */
    DatabaseManager* get_db_manager() const;
    
    /**
     * 获取行情解析器接口
     * @return 行情解析器指针
     */
    IQuoteParser* get_quote_parser() const;
    
    /**
     * 获取交易处理器接口
     * @return 交易处理器指针
     */
    ITradeHandler* get_trade_handler() const;

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    TradingTimeManager* get_trading_time_manager() const;

    // ===== 状态查询接口 =====
    
    /**
     * 检查服务是否正在运行
     * @return true运行中，false已停止
     */
    bool is_running() const;
    
    /**
     * 检查服务是否已初始化
     * @return true已初始化，false未初始化
     */
    bool is_initialized() const;
    
    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     */
    const char* get_last_error() const;
    
    /**
     * 获取数据获取模式
     * @return 数据模式枚举
     */
    DataMode get_data_mode() const;

private:
    // ===== 私有成员 =====
    
    // 状态管理
    std::atomic<bool> _running;
    std::atomic<bool> _initialized;
    
    // 核心组件
    std::unique_ptr<StrategyAdapter> _strategy_adapter;
    IQuoteParser* _quote_parser;
    ITradeHandler* _trade_handler;
    DatabaseManager* _db_manager;
    TradingTimeManager* _trading_time_manager;
    
    // 数据处理模式
    std::atomic<bool> _quote_callback_mode;
    std::atomic<bool> _quote_polling_mode;
    std::atomic<bool> _trade_callback_mode;
    std::atomic<bool> _trade_polling_mode;
    
    // 轮询线程
    std::unique_ptr<std::thread> _quote_polling_thread;
    std::unique_ptr<std::thread> _trade_polling_thread;
    
    // 回调句柄
    IndexDataCallbackHandle _quote_index_handle;
    DepthDataCallbackHandle _quote_depth_handle;
    DepthFiftyDataCallbackHandle _quote_depth_fifty_handle;
    EntrustDataCallbackHandle _quote_entrust_handle;
    TradeDataCallbackHandle _quote_trade_handle;
    
    TradeOrderCallbackHandle _trade_order_rtn_handle{};     // 订单主推回调句柄
    TradeOrderQueryCallbackHandle _trade_order_query_handle{}; // 订单查询回调句柄
    TradeFillCallbackHandle _trade_fill_handle{};           // 成交回调句柄
    PositionCallbackHandle _trade_position_handle{};        // 持仓回调句柄
    AccountCallbackHandle _trade_account_handle{};          // 账户回调句柄
    TradeErrorCallbackHandle _trade_error_handle{};         // 错误回调句柄
    TradeOrderRspCallbackHandle _trade_order_rsp_handle{};  // 订单响应回调句柄
    TradeOrderCancelRspCallbackHandle _trade_cancel_rsp_handle{}; // 撤单响应回调句柄
    
    // 线程安全
    mutable std::shared_mutex _mutex;
    std::string _last_error;
    std::string _config_file;
    
    // ===== 私有方法 =====
    
    /**
     * 设置数据处理模式
     */
    void _setup_data_processing();
    
    /**
     * 加载配置文件
     * @param config_file 配置文件路径
     * @param config 配置JSON对象
     * @return true成功，false失败
     */
    bool _load_config(const char* config_file, nlohmann::json& config);
    
    /**
     * 设置行情回调模式
     * @return 0成功，负数失败
     */
    int _setup_quote_callback_mode();
    
    /**
     * 设置行情轮询模式
     * @return 0成功，负数失败
     */
    int _setup_quote_polling_mode();
    
    /**
     * 启动行情轮询线程
     */
    void _start_quote_polling_thread();
    
    /**
     * 停止行情轮询线程
     */
    void _stop_quote_polling_thread();
    
    /**
     * 行情轮询循环
     */
    void _quote_polling_loop();
    
    /**
     * 设置交易回调模式
     * @return 0成功，负数失败
     */
    int _setup_trade_callback_mode();
    
    /**
     * 设置交易轮询模式
     * @return 0成功，负数失败
     */
    int _setup_trade_polling_mode();
    
    /**
     * 启动交易轮询线程
     */
    void _start_trade_polling_thread();
    
    /**
     * 停止交易轮询线程
     */
    void _stop_trade_polling_thread();
    
    /**
     * 交易轮询循环
     */
    void _trade_polling_loop();
    
    /**
     * 清理行情回调句柄
     */
    void _clear_quote_callbacks();
    
    /**
     * 清理交易回调句柄
     */
    void _clear_trade_callbacks();
    
    /**
     * 清理所有回调句柄
     */
    void _clear_all_callbacks();

    /**
     * 设置错误信息
     * @param error 错误信息
     */
    void _set_error(const std::string& error);
    
    // ===== 静态回调函数（用于多策略分发） =====
    
    static void _on_index_data_callback(const IndexData* data);
    static void _on_depth_data_callback(const DepthData* data);
    static void _on_depth_fifty_data_callback(const DepthFiftyData* data);
    static void _on_entrust_data_callback(const EntrustData* data);
    static void _on_trade_data_callback(const TradeData* data);
    static void _on_order_rtn_callback(const TradeOrderData* data);
    static void _on_order_query_callback(const TradeOrderData* data);
    static void _on_account_data_callback(const AccountData* data);
    static void _on_fill_data_callback(const TradeFillData* data);
    static void _on_position_data_callback(const PositionData* data);
    static void _on_trade_error_callback(int32_t error_code, const char* error_msg, int64_t request_id);
    static void _on_order_rsp_callback(const TradeOrderRspData* data);
    static void _on_cancel_rsp_callback(const TradeOrderCancelRsqData* data);
};

NS_FT_END

#endif // STRATEGY_SERVICE_H