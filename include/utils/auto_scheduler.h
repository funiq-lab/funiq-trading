#ifndef AUTO_SCHEDULER_H
#define AUTO_SCHEDULER_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include "core/macros.h"
#include "utils/cron_scheduler.h"
#include "utils/trading_time.h"

NS_FT_BEGIN
/**
 * 自动启停调度器
 * 
 * 功能：
 * 1. 自动在交易时间开始前启动服务
 * 2. 自动在交易时间结束后停止服务
 * 3. 支持节假日跳过
 * 4. 支持手动启停控制
 * 5. 支持7*24小时模式
 */
class AutoScheduler
{
public:
    /**
     * 服务启动回调函数类型
     */
    using ServiceStartCallback = std::function<bool()>;
    
    /**
     * 服务停止回调函数类型
     */
    using ServiceStopCallback = std::function<void()>;

    /**
     * 构造函数
     * @param cron_scheduler cron调度器实例
     */
    explicit AutoScheduler(CronScheduler* cron_scheduler);
    
    /**
     * 析构函数
     */
    ~AutoScheduler();

    // ===== 依赖注入 =====
    
    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     * @note 必须在start_scheduler()之前设置
     */
    void set_trading_time_manager(TradingTimeManager* trading_time_manager);
    
    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    TradingTimeManager* get_trading_time_manager() const;

    // ===== 回调函数设置 =====
    
    /**
     * 设置服务启动回调函数
     * @param callback 启动回调函数
     */
    void set_start_callback(const ServiceStartCallback& callback);
    
    /**
     * 设置服务停止回调函数
     * @param callback 停止回调函数
     */
    void set_stop_callback(const ServiceStopCallback& callback);

    // ===== 调度器控制 =====
    
    /**
     * 启动自动调度器
     * @return 成功返回0，失败返回负数
     * @pre 必须先设置 TradingTimeManager
     */
    int start_scheduler();

    /**
     * 设置调度器间隔时间
     * @param interval_minutes 间隔时间（分钟）
     */
    void set_schedule_interval(int interval_minutes);

    /**
     * 获取调度器间隔时间
     * @return 间隔时间（分钟）
     */
    int get_schedule_interval() const;
    
    /**
     * 停止自动调度器
     */
    void stop_scheduler();
    
    /**
     * 检查调度器是否正在运行
     * @return true正在运行，false已停止
     */
    bool is_running() const;

    // ===== 手动控制 =====
    
    /**
     * 手动启动服务
     * @return 成功返回true，失败返回false
     */
    bool manual_start_services();
    
    /**
     * 手动停止服务
     */
    void manual_stop_services();

    // ===== 状态查询 =====
    
    /**
     * 获取当前是否为交易时间
     * @return true为交易时间，false为非交易时间
     */
    bool is_current_trading_time() const;
    
    /**
     * 获取下一个交易时间
     * @return 下一个交易时间点
     */
    std::chrono::system_clock::time_point get_next_trading_time() const;
    
    /**
     * 获取最后一次错误信息
     * @return 错误信息
     */
    const std::string& get_last_error() const;

private:
    CronScheduler* _cron_scheduler;
    TradingTimeManager* _trading_time_manager = nullptr;
    int _schedule_interval_minutes = 5;
    std::atomic<bool> _starting{false};
    std::atomic<bool> _stopping{false};

    std::string _last_error;
    
    ServiceStartCallback _start_callback;
    ServiceStopCallback _stop_callback;
    
    std::atomic<bool> _scheduler_running{false};
    std::atomic<bool> _auto_mode{true};  // 自动模式开关
    
    mutable std::mutex _mutex;
    
    // cron任务ID列表
    std::vector<int> _trading_cron_jobs;  // 交易时间相关的任务ID
    
    // ===== 内部方法 =====
    
    /**
     * 设置定时任务
     * @return 成功返回0，失败返回负数
     */
    int _setup_cron_jobs();
    
    /**
     * 清理定时任务
     */
    void _cleanup_cron_jobs();
    
    /**
     * 自动启动服务（由cron任务调用）
     */
    void _auto_start_services();
    
    /**
     * 自动停止服务（由cron任务调用）
     */
    void _auto_stop_services();
    
    /**
     * 检查是否应该启动服务（初始化时调用）
     */
    void _check_should_start_services();
    
    /**
     * 创建 cron 表达式
     * @param hour 小时
     * @param minute 分钟
     * @param trading_weekdays 交易日列表
     * @return cron 表达式字符串
     */
    std::string _create_cron_expression(int hour, int minute, const std::vector<int>& trading_weekdays);
    
    /**
     * 设置错误信息
     * @param error 错误信息
     */
    void _set_error(const std::string& error);

    /**
     * 检查是否存在时间重叠
     * @param sessions 交易时间列表
     */
    void _check_schedule_overlaps(const std::vector<TradingSession>& sessions);
};

NS_FT_END

#endif // AUTO_SCHEDULER_H