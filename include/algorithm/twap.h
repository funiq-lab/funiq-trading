#ifndef TWAP_H
#define TWAP_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <queue>
#include <thread>

#include "core/macros.h"
#include "types/trade_types.h"
#include "types/base_types.h"
#include "utils/BS_thread_pool.hpp"
#include <nlohmann/json.hpp>

NS_FT_BEGIN

// 前向声明
class ITradeHandler;
class IQuoteParser;
class TradingTimeManager;

/**
 * TWAP任务状态枚举
 */
enum class TwapTaskStatus {
    CREATED = 0,     // 已创建
    RUNNING = 1,     // 运行中
    COMPLETED = 2,   // 已完成
    CANCELLED = 3,   // 已取消
    ERROR = 4        // 错误状态
};

/**
 * TWAP拆单片段
 */
struct TwapSlice
{
    int64_t quantity;                                          // 数量
    std::chrono::system_clock::time_point execution_time;      // 执行时间
    int64_t actual_order_id = 0;                              // 实际订单ID
    bool is_executed = false;                                 // 是否已执行成功
    bool is_processed = false;                                // 是否已处理（成功或失败）
    std::string error_msg;                                    // 错误信息

    TwapSlice(int64_t qty, std::chrono::system_clock::time_point exec_time)
        : quantity(qty), execution_time(exec_time) {}
};

/**
 * TWAP拆单任务
 */
struct TwapTask
{
    uint64_t task_id;                                         // 任务ID（64位整数）
    
    // 账户信息（从函数参数传入）
    AccountInfo account;                                      // 账户信息
    
    // 交易信息
    std::string security_code;                                // 股票代码
    MarketType market;                                        // 市场类型
    int64_t total_quantity;                                   // 总股数
    std::chrono::seconds interval;                            // 拆单间隔
    std::chrono::system_clock::time_point start_time;         // 开始时间
    std::chrono::system_clock::time_point end_time;           // 结束时间
    std::chrono::system_clock::time_point create_time;        // 创建时间
    
    // 执行状态
    TwapTaskStatus status;                                    // 任务状态
    std::vector<TwapSlice> slices;                            // 拆单片段
    int submitted_count = 0;                                  // 已提交的订单数量
    
    // 价格阈值相关
    bool enable_price_threshold = false;                      // 是否启用价格阈值
    double price_threshold_ratio = 0.05;                     // 价格阈值比例（默认5%）
    double initial_price = 0.0;                              // 初始价格
    bool initial_price_set = false;                          // 初始价格是否已设置
    
    std::string error_msg;                                    // 错误信息

    TwapTask() : status(TwapTaskStatus::CREATED) {}

    /**
     * 获取总切片数量
     */
    size_t get_total_slice_count() const {
        return slices.size();
    }

    /**
     * 获取已处理的切片数量（包括成功和失败的）
     */
    size_t get_processed_slice_count() const {
        size_t count = 0;
        for (const auto& slice : slices) {
            if (slice.is_processed) {
                count++;
            }
        }
        return count;
    }

    /**
     * 获取执行成功的切片数量
     */
    size_t get_executed_slice_count() const {
        size_t count = 0;
        for (const auto& slice : slices) {
            if (slice.is_executed) {
                count++;
            }
        }
        return count;
    }

    /**
     * 获取交易时长
     */
    std::chrono::seconds get_trading_duration() const;

    /**
     * 转换为JSON格式
     */
    nlohmann::json to_json() const;
};

/**
 * TWAP拆单算法管理器
 *
 * 功能：
 * 1. 创建拆单任务：根据参数计算并创建基于内存的拆单任务
 * 2. 任务管理：维护所有任务历史和当前活跃任务
 * 3. 定时执行：根据间隔时间自动执行拆单
 * 4. 状态查询：根据任务ID查询拆单任务状态
 * 5. 价格阈值检查：可选的价格保护机制
 * 6. 定时清理：每天下午4点自动清理所有任务
 *
 * 使用方式：
 * 1. 创建TwapAlgorithm实例
 * 2. 调用init()初始化算法
 * 3. 设置依赖服务set_quote_parser(), set_trade_handler()
 * 4. 调用start()启动算法服务
 * 5. 调用create_twap_sell_task()创建拆单任务
 * 6. 程序结束时调用stop()和release()
 *
 * 注意：
 * - 实例应该在服务启动时创建并在需要的地方通过依赖注入传递
 * - 确保在所有使用方停止后再销毁实例
 * - 线程安全：所有公开接口都是线程安全的
 * - 任务历史：保留所有任务历史直到每日清理
 */
class TwapAlgorithm
{
public:
    /**
     * 构造函数
     */
    TwapAlgorithm();

    /**
     * 析构函数
     * @note 会自动调用release()清理资源
     */
    ~TwapAlgorithm();

    // 禁用拷贝构造和赋值操作
    TwapAlgorithm(const TwapAlgorithm &) = delete;
    TwapAlgorithm &operator=(const TwapAlgorithm &) = delete;

    // ===== 生命周期管理 =====

    /**
     * 初始化算法
     * @return 0成功，负数失败
     */
    int init();

    /**
     * 启动算法服务
     * @return 0成功，负数失败
     * @pre 必须先调用init()并设置依赖服务
     */
    int start();

    /**
     * 停止算法服务
     * @return 0成功，负数失败
     * @note 停止后可以重新调用start()恢复服务
     */
    int stop();

    /**
     * 释放资源
     * @note 释放所有资源，停止所有线程，清理所有任务
     */
    void release();

    // ===== 依赖注入 =====

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器指针
     * @note 必须在start()之前设置
     */
    void set_quote_parser(IQuoteParser *quote_parser);

    /**
     * 设置交易处理器
     * @param trade_handler 交易处理器指针
     * @note 必须在start()之前设置
     */
    void set_trade_handler(ITradeHandler *trade_handler);

    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     * @note 必须在start()之前设置
     */
    void set_trading_time_manager(TradingTimeManager *trading_time_manager);

    /**
     * 获取行情解析器
     * @return 行情解析器指针
     */
    IQuoteParser *get_quote_parser() const;

    /**
     * 获取交易处理器
     * @return 交易处理器指针
     */
    ITradeHandler *get_trade_handler() const;

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    TradingTimeManager *get_trading_time_manager() const;
    // ===== 状态查询 =====

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     */
    bool is_initialized() const;

    /**
     * 检查是否正在运行
     * @return true运行中，false已停止
     */
    bool is_running() const;

    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     */
    std::string get_last_error() const;

    // ===== TWAP拆单接口 =====

    /**
     * 创建TWAP拆单任务（基于交易时间的智能拆单）
     * @param account 账户信息
     * @param security_code 股票代码
     * @param market 市场类型
     * @param total_quantity 总股数
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @param interval_seconds 拆单间隔（秒，可选，默认10秒）
     * @param enable_price_threshold 是否启用价格阈值检查（可选，默认false）
     * @param price_threshold_ratio 价格阈值比例（可选，默认0.05即5%）
     * @return 任务ID（64位整数），失败返回0
     * @pre 必须先调用start()启动服务并设置TradingTimeManager
     * @note 线程安全
     * @note 程序会自动计算start_time到end_time之间的有效交易时间，并根据interval_seconds计算拆单数量
     * @note 拆单数量 = 有效交易时长 / interval_seconds，每单股数 = total_quantity / 拆单数量
     * @note 只在交易时间内执行拆单，非交易时间自动跳过
     */
    uint64_t create_twap_sell_task(const AccountInfo &account,
                                   const std::string &security_code,
                                   MarketType market,
                                   int64_t total_quantity,
                                   const std::chrono::system_clock::time_point &start_time,
                                   const std::chrono::system_clock::time_point &end_time,
                                   int interval_seconds = 10,
                                   bool enable_price_threshold = false,
                                   double price_threshold_ratio = 0.05);

    /**
     * 取消TWAP拆单任务
     * @param task_id 任务ID
     * @return 成功返回0，失败返回错误码
     * @note 线程安全
     */
    int cancel_twap_task(uint64_t task_id);

    /**
     * 获取TWAP任务状态
     * @param task_id 任务ID
     * @return JSON格式的任务状态，失败返回空字符串
     * @note 线程安全，可查询所有任务（包括已完成的）
     */
    std::string get_twap_task_status(uint64_t task_id);

    /**
     * 获取所有任务列表
     * @return JSON格式的任务列表
     * @note 线程安全，包括所有状态的任务
     */
    std::string get_all_tasks();

    /**
     * 获取正在运行的任务列表
     * @return JSON格式的任务列表
     * @note 线程安全，只包括RUNNING状态的任务
     */
    std::string get_running_tasks();

    /**
     * 获取指定状态的任务列表
     * @param status 任务状态
     * @return JSON格式的任务列表
     * @note 线程安全
     */
    std::string get_tasks_by_status(TwapTaskStatus status);

    /**
     * 获取任务统计信息
     * @return JSON格式的统计信息
     * @note 线程安全
     */
    std::string get_task_statistics();

    /**
     * 手动清理所有任务
     * @return 清理的任务数量
     * @note 线程安全，清理所有任务历史
     */
    int clear_all_tasks();

    /**
     * 清理指定状态的任务
     * @param status 要清理的任务状态
     * @return 清理的任务数量
     * @note 线程安全
     */
    int clear_tasks_by_status(TwapTaskStatus status);

    /**
     * 清理指定时间之前的任务
     * @param before_time 时间点
     * @return 清理的任务数量
     * @note 线程安全
     */
    int clear_tasks_before(const std::chrono::system_clock::time_point &before_time);

    /**
     * 将任务ID转换为可读字符串（用于日志）
     * @param task_id 任务ID
     * @return 格式化字符串，如 "12345678_001"
     */
    static std::string format_task_id(uint64_t task_id);

private:
    // ===== 状态管理 =====
    std::atomic<bool> _initialized;
    std::atomic<bool> _running;
    std::string _last_error;

    // 依赖服务
    IQuoteParser *_quote_parser = nullptr;               // 行情解析器
    ITradeHandler *_trade_handler = nullptr;             // 交易处理器
    TradingTimeManager *_trading_time_manager = nullptr; // 交易时间管理器

    // 线程安全保护
    mutable std::recursive_mutex _mutex;

    // TWAP任务管理
    std::unordered_map<uint64_t, TwapTask> _all_tasks;       // 使用整数key
    std::unordered_map<uint64_t, TwapTask *> _running_tasks; // 使用整数key

    // 执行线程
    std::unique_ptr<std::thread> _execution_thread;
    std::atomic<bool> _should_stop_execution;

    // 定时清理线程
    std::unique_ptr<std::thread> _cleanup_thread;
    std::atomic<bool> _should_stop_cleanup;

    // 订单跟踪
    std::unordered_map<int64_t, uint64_t> _order_to_task_map; // order_id -> task_id

    // ===== 优化的任务调度 =====

    struct TaskExecution
    {
        uint64_t task_id; // 改为整数
        size_t slice_index;
        std::chrono::system_clock::time_point execution_time;

        bool operator>(const TaskExecution &other) const
        {
            return execution_time > other.execution_time;
        }
    };

    // 执行队列（按时间排序）
    std::priority_queue<TaskExecution, std::vector<TaskExecution>,
                        std::greater<TaskExecution>>
        _execution_queue;

    // 线程池
    std::unique_ptr<BS::thread_pool<>> _thread_pool;

    // 同步原语
    std::condition_variable _execution_cv;
    std::mutex _execution_mutex;
    std::condition_variable _cleanup_cv;
    std::mutex _cleanup_mutex;

    // 性能控制
    std::atomic<int> _concurrent_executions{0};
    const int MAX_CONCURRENT_EXECUTIONS = 10;
    const int MAX_TOTAL_TASKS = 10000;  // 最大总任务数
    const int MAX_RUNNING_TASKS = 1000; // 最大运行任务数

    // 统计信息
    std::atomic<uint64_t> _total_tasks_created{0};
    std::atomic<uint64_t> _total_tasks_running{0};
    std::atomic<uint64_t> _total_tasks_completed{0};
    std::atomic<uint64_t> _total_tasks_cancelled{0};
    std::atomic<uint64_t> _total_tasks_error{0};
    std::atomic<uint64_t> _total_slices_executed{0};
    std::atomic<uint64_t> _total_slices_error{0};

    // 清理配置
    const int DAILY_CLEANUP_HOUR = 16;  // 每天下午4点清理
    const int DAILY_CLEANUP_MINUTE = 0; // 0分钟

    // 任务ID生成器（新增）
    std::atomic<uint32_t> _task_sequence{0}; // 递增序列

    // ===== 私有方法 =====

    /**
     * 执行线程主循环
     */
    void _execution_loop();

    /**
     * 定时清理线程主循环
     */
    void _cleanup_loop();

    /**
     * 调度拆单片段执行
     */
    void _schedule_slice_execution(uint64_t task_id,
                                   size_t slice_index,
                                   std::chrono::system_clock::time_point exec_time);

    /**
     * 批量处理到期的执行任务
     */
    void _process_due_executions();

    /**
     * 异步执行单个拆单片段
     */
    void _async_execute_slice(uint64_t task_id, size_t slice_index);

    /**
     * 更新任务状态
     * @param task_id 任务ID
     * @param new_status 新状态
     * @note 内部方法，调用者需要持有锁
     */
    void _update_task_status(uint64_t task_id, TwapTaskStatus new_status);

    /**
     * 检查是否需要每日清理
     * @return true需要清理，false不需要
     */
    bool _should_daily_cleanup();

    /**
     * 计算下次清理时间
     * @return 下次清理的时间点
     */
    std::chrono::system_clock::time_point _calculate_next_cleanup_time();

    /**
     * 执行每日清理
     */
    void _perform_daily_cleanup();

    /**
     * 智能清理已完成的任务
     * @note 定期清理，避免内存无限增长
     */
    void _smart_cleanup();

    /**
     * 检查价格阈值，如果价格下跌超过阈值则停止任务
     * @param task_id 任务ID
     * @param current_price 当前价格
     * @return true表示价格正常，false表示触发阈值需要停止任务
     */
    bool _check_price_threshold(uint64_t task_id, double current_price);

    /**
     * 获取股票的实时价格
     * @param security_code 股票代码
     * @param market 市场类型
     * @return 实时价格，失败返回0.0
     */
    double _get_real_time_price(const std::string &security_code, MarketType market);

    /**
     * 生成任务ID（基于时间戳+递增序列）
     * 高32位：Unix时间戳（秒）
     * 低32位：递增序列号
     * @return 64位唯一任务ID
     */
    uint64_t _generate_task_id();

    /**
     * 从任务ID提取时间戳
     * @param task_id 任务ID
     * @return Unix时间戳（秒）
     */
    static uint32_t _extract_timestamp(uint64_t task_id);

    /**
     * 从任务ID提取序列号
     * @param task_id 任务ID
     * @return 序列号
     */
    static uint32_t _extract_sequence(uint64_t task_id);

    /**
     * 计算TWAP拆单片段（基于交易时间）
     * @param task 任务对象
     * @return 成功返回0，失败返回负数
     */
    int _calculate_twap_slices(TwapTask &task);

    /**
     * 检查是否可以执行下一个拆单
     * @param task 任务对象
     * @return true可以执行，false需要等待
     */
    bool _can_execute_next_slice(const TwapTask &task) const;

    /**
     * 调整执行时间到下一个交易时间
     * @param time_point 原时间点
     * @return 调整后的交易时间点
     */
    std::chrono::system_clock::time_point _adjust_to_next_trading_time(
        const std::chrono::system_clock::time_point &time_point) const;

    /**
     * 设置错误信息
     */
    void _set_error(const std::string &error);

    /**
     * 执行卖出订单
     */
    int64_t _execute_sell_order(const TwapTask &task, const TwapSlice &slice);

    /**
     * 时间转换工具
     */
    std::string _format_time_point(const std::chrono::system_clock::time_point &tp) const;
};

NS_FT_END

#endif // TWAP_H