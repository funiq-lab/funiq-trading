#include "utils/auto_scheduler.h"
#include "utils/trading_time.h"
#include "logger/logger.hpp"
#include <sstream>
#include <iomanip>
#include <atomic>
#include <thread>

NS_FT_BEGIN

AutoScheduler::AutoScheduler(CronScheduler* cron_scheduler)
    : _cron_scheduler(cron_scheduler)
{
    if (!_cron_scheduler) {
        _set_error("CronScheduler pointer is null");
        return;
    }
    
    LOG_INFO("AutoScheduler constructed (TradingTimeManager will be injected)");
}

AutoScheduler::~AutoScheduler()
{
    stop_scheduler();
    LOG_INFO("AutoScheduler destructed");
}

// ===== 依赖注入 =====

void AutoScheduler::set_trading_time_manager(TradingTimeManager* trading_time_manager)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _trading_time_manager = trading_time_manager;
    
    if (_trading_time_manager) {
        // 记录配置信息
        const auto& config = _trading_time_manager->get_config();
        if (config.is_24x7) {
            LOG_INFO("AutoScheduler: TradingTimeManager set in 7*24 hour mode");
        } else {
            LOG_INFO("AutoScheduler: TradingTimeManager set with {} trading sessions, {} holidays", 
                     config.trading_sessions.size(), config.holidays.size());
            
            for (size_t i = 0; i < config.trading_sessions.size(); ++i) {
                const auto& session = config.trading_sessions[i];
                LOG_INFO("Session {}: {:02d}:{:02d} - {:02d}:{:02d}",
                         i + 1, session.start_hour, session.start_minute,
                         session.end_hour, session.end_minute);
            }
        }
    } else {
        LOG_WARN("AutoScheduler: TradingTimeManager set to null");
    }
}

TradingTimeManager* AutoScheduler::get_trading_time_manager() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _trading_time_manager;
}

// ===== 回调函数设置 =====

void AutoScheduler::set_start_callback(const ServiceStartCallback& callback)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _start_callback = callback;
}

void AutoScheduler::set_stop_callback(const ServiceStopCallback& callback)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stop_callback = callback;
}

// ===== 调度器控制 =====

int AutoScheduler::start_scheduler()
{
    if (_scheduler_running.load()) {
        LOG_WARN("AutoScheduler is already running");
        return 0;
    }
    
    if (!_trading_time_manager) {
        _set_error("TradingTimeManager not set - must call set_trading_time_manager() first");
        return -1;
    }
    
    if (!_cron_scheduler || !_cron_scheduler->is_running()) {
        _set_error("CronScheduler is not running");
        return -1;
    }
    
    // 设置定时任务
    if (_setup_cron_jobs() != 0) {
        return -1;
    }
    
    _scheduler_running.store(true);
    LOG_INFO("AutoScheduler started successfully");
    
    // 立即检查一次是否应该启动服务
    _check_should_start_services();
    
    return 0;
}

void AutoScheduler::stop_scheduler()
{
    // 简化停止逻辑
    if (!_scheduler_running.load()) {
        return;
    }
    
    _scheduler_running.store(false);
    
    // 清理定时任务
    for (int job_id : _trading_cron_jobs) {
        if (job_id > 0) {
            _cron_scheduler->remove_job(job_id);
        }
    }
    _trading_cron_jobs.clear();
    
    LOG_INFO("AutoScheduler stopped");
}

bool AutoScheduler::is_running() const
{
    return _scheduler_running.load();
}

// ===== 手动控制 =====

bool AutoScheduler::manual_start_services()
{
    if (!_start_callback) {
        _set_error("Start callback not set");
        return false;
    }
    
    LOG_INFO("Manual start services requested");
    
    // 暂时关闭自动模式，避免冲突
    _auto_mode.store(false);
    
    bool success = _start_callback();
    if (success) {
        LOG_INFO("Services started successfully (manual mode)");
    } else {
        LOG_ERROR("Failed to start services (manual mode)");
        _auto_mode.store(true);  // 恢复自动模式
    }
    
    return success;
}

void AutoScheduler::manual_stop_services()
{
    LOG_INFO("Manual stop services requested");
    
    if (_stop_callback) {
        _stop_callback();
    }
    
    _auto_mode.store(true);  // 恢复自动模式
    
    LOG_INFO("Services stopped (manual mode)");
}

// ===== 状态查询 =====

bool AutoScheduler::is_current_trading_time() const
{
    if (!_trading_time_manager) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    return _trading_time_manager->is_trading_time(now);
}

std::chrono::system_clock::time_point AutoScheduler::get_next_trading_time() const
{
    if (!_trading_time_manager) {
        return std::chrono::system_clock::now();
    }
    
    auto now = std::chrono::system_clock::now();
    return _trading_time_manager->get_next_trading_time(now);
}

const std::string& AutoScheduler::get_last_error() const
{
    return _last_error;
}

void AutoScheduler::set_schedule_interval(int interval_minutes)
{
    _schedule_interval_minutes = interval_minutes;
}

int AutoScheduler::get_schedule_interval() const
{
    return _schedule_interval_minutes;
}

// ===== 内部方法 =====

int AutoScheduler::_setup_cron_jobs()
{
    // 清理现有的任务
    _cleanup_cron_jobs();
    
    if (!_trading_time_manager) {
        _set_error("TradingTimeManager not initialized");
        return -1;
    }
    
    const auto& config = _trading_time_manager->get_config();
    
    // 如果是7*24小时模式，不需要设置定时任务
    if (config.is_24x7) {
        LOG_INFO("7*24 hour mode enabled, no cron jobs needed");
        return 0;
    }
    
    // 如果没有交易时间段配置，也不需要设置定时任务
    if (config.trading_sessions.empty()) {
        LOG_WARN("No trading sessions configured, no cron jobs will be set");
        return 0;
    }
    
    // 检测时间重叠
    _check_schedule_overlaps(config.trading_sessions);
    
    // 原有的创建 cron 任务逻辑保持不变
    int job_count = 0;
    for (const auto& session : config.trading_sessions) {
        // 计算启动时间
        int start_hour = session.start_hour;
        int start_minute = session.start_minute - _schedule_interval_minutes;
        
        // 处理负数分钟
        if (start_minute < 0) {
            start_minute += 60;
            start_hour -= 1;
            if (start_hour < 0) {
                start_hour = 23;  // 跨天的情况
            }
        }
        
        // 计算停止时间
        int end_hour = session.end_hour;
        int end_minute = session.end_minute + _schedule_interval_minutes;
        
        // 处理超过60分钟的情况
        if (end_minute >= 60) {
            end_minute -= 60;
            end_hour += 1;
            if (end_hour >= 24) {
                end_hour = 0;  // 跨天的情况
            }
        }
        
        // 创建启动任务
        std::string start_cron = _create_cron_expression(start_hour, start_minute, config.trading_weekdays);
        std::string start_job_name = "auto_start_" + std::to_string(job_count);
        
        int start_job_id = _cron_scheduler->add_job(
            start_job_name,
            start_cron,
            [this]() { _auto_start_services(); }
        );
        
        if (start_job_id <= 0) {
            _set_error("Failed to add start job for session " + std::to_string(job_count));
            return -1;
        }
        
        _trading_cron_jobs.push_back(start_job_id);
        LOG_INFO("Added start job: {} at {:02d}:{:02d} ({}min before trading: {:02d}:{:02d})", 
                 start_job_name, start_hour, start_minute, _schedule_interval_minutes,
                 session.start_hour, session.start_minute);
        
        // 创建停止任务
        std::string end_cron = _create_cron_expression(end_hour, end_minute, config.trading_weekdays);
        std::string end_job_name = "auto_stop_" + std::to_string(job_count);
        
        int end_job_id = _cron_scheduler->add_job(
            end_job_name,
            end_cron,
            [this]() { _auto_stop_services(); }
        );
        
        if (end_job_id <= 0) {
            _set_error("Failed to add stop job for session " + std::to_string(job_count));
            return -1;
        }
        
        _trading_cron_jobs.push_back(end_job_id);
        LOG_INFO("Added stop job: {} at {:02d}:{:02d} ({}min after trading: {:02d}:{:02d})", 
                 end_job_name, end_hour, end_minute, _schedule_interval_minutes,
                 session.end_hour, session.end_minute);
        
        job_count++;
    }
    
    LOG_INFO("Successfully set up {} trading cron jobs", _trading_cron_jobs.size());
    return 0;
}

// 修改重叠检测方法
void AutoScheduler::_check_schedule_overlaps(const std::vector<TradingSession>& sessions)
{
    LOG_INFO("Checking for schedule overlaps with {} minutes interval...", _schedule_interval_minutes);
    
    if (sessions.size() < 2) {
        LOG_INFO("Only {} session(s), no overlaps possible", sessions.size());
        return;
    }
    
    struct ActivePeriod {
        int start_minutes;
        int end_minutes;
        int session_index;
        
        ActivePeriod(int start, int end, int idx) 
            : start_minutes(start), end_minutes(end), session_index(idx) {}
    };
    
    std::vector<ActivePeriod> active_periods;
    
    // 计算每个会话的完整活跃时间段（从启动到停止）
    for (size_t i = 0; i < sessions.size(); ++i) {
        const auto& session = sessions[i];
        
        // 启动时间（交易开始前N分钟）
        int start_minutes = session.start_hour * 60 + session.start_minute - _schedule_interval_minutes;
        if (start_minutes < 0) start_minutes += 24 * 60;
        
        // 停止时间（交易结束后N分钟）
        int end_minutes = session.end_hour * 60 + session.end_minute + _schedule_interval_minutes;
        if (end_minutes >= 24 * 60) end_minutes -= 24 * 60;
        
        active_periods.emplace_back(start_minutes, end_minutes, i);
    }
    
    // 按开始时间排序
    std::sort(active_periods.begin(), active_periods.end(), 
              [](const ActivePeriod& a, const ActivePeriod& b) {
                  return a.start_minutes < b.start_minutes;
              });
    
    // 检测时间段重叠
    bool has_overlap = false;
    for (size_t i = 0; i < active_periods.size() - 1; ++i) {
        const auto& current = active_periods[i];
        const auto& next = active_periods[i + 1];
        
        // 检查重叠：如果当前时间段的结束时间晚于下一个时间段的开始时间
        if (current.end_minutes > next.start_minutes) {
            has_overlap = true;
            
            int curr_start_hour = current.start_minutes / 60;
            int curr_start_min = current.start_minutes % 60;
            int curr_end_hour = current.end_minutes / 60;
            int curr_end_min = current.end_minutes % 60;
            
            int next_start_hour = next.start_minutes / 60;
            int next_start_min = next.start_minutes % 60;
            int next_end_hour = next.end_minutes / 60;
            int next_end_min = next.end_minutes % 60;
            
            // 计算重叠时间
            int overlap_start = next.start_minutes;
            int overlap_end = std::min(current.end_minutes, next.end_minutes);
            int overlap_duration = overlap_end - overlap_start;
            
            int overlap_start_hour = overlap_start / 60;
            int overlap_start_min = overlap_start % 60;
            int overlap_end_hour = overlap_end / 60;
            int overlap_end_min = overlap_end % 60;
            
            // 只记录重叠详情，不要大标题
            LOG_WARN("Session {} ({:02d}:{:02d}-{:02d}:{:02d}) overlaps with Session {} ({:02d}:{:02d}-{:02d}:{:02d})", 
                     current.session_index, curr_start_hour, curr_start_min, curr_end_hour, curr_end_min,
                     next.session_index, next_start_hour, next_start_min, next_end_hour, next_end_min);
            LOG_WARN("Overlap period: {:02d}:{:02d} - {:02d}:{:02d} ({} minutes)", 
                     overlap_start_hour, overlap_start_min, overlap_end_hour, overlap_end_min, overlap_duration);
        }
    }
    
    if (has_overlap) {
        LOG_ERROR("🚨 CRITICAL: Schedule overlaps detected! Services will conflict during overlapping periods!");
        LOG_ERROR("Current interval: {} minutes", _schedule_interval_minutes);
        
        // 只显示计算后的活跃时间段
        LOG_INFO("Active periods (including {}min buffer):", _schedule_interval_minutes);
        for (const auto& period : active_periods) {
            int start_hour = period.start_minutes / 60;
            int start_min = period.start_minutes % 60;
            int end_hour = period.end_minutes / 60;
            int end_min = period.end_minutes % 60;
            LOG_INFO("  Session {}: {:02d}:{:02d} - {:02d}:{:02d}", 
                     period.session_index, start_hour, start_min, end_hour, end_min);
        }
        
        LOG_ERROR("SOLUTION: Increase gap between sessions or reduce interval time!");
        LOG_ERROR("Minimum gap needed: {} minutes", _schedule_interval_minutes * 2 + 1);
        
    } else {
        LOG_INFO("✅ No schedule overlaps detected with {} minutes interval", _schedule_interval_minutes);
    }
}

void AutoScheduler::_cleanup_cron_jobs()
{
    // 这个方法现在只在 _setup_cron_jobs 中调用，不需要额外的锁保护
    for (int job_id : _trading_cron_jobs) {
        if (job_id > 0) {
            _cron_scheduler->remove_job(job_id);
        }
    }
    _trading_cron_jobs.clear();
    
    LOG_INFO("Cron jobs cleaned up");
}

// ===== 定时任务回调 =====

void AutoScheduler::_auto_start_services()
{
    // 防止重复启动
    if (_starting.exchange(true)) {
        LOG_DEBUG("Start operation already in progress, skipping duplicate call");
        return;
    }
    
    if (!_auto_mode.load()) {
        LOG_DEBUG("Auto mode disabled, skipping auto start");
        _starting.store(false);
        return;
    }
    
    // 检查今天是否为交易日（无锁调用）
    if (!_trading_time_manager || !_trading_time_manager->is_trading_day(std::chrono::system_clock::now())) {
        LOG_DEBUG("Today is not a trading day, skipping auto start");
        _starting.store(false);
        return;
    }
    
    LOG_INFO("Trading time approaching ({}min before), starting services automatically", _schedule_interval_minutes);
    
    // 直接调用回调函数，让服务自己决定是否需要启动
    if (_start_callback) {
        bool success = _start_callback();
        if (success) {
            LOG_INFO("Services started successfully (auto mode)");
        } else {
            LOG_ERROR("Failed to start services (auto mode)");
        }
    }
    
    // 延迟重置标志，防止短时间内重复调用
    std::this_thread::sleep_for(std::chrono::seconds(1));
    _starting.store(false);
}

void AutoScheduler::_auto_stop_services()
{
    // 防止重复停止
    if (_stopping.exchange(true)) {
        LOG_DEBUG("Stop operation already in progress, skipping duplicate call");
        return;
    }
    
    if (!_auto_mode.load()) {
        LOG_DEBUG("Auto mode disabled, skipping auto stop");
        _stopping.store(false);
        return;
    }
    
    LOG_INFO("Trading time ended ({} min after), stopping services automatically", _schedule_interval_minutes);
    
    // 直接调用回调函数，让服务自己决定是否需要停止
    if (_stop_callback) {
        _stop_callback();
    }
    
    LOG_INFO("Services stopped (auto mode)");
    
    // 延迟重置标志，防止短时间内重复调用
    std::this_thread::sleep_for(std::chrono::seconds(1));
    _stopping.store(false);
}

void AutoScheduler::_check_should_start_services()
{
    if (!_auto_mode.load()) {
        return;
    }
    
    if (!_trading_time_manager) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    bool should_start = _trading_time_manager->is_trading_time(now);
    
    if (should_start) {
        LOG_INFO("Currently in trading time, starting services");
        _auto_start_services();
    }
}

std::string AutoScheduler::_create_cron_expression(int hour, int minute, const std::vector<int>& trading_weekdays)
{
    // 创建 cron 表达式格式: "秒 分 时 日 月 周"
    std::ostringstream oss;
    oss << "0 " << minute << " " << hour << " * *";
    
    // 添加交易日限制
    if (!trading_weekdays.empty()) {
        // 检查是否包含全部天（1-7）
        bool is_all_days = (trading_weekdays.size() == 7);
        if (is_all_days) {
            for (int day = 1; day <= 7; ++day) {
                if (std::find(trading_weekdays.begin(), trading_weekdays.end(), day) == trading_weekdays.end()) {
                    is_all_days = false;
                    break;
                }
            }
        }
        
        if (is_all_days) {
            // 如果包含全部天，使用 * 表示所有天
            oss << " *";
        } else {
            // 转换为 cron 标准格式：我们的 1-7 -> cron 的 1-6,0
            oss << " ";
            bool first = true;
            for (int day : trading_weekdays) {
                if (!first) oss << ",";
                first = false;
                
                if (day == 7) {
                    oss << "0";  // 周日：我们的 7 -> cron 的 0
                } else {
                    oss << day;  // 周一到周六：保持不变
                }
            }
        }
    } else {
        oss << " *";  // 所有日期
    }
    
    return oss.str();
}

void AutoScheduler::_set_error(const std::string& error)
{
    _last_error = error;
    LOG_ERROR("AutoScheduler error: {}", error);
}

NS_FT_END