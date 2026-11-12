#ifndef BASE_STRATEGY_H
#define BASE_STRATEGY_H

#include "types/strategy_types.h"
#include "database/database_manager.hpp"
#include "core/macros.h"
#include "utils/performance_stats_collector.h"
#include <string>
#include <chrono>
#include <memory>
#include <mutex>

NS_FT_BEGIN
// 前向声明
class IQuoteParser;
class ITradeHandler;
class TradingTimeManager;
class TwapAlgorithm;

// 数据库相关前向声明
namespace database
{
    class DatabaseManager;
}

/**
 * 策略基类接口
 *
 * 所有具体策略都必须继承此基类并实现相应接口。提供两大核心功能：
 * 1. 数据监听：统一的行情和交易数据监听接口
 * 2. 交易逻辑：标准的买卖和撤单操作接口
 *
 * 注意：
 * - 银证转账等基础功能在具体的服务层实现，策略层不处理
 * - 所有策略不会自动运行，需要通过StrategyService明确启动
 * - 策略实例由StrategyAdapter统一管理
 * - 此类只定义接口，具体实现由子类完成
 */
class BaseStrategy
{
public:
    /**
     * 虚析构函数
     */
    virtual ~BaseStrategy() = default;

    /**
     * 初始化策略
     */
    virtual int init(const char *config) = 0;

    /**
     * 启动策略
     */
    virtual int start() = 0;

    /**
     * 停止策略
     */
    virtual int stop() = 0;

    /**
     * 清理策略资源
     */
    virtual void release() = 0;

    /**
     * 获取策略ID
     */
    virtual const char *set_strategy_id(const char *strategy_id) = 0;

    /**
     * 获取策略ID
     */
    virtual const char *get_strategy_id() const = 0;

    /**
     * 获取策略状态
     */
    virtual StrategyState get_state() const = 0;

    /**
     * 获取最后错误信息
     */
    virtual const char *get_last_error() const = 0;

    // ====== 用于配置模式的策略服务 ======
    /**
     * 更新策略配置
     */
    virtual int update_config(const char *config) { return 0; };

    /**
     * 获取策略配置
     */
    virtual const char *get_config() const { return "{}"; };

    // ====== 用于挂单模式的策略服务 ======
    /**
     * 新增挂单
     */
    virtual int add_order_queue(const char *order) { return 0; };

    /**
     * 更新挂单
     */
    virtual int update_order_queue(const int64_t id, const char *order) { return 0; };

    /**
     * 删除挂单
     */
    virtual int remove_order_queue(const int64_t id) { return 0; };

    /**
     * 取消挂单
     */
    virtual int cancel_order_queue(const int64_t id) { return 0; };

    /**
     * 批量新增挂单
     */
    virtual int batch_add_order_queue(const char *orders) { return 0; };

    /**
     * 清空挂单
     */
    virtual int remove_all_active_order_queue() { return 0; };

    /**
     * 取消所有挂单
     */
    virtual int cancel_all_active_order_queue() { return 0; };

    /**
     * 获取当前的挂单队列
     */
    virtual const char *get_order_queue() { return "[]"; };

    // ===== 策略数据暴露接口 =====

    /**
     * 获取策略运行时数据（JSON格式）
     * @param data_type 数据类型（如 "orders", "positions", "metrics", "status"）
     * @return JSON字符串，NULL表示不支持该数据类型
     */
    virtual const char *get_strategy_data(const char *data_type) const { return nullptr; }

    /**
     * 获取所有可用的数据类型列表
     * @return JSON数组，包含所有支持的data_type
     */
    virtual const char *get_available_data_types() const
    {
        return R"(["status", "config"])"; // 默认只提供基础数据
    }

    // ===== 策略数据变化回调接口 =====

    /**
     * 设置策略数据变化回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在策略数据变化时执行，应避免阻塞操作
     */
    virtual StrategyDataCallbackHandle set_data_change_callback(StrategyDataCallbackType callback)
    {
        return StrategyDataCallbackHandle{};
    }

    /**
     * 移除策略数据变化回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_data_change_callback(StrategyDataCallbackHandle handle)
    {
        return -1;
    }

    // ===== 数据监听接口 =====

    /**
     * 行情数据监听 - 指数数据
     * @param data 指数数据
     */
    virtual void on_index_data(const IndexData *data) {}

    /**
     * 行情数据监听 - 深度数据
     * @param data 深度数据
     */
    virtual void on_depth_data(const DepthData *data) {}

    /**
     * 行情数据监听 - 50笔行情
     * @param data 深度数据
     */
    virtual void on_depth_fifty_data(const DepthFiftyData *data) {}

    /**
     * 行情数据监听 - 委托数据
     * @param data 委托数据
     */
    virtual void on_entrust_data(const EntrustData *data) {}

    /**
     * 行情数据监听 - 成交数据
     * @param data 成交数据
     */
    virtual void on_trade_data(const TradeData *data) {}

    /**
     * 交易数据监听 - 订单回报
     * @param data 订单数据
     */
    virtual void on_order_rtn_data(const TradeOrderData *data) {}

    /**
     * 交易数据监听 - 订单查询
     * @param data 订单数据
     */
    virtual void on_order_query_data(const TradeOrderData *data) {}

    /**
     * 交易数据监听 - 成交回报
     * @param data 成交数据
     */
    virtual void on_fill_data(const TradeFillData *data) {}

    /**
     * 交易数据监听 - 持仓变化
     * @param data 持仓数据
     */
    virtual void on_position_data(const PositionData *data) {}

    /**
     * 交易数据监听 - 账户变化
     * @param data 账户数据
     */
    virtual void on_account_data(const AccountData *data) {}

    /**
     * 交易数据监听 - 交易错误
     * @param error_code 错误代码
     * @param error_msg 错误信息
     */
    virtual void on_trade_error(int error_code, const char *error_msg) {}

    /**
     * 交易数据监听 - 委托响应
     * @param data 委托响应数据
     */
    virtual void on_order_rsp_data(const TradeOrderRspData *data) {}

    /**
     * 交易数据监听 - 撤单响应
     * @param data 撤单响应数据
     */
    virtual void on_cancel_rsp_data(const TradeOrderCancelRsqData *data) {}

protected:
    /**
     * 买入下单
     * @param account 账户信息
     * @param security 证券信息
     * @param price 价格
     * @param quantity 数量
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    virtual int64_t buy_order(const AccountInfo *account, const SecurityInfo *security,
                              double price, int64_t quantity) { return 0; };

    /**
     * 卖出下单
     * @param account 账户信息
     * @param security 证券信息
     * @param price 价格
     * @param quantity 数量
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    virtual int64_t sell_order(const AccountInfo *account, const SecurityInfo *security,
                               double price, int64_t quantity) { return 0; };

    /**
     * 撤单
     * @param account 账户信息
     * @param orig_order_id 原订单ID
     * @param orig_batch_id 原批次ID
     * @param market 市场类型
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    virtual int64_t cancel_order(const AccountInfo *account, int64_t orig_order_id,
                                 uint64_t orig_batch_id, MarketType market) { return 0; };

    /**
     * 获取行情解析器（由StrategyAdapter注入）
     * @return 行情解析器接口指针
     */
    virtual IQuoteParser *get_quote_parser() const = 0;

    /**
     * 获取交易处理器（由StrategyAdapter注入）
     * @return 交易处理器接口指针
     */
    virtual ITradeHandler *get_trade_handler() const = 0;

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器接口
     */
    virtual void set_quote_parser(IQuoteParser *quote_parser) = 0;

    /**
     * 设置交易处理器
     * @param trade_handler 交易处理器接口
     */
    virtual void set_trade_handler(ITradeHandler *trade_handler) = 0;

    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     * @note 在策略初始化后调用，用于策略数据持久化
     */
    virtual void set_db_manager(DatabaseManager *db_manager) = 0;

    /**
     * 获取数据库管理器
     * @return 数据库管理器指针，未设置时返回nullptr
     */
    virtual DatabaseManager *get_db_manager() const = 0;

    /**
     * 子类调用此方法通知数据变化
     * @param data_type 数据类型
     * @param data JSON数据
     */
    virtual void notify_strategy_data_change(const char *data_type, const char *data) {}

    /**
     * 设置交易时间管理器（由StrategyAdapter注入）
     * @param trading_time_manager 交易时间管理器指针
     */
    virtual void set_trading_time_manager(TradingTimeManager *trading_time_manager) {};

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    virtual TradingTimeManager *get_trading_time_manager() const { return nullptr; };

    // ===== 性能测试辅助工具 =====

    /**
     * 性能测试阶段标记点
     */
    class PerformanceStageMarker
    {
    public:
        PerformanceStageMarker(BaseStrategy *strategy, const std::string &operation_name)
            : _strategy(strategy), _operation_name(operation_name),
              _start_time(std::chrono::steady_clock::now()),
              _last_stage_time(_start_time) {}

        ~PerformanceStageMarker()
        {
            // 析构时释放当前统计的阶段信息
            _strategy->record_end(_operation_name);
        }

        /**
         * 计算当前阶段和总耗时
         * @return 包含本阶段耗时（纳秒）和总耗时（纳秒）的pair
         */
        std::pair<int64_t, int64_t> calculate_time(const std::chrono::steady_clock::time_point &now)
        {
            auto stage_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     now - _last_stage_time)
                                     .count();
            auto total_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     now - _start_time)
                                     .count();
            return std::make_pair(stage_elapsed, total_elapsed);
        }

        void mark_total()
        {
             if (_strategy && _strategy->is_performance_logging_enabled())
            {
                auto now = std::chrono::steady_clock::now();
                auto [stage_elapsed, total_elapsed] = calculate_time(now);
                _strategy->record_total(_operation_name, stage_elapsed, total_elapsed);
                _last_stage_time = now;
            }
        }

        // 标记一个阶段的完成
        void mark_stage(const std::string &stage_name)
        {

            if (_strategy && _strategy->is_performance_logging_enabled())
            {
                auto now = std::chrono::steady_clock::now();
                auto [stage_elapsed, total_elapsed] = calculate_time(now);
                _strategy->record_performance_stage(_operation_name, stage_name, stage_elapsed, total_elapsed);
                _last_stage_time = now;
            }
        }

    private:
        BaseStrategy *_strategy;
        std::string _operation_name;
        std::chrono::steady_clock::time_point _start_time;
        std::chrono::steady_clock::time_point _last_stage_time;
    };

    /**
     * 启用性能日志
     * @param sample_count 样本数量
     * @param log_prefix 日志前缀
     */
    void enable_performance_logging(int sample_count, const std::string &log_prefix)
    {
        _perf_enabled = true;
        _perf_log_prefix = log_prefix;
        _perf_collector = std::make_unique<funiq_trading::PerformanceStatsCollector>(sample_count);
    }

    /**
     * 检查是否启用性能日志
     */
    bool is_performance_logging_enabled() const
    {
        return _perf_enabled;
    }

    /**
     * 记录性能阶段数据
     * @param operation_name 操作名称
     * @param stage_name 阶段名称
     * @param stage_elapsed_ns 本阶段耗时（纳秒）
     * @param total_elapsed_ns 从操作开始到现在的总耗时（纳秒）
     */
    void record_performance_stage(const std::string &operation_name,
                                  const std::string &stage_name,
                                  int64_t stage_elapsed_ns,
                                  int64_t total_elapsed_ns)
    {
        if (_perf_enabled && _perf_collector)
        {
            std::lock_guard<std::mutex> lock(_perf_mutex);
            _perf_collector->record_stage(operation_name, stage_name,
                                          stage_elapsed_ns, total_elapsed_ns,
                                          _perf_log_prefix);
        }
    }

    void record_total(const std::string &operation_name,
                      int64_t stage_elapsed_ns,
                      int64_t total_elapsed_ns)
    {
        if (_perf_enabled && _perf_collector)
        {
            std::lock_guard<std::mutex> lock(_perf_mutex);
            _perf_collector->record_total(operation_name,
                                          stage_elapsed_ns, total_elapsed_ns,
                                          _perf_log_prefix);
        }
    }

    void record_end(const std::string &operation_name)
    {
        if (_perf_enabled && _perf_collector)
        {
            std::lock_guard<std::mutex> lock(_perf_mutex);
            _perf_collector->record_end(operation_name);
        }
    }

private:
    // 性能统计相关
    bool _perf_enabled = false;
    std::string _perf_log_prefix = "[BaseStrategy]";
    std::unique_ptr<funiq_trading::PerformanceStatsCollector> _perf_collector;
    mutable std::mutex _perf_mutex;

    friend class StrategyAdapter;
};

NS_FT_END

#endif // BASE_STRATEGY_H