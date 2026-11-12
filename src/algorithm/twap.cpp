#include "algorithm/twap.h"
#include "trade/trade_handler.h"
#include "quote/quote_interface.h"
#include "utils/trading_time.h"
#include "logger/logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <random>

NS_FT_BEGIN

// ===== TwapTask 方法实现 =====

std::chrono::seconds TwapTask::get_trading_duration() const
{
    if (!slices.empty())
    {
        // 从拆单片段计算实际交易时长
        auto first_time = slices.front().execution_time;
        auto last_time = slices.back().execution_time;
        return std::chrono::duration_cast<std::chrono::seconds>(last_time - first_time);
    }
    return std::chrono::seconds(0);
}

nlohmann::json TwapTask::to_json() const
{
    nlohmann::json j;
    j["task_id"] = task_id;
    j["task_id_formatted"] = TwapAlgorithm::format_task_id(task_id);
    j["security_code"] = security_code;
    j["market"] = static_cast<int>(market);
    j["total_quantity"] = total_quantity;
    j["interval_seconds"] = interval.count();
    j["status"] = static_cast<int>(status);

    // 时间字段
    auto to_timestamp = [](const std::chrono::system_clock::time_point &tp)
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   tp.time_since_epoch())
            .count();
    };

    j["start_time"] = to_timestamp(start_time);
    j["end_time"] = to_timestamp(end_time);
    j["create_time"] = to_timestamp(create_time);

    // 执行状态
    j["total_slices"] = get_total_slice_count();
    j["executed_slices"] = get_executed_slice_count();
    j["submitted_count"] = submitted_count;

    // 价格阈值信息
    j["enable_price_threshold"] = enable_price_threshold;
    j["price_threshold_ratio"] = price_threshold_ratio;
    j["initial_price"] = initial_price;
    j["initial_price_set"] = initial_price_set;

    j["error_msg"] = error_msg;

    return j;
}

// ===== TwapAlgorithm 构造和析构 =====

TwapAlgorithm::TwapAlgorithm()
    : _initialized(false), _running(false), _should_stop_execution(false), _should_stop_cleanup(false)
{
}

TwapAlgorithm::~TwapAlgorithm()
{
    release();
}

// ===== 生命周期管理 =====

int TwapAlgorithm::init()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (_initialized.load())
    {
        LOG_WARN("TwapAlgorithm already initialized");
        return 0;
    }

    try
    {
        // 创建线程池
        _thread_pool = std::make_unique<BS::thread_pool<>>(4);

        // 重置统计信息
        _total_tasks_created.store(0);
        _total_tasks_running.store(0);
        _total_tasks_completed.store(0);
        _total_tasks_cancelled.store(0);
        _total_tasks_error.store(0);
        _total_slices_executed.store(0);
        _total_slices_error.store(0);

        _initialized.store(true);
        LOG_INFO("TwapAlgorithm initialized successfully");
        return 0;
    }
    catch (const std::exception &e)
    {
        _set_error("Failed to initialize TWAP algorithm: " + std::string(e.what()));
        return -1;
    }
}

int TwapAlgorithm::start()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (!_initialized.load())
    {
        _set_error("TWAP algorithm not initialized");
        return -1;
    }

    if (_running.load())
    {
        LOG_WARN("TwapAlgorithm already running");
        return 0;
    }

    // 检查依赖服务
    if (!_quote_parser)
    {
        _set_error("Quote parser not set");
        return -1;
    }

    if (!_trade_handler)
    {
        _set_error("Trade handler not set");
        return -1;
    }

    if (!_trading_time_manager)
    {
        _set_error("TradingTimeManager not set");
        return -1;
    }

    try
    {
        // 启动执行线程
        _should_stop_execution.store(false);
        _execution_thread = std::make_unique<std::thread>(&TwapAlgorithm::_execution_loop, this);

        // 启动清理线程
        _should_stop_cleanup.store(false);
        _cleanup_thread = std::make_unique<std::thread>(&TwapAlgorithm::_cleanup_loop, this);

        _running.store(true);
        LOG_INFO("TwapAlgorithm started successfully");
        return 0;
    }
    catch (const std::exception &e)
    {
        _set_error("Failed to start TWAP algorithm: " + std::string(e.what()));
        return -1;
    }
}

int TwapAlgorithm::stop()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (!_running.load())
    {
        return 0;
    }

    LOG_INFO("Stopping TwapAlgorithm...");

    // 停止执行线程
    _should_stop_execution.store(true);
    _execution_cv.notify_all();

    // 停止清理线程
    _should_stop_cleanup.store(true);
    _cleanup_cv.notify_all();

    // 等待线程结束
    if (_execution_thread && _execution_thread->joinable())
    {
        _execution_thread->join();
        _execution_thread.reset();
    }

    if (_cleanup_thread && _cleanup_thread->joinable())
    {
        _cleanup_thread->join();
        _cleanup_thread.reset();
    }

    _running.store(false);
    LOG_INFO("TwapAlgorithm stopped successfully");
    return 0;
}

void TwapAlgorithm::release()
{
    stop();

    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // 清理所有任务
    _all_tasks.clear();
    _running_tasks.clear();
    _order_to_task_map.clear();

    // 清空执行队列
    while (!_execution_queue.empty())
    {
        _execution_queue.pop();
    }

    // 清理线程池
    if (_thread_pool)
    {
        _thread_pool.reset();
    }

    _initialized.store(false);
    LOG_INFO("TwapAlgorithm released successfully");
}

// ===== 依赖注入 =====

void TwapAlgorithm::set_quote_parser(IQuoteParser *quote_parser)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _quote_parser = quote_parser;
}

void TwapAlgorithm::set_trade_handler(ITradeHandler *trade_handler)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _trade_handler = trade_handler;
}

void TwapAlgorithm::set_trading_time_manager(TradingTimeManager *trading_time_manager)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _trading_time_manager = trading_time_manager;
}

IQuoteParser *TwapAlgorithm::get_quote_parser() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _quote_parser;
}

ITradeHandler *TwapAlgorithm::get_trade_handler() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _trade_handler;
}

TradingTimeManager *TwapAlgorithm::get_trading_time_manager() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _trading_time_manager;
}

// ===== 状态查询 =====

bool TwapAlgorithm::is_initialized() const
{
    return _initialized.load();
}

bool TwapAlgorithm::is_running() const
{
    return _running.load();
}

std::string TwapAlgorithm::get_last_error() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _last_error;
}

// ===== TWAP拆单接口 =====

uint64_t TwapAlgorithm::create_twap_sell_task(
    const AccountInfo &account,
    const std::string &security_code,
    MarketType market,
    int64_t total_quantity,
    const std::chrono::system_clock::time_point &start_time,
    const std::chrono::system_clock::time_point &end_time,
    int interval_seconds,
    bool enable_price_threshold,
    double price_threshold_ratio)
{

    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (!_running.load())
    {
        _set_error("TWAP algorithm not running");
        return 0; // 失败返回0
    }

    // 参数验证
    if (total_quantity <= 0)
    {
        _set_error("Total quantity must be positive");
        return 0;
    }

    if (interval_seconds <= 0)
    {
        _set_error("Interval seconds must be positive");
        return 0;
    }

    if (start_time >= end_time)
    {
        _set_error("Start time must be before end time");
        return 0;
    }

    if (_all_tasks.size() >= MAX_TOTAL_TASKS)
    {
        _set_error("Maximum number of tasks reached");
        return 0;
    }

    if (_running_tasks.size() >= MAX_RUNNING_TASKS)
    {
        _set_error("Maximum number of running tasks reached");
        return 0;
    }

    try
    {
        // 创建新任务
        TwapTask task;
        task.task_id = _generate_task_id();
        task.account = account;
        task.security_code = security_code;
        task.market = market;
        task.total_quantity = total_quantity;
        task.interval = std::chrono::seconds(interval_seconds);
        task.start_time = start_time;
        task.end_time = end_time;
        task.create_time = std::chrono::system_clock::now();
        task.status = TwapTaskStatus::CREATED;
        task.enable_price_threshold = enable_price_threshold;
        task.price_threshold_ratio = price_threshold_ratio;

        // 计算拆单片段
        if (_calculate_twap_slices(task) != 0)
        {
            return 0;
        }

        // 保存任务
        uint64_t task_id = task.task_id;
        _all_tasks[task_id] = task;
        _running_tasks[task_id] = &_all_tasks[task_id];

        // 调度所有拆单片段
        for (size_t i = 0; i < task.slices.size(); ++i)
        {
            _schedule_slice_execution(task_id, i, task.slices[i].execution_time);
        }

        // 更新任务状态为运行中
        _update_task_status(task_id, TwapTaskStatus::RUNNING);

        // 更新统计信息
        _total_tasks_created.fetch_add(1);
        _total_tasks_running.fetch_add(1);

        LOG_INFO("TWAP task created: {} ({}) with {} slices",
                 task_id, format_task_id(task_id), task.slices.size());
        return task_id;
    }
    catch (const std::exception &e)
    {
        _set_error("Failed to create TWAP task: " + std::string(e.what()));
        return 0;
    }
}

int TwapAlgorithm::cancel_twap_task(uint64_t task_id)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _running_tasks.find(task_id);
    if (it == _running_tasks.end())
    {
        _set_error("Task not found or not running: " + std::to_string(task_id));
        return -1;
    }

    // 更新任务状态
    _update_task_status(task_id, TwapTaskStatus::CANCELLED);

    // 从运行任务中移除
    _running_tasks.erase(it);

    _total_tasks_running.fetch_sub(1);
    _total_tasks_cancelled.fetch_add(1);

    LOG_INFO("TWAP task cancelled: {}", task_id);
    return 0;
}

std::string TwapAlgorithm::get_twap_task_status(uint64_t task_id)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _all_tasks.find(task_id);
    if (it == _all_tasks.end())
    {
        return "";
    }

    return it->second.to_json().dump();
}

std::string TwapAlgorithm::get_all_tasks()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    nlohmann::json tasks = nlohmann::json::array();
    for (const auto &pair : _all_tasks)
    {
        tasks.push_back(pair.second.to_json());
    }

    return tasks.dump();
}

std::string TwapAlgorithm::get_running_tasks()
{
    return get_tasks_by_status(TwapTaskStatus::RUNNING);
}

std::string TwapAlgorithm::get_tasks_by_status(TwapTaskStatus status)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    nlohmann::json tasks = nlohmann::json::array();
    for (const auto &pair : _all_tasks)
    {
        if (pair.second.status == status)
        {
            tasks.push_back(pair.second.to_json());
        }
    }

    return tasks.dump();
}

std::string TwapAlgorithm::get_task_statistics()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    nlohmann::json stats;
    stats["total_tasks"] = _all_tasks.size();
    stats["running_tasks"] = _running_tasks.size();
    stats["total_created"] = _total_tasks_created.load();
    stats["total_running"] = _total_tasks_running.load();
    stats["total_completed"] = _total_tasks_completed.load();
    stats["total_cancelled"] = _total_tasks_cancelled.load();
    stats["total_error"] = _total_tasks_error.load();
    stats["total_slices_executed"] = _total_slices_executed.load();
    stats["total_slices_error"] = _total_slices_error.load();
    stats["concurrent_executions"] = _concurrent_executions.load();

    return stats.dump();
}

int TwapAlgorithm::clear_all_tasks()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    int cleared_count = _all_tasks.size();

    _all_tasks.clear();
    _running_tasks.clear();
    _order_to_task_map.clear();

    // 重置统计信息
    _total_tasks_running.store(0);

    LOG_INFO("Cleared {} TWAP tasks", cleared_count);
    return cleared_count;
}

int TwapAlgorithm::clear_tasks_by_status(TwapTaskStatus status)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    int cleared_count = 0;
    auto it = _all_tasks.begin();
    while (it != _all_tasks.end())
    {
        if (it->second.status == status)
        {
            // 如果是运行中的任务，也要从运行任务中移除
            if (status == TwapTaskStatus::RUNNING)
            {
                _running_tasks.erase(it->first);
                _total_tasks_running.fetch_sub(1);
            }
            it = _all_tasks.erase(it);
            cleared_count++;
        }
        else
        {
            ++it;
        }
    }

    LOG_INFO("Cleared {} TWAP tasks with status {}", cleared_count, static_cast<int>(status));
    return cleared_count;
}

int TwapAlgorithm::clear_tasks_before(const std::chrono::system_clock::time_point &before_time)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    int cleared_count = 0;
    auto it = _all_tasks.begin();
    while (it != _all_tasks.end())
    {
        if (it->second.create_time < before_time &&
            it->second.status != TwapTaskStatus::RUNNING)
        {

            it = _all_tasks.erase(it);
            cleared_count++;
        }
        else
        {
            ++it;
        }
    }

    LOG_INFO("Cleared {} TWAP tasks created before specified time", cleared_count);
    return cleared_count;
}

// ===== 私有方法实现 =====

void TwapAlgorithm::_execution_loop()
{
    LOG_INFO("TWAP execution thread started");

    while (!_should_stop_execution.load())
    {
        try
        {
            std::unique_lock<std::mutex> lock(_execution_mutex);

            // 等待直到有任务需要执行或者收到停止信号
            bool has_work = _execution_cv.wait_for(lock, std::chrono::seconds(1), [this]()
                                   { return _should_stop_execution.load() || !_execution_queue.empty(); });

            if (_should_stop_execution.load())
            {
                break;
            }

            if (has_work && !_execution_queue.empty())
            {
                // 处理到期的执行任务
                _process_due_executions();
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in TWAP execution loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_INFO("TWAP execution thread stopped");
}

void TwapAlgorithm::_cleanup_loop()
{
    LOG_INFO("TWAP cleanup thread started");

    while (!_should_stop_cleanup.load())
    {
        try
        {
            std::unique_lock<std::mutex> lock(_cleanup_mutex);

            // 每小时检查一次
            _cleanup_cv.wait_for(lock, std::chrono::hours(1), [this]()
                                 { return _should_stop_cleanup.load(); });

            if (_should_stop_cleanup.load())
            {
                break;
            }

            // 检查是否需要每日清理
            if (_should_daily_cleanup())
            {
                _perform_daily_cleanup();
            }

            // 执行智能清理
            _smart_cleanup();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in TWAP cleanup loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::minutes(10));
        }
    }

    LOG_INFO("TWAP cleanup thread stopped");
}

void TwapAlgorithm::_schedule_slice_execution(uint64_t task_id,
                                              size_t slice_index,
                                              std::chrono::system_clock::time_point exec_time)
{
    std::lock_guard<std::mutex> lock(_execution_mutex);

    TaskExecution execution;
    execution.task_id = task_id;
    execution.slice_index = slice_index;
    execution.execution_time = exec_time;

    _execution_queue.push(execution);
    
    _execution_cv.notify_one();
}

void TwapAlgorithm::_process_due_executions()
{
    auto now = std::chrono::system_clock::now();
    int processed_count = 0;
    int skipped_count = 0;

    while (!_execution_queue.empty() && _execution_queue.top().execution_time <= now)
    {
        auto execution = _execution_queue.top();
        _execution_queue.pop();

        auto delay = std::chrono::duration_cast<std::chrono::seconds>(now - execution.execution_time);
        
        LOG_DEBUG("Processing execution: task={}, slice={}, scheduled_time={}, delay={}s", 
                 execution.task_id, execution.slice_index, 
                 _format_time_point(execution.execution_time), delay.count());

        // 检查是否可以执行
        if (_concurrent_executions.load() < MAX_CONCURRENT_EXECUTIONS)
        {
            _async_execute_slice(execution.task_id, execution.slice_index);
            processed_count++;
        }
        else
        {
            // 如果并发数太高，重新调度到稍后
            execution.execution_time = now + std::chrono::seconds(1);
            _execution_queue.push(execution);
            skipped_count++;
            
            LOG_DEBUG("Max concurrent executions reached ({}), rescheduling task={} slice={}", 
                     MAX_CONCURRENT_EXECUTIONS, execution.task_id, execution.slice_index);
            break;
        }
    }

    if (processed_count > 0 || skipped_count > 0)
    {
        LOG_DEBUG("Execution processing completed: processed={}, skipped={}, remaining_queue={}", 
                 processed_count, skipped_count, _execution_queue.size());
    }
}

void TwapAlgorithm::_async_execute_slice(uint64_t task_id, size_t slice_index)
{
    if (!_thread_pool)
    {
        LOG_ERROR("Thread pool not available for task {} slice {}", task_id, slice_index);
        return;
    }

    _concurrent_executions.fetch_add(1);

    _thread_pool->detach_task([this, task_id, slice_index]() {
        try {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            
            auto running_it = _running_tasks.find(task_id);
            if (running_it == _running_tasks.end()) {
                return;
            }
            
            TwapTask* task = running_it->second;
            if (slice_index >= task->slices.size()) {
                return;
            }
            
            TwapSlice& slice = task->slices[slice_index];
            if (slice.is_executed) {
                return;
            }
            
            // 检查是否在交易时间内
            auto now = std::chrono::system_clock::now();
            bool is_trading_time = _can_execute_next_slice(*task);
            
            LOG_DEBUG("Trading time check: task={}, slice={}, is_trading_time={}, now={}", 
                     task_id, slice_index, is_trading_time, _format_time_point(now));
            
            if (!is_trading_time) {
                // 调整到下一个交易时间
                auto next_trading_time = _adjust_to_next_trading_time(now);
                
                LOG_DEBUG("Not in trading time, adjusting: task={}, slice={}, next_time={}, end_time={}", 
                         task_id, slice_index, _format_time_point(next_trading_time), 
                         _format_time_point(task->end_time));
                
                if (next_trading_time <= task->end_time) {
                    slice.execution_time = next_trading_time;
                    LOG_INFO("Rescheduling task {} slice {} to next trading time: {}", 
                             task_id, slice_index, _format_time_point(next_trading_time));
                    _schedule_slice_execution(task_id, slice_index, next_trading_time);
                } else {
                    // 超出了结束时间，跳过这个片段
                    slice.error_msg = "Execution time beyond task end time and trading hours";
                    slice.is_processed = true;
                    _total_slices_error.fetch_add(1);
                    LOG_WARN("Skipping task {} slice {} due to time constraints", task_id, slice_index);
                }
                return;
            }
            
            // 价格阈值检查
            if (task->enable_price_threshold) {
                LOG_DEBUG("Checking price threshold for task {} slice {}", task_id, slice_index);
                double current_price = _get_real_time_price(task->security_code, task->market);
                if (!_check_price_threshold(task_id, current_price)) {
                    LOG_WARN("Price threshold check failed for task {}, cancelling", task_id);
                    _update_task_status(task_id, TwapTaskStatus::CANCELLED);
                    return;
                }
            }
            
            // 执行卖出订单
            int64_t order_id = _execute_sell_order(*task, slice);
            if (order_id > 0) {
                slice.actual_order_id = order_id;
                slice.is_executed = true;
                task->submitted_count++;
                
                // 记录订单到任务的映射
                _order_to_task_map[order_id] = task_id;
                
                _total_slices_executed.fetch_add(1);
                
                LOG_INFO("TWAP slice executed: task={}, slice={}/{}, order_id={}, quantity={}", 
                         task_id, slice_index + 1, task->slices.size(), order_id, slice.quantity);
            } else {
                slice.error_msg = "Failed to execute sell order";
                slice.is_processed = true;
                _total_slices_error.fetch_add(1);
                LOG_ERROR("Failed to execute TWAP slice: task={}, slice={}/{}", 
                         task_id, slice_index + 1, task->slices.size());
            }
            
            // 标记切片已处理（不管成功还是失败）
            if (!slice.is_processed) {
                slice.is_processed = true;
            }

            // 检查任务是否完成
            size_t processed_count = task->get_processed_slice_count();
            size_t total_count = task->get_total_slice_count();
            
            if (processed_count == total_count) {
                _update_task_status(task_id, TwapTaskStatus::COMPLETED);
                _running_tasks.erase(task_id);
                _total_tasks_running.fetch_sub(1);
                _total_tasks_completed.fetch_add(1);
                
                LOG_INFO("TWAP task completed: {} (executed: {}/{}, failed: {})", 
                         task_id, task->get_executed_slice_count(), total_count,
                         total_count - task->get_executed_slice_count());
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception executing TWAP slice {}/{}: {}", task_id, slice_index, e.what());
            _total_slices_error.fetch_add(1);
        }
        
        _concurrent_executions.fetch_sub(1);
    });
}

void TwapAlgorithm::_update_task_status(uint64_t task_id, TwapTaskStatus new_status)
{
    auto it = _all_tasks.find(task_id);
    if (it != _all_tasks.end())
    {
        it->second.status = new_status;

        if (new_status == TwapTaskStatus::ERROR)
        {
            _total_tasks_error.fetch_add(1);
            if (_running_tasks.find(task_id) != _running_tasks.end())
            {
                _running_tasks.erase(task_id);
                _total_tasks_running.fetch_sub(1);
            }
        }
    }
}

bool TwapAlgorithm::_should_daily_cleanup()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto *tm = std::localtime(&time_t);

    return (tm->tm_hour == DAILY_CLEANUP_HOUR && tm->tm_min == DAILY_CLEANUP_MINUTE);
}

std::chrono::system_clock::time_point TwapAlgorithm::_calculate_next_cleanup_time()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto *tm = std::localtime(&time_t);

    // 设置为明天的清理时间
    tm->tm_mday += 1;
    tm->tm_hour = DAILY_CLEANUP_HOUR;
    tm->tm_min = DAILY_CLEANUP_MINUTE;
    tm->tm_sec = 0;

    return std::chrono::system_clock::from_time_t(std::mktime(tm));
}

void TwapAlgorithm::_perform_daily_cleanup()
{
    LOG_INFO("Performing daily TWAP cleanup...");

    auto cutoff_time = std::chrono::system_clock::now() - std::chrono::hours(24);
    int cleared = clear_tasks_before(cutoff_time);

    LOG_INFO("Daily cleanup completed, cleared {} tasks", cleared);
}

void TwapAlgorithm::_smart_cleanup()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // 如果任务数量超过阈值，清理已完成和已取消的旧任务
    if (_all_tasks.size() > MAX_TOTAL_TASKS * 0.8)
    {
        int cleared = 0;
        auto cutoff_time = std::chrono::system_clock::now() - std::chrono::hours(6);

        auto it = _all_tasks.begin();
        while (it != _all_tasks.end() && cleared < 100)
        {
            const auto &task = it->second;
            if ((task.status == TwapTaskStatus::COMPLETED ||
                 task.status == TwapTaskStatus::CANCELLED ||
                 task.status == TwapTaskStatus::ERROR) &&
                task.create_time < cutoff_time)
            {

                it = _all_tasks.erase(it);
                cleared++;
            }
            else
            {
                ++it;
            }
        }

        if (cleared > 0)
        {
            LOG_INFO("Smart cleanup cleared {} old tasks", cleared);
        }
    }
}

bool TwapAlgorithm::_check_price_threshold(uint64_t task_id, double current_price)
{
    auto it = _all_tasks.find(task_id);
    if (it == _all_tasks.end())
    {
        return false;
    }

    TwapTask &task = it->second;

    if (!task.initial_price_set)
    {
        task.initial_price = current_price;
        task.initial_price_set = true;
        return true;
    }

    if (current_price <= 0.0 || task.initial_price <= 0.0)
    {
        return true; // 价格无效，继续执行
    }

    double price_change_ratio = (task.initial_price - current_price) / task.initial_price;

    if (price_change_ratio > task.price_threshold_ratio)
    {
        LOG_WARN("TWAP task {} cancelled due to price drop: initial={:.3f}, current={:.3f}, threshold={:.3f}",
                 task_id, task.initial_price, current_price, task.price_threshold_ratio);
        task.error_msg = "Price dropped below threshold";
        return false;
    }

    return true;
}

double TwapAlgorithm::_get_real_time_price(const std::string &security_code, MarketType market)
{
    if (!_quote_parser)
    {
        LOG_WARN("Quote parser not available");
        return 0.0;
    }

    try
    {
        DepthData depth_data;
        int result = _quote_parser->get_depth_data_by_code(security_code.c_str(), &depth_data);

        if (result == 0)
        { // 成功获取数据
            // 优先使用最新价
            if (depth_data.last_price > 0.0)
            {
                return depth_data.last_price;
            }

            LOG_DEBUG("No last price found for security: {}", security_code);
            return 0.0;
        }
        else if (result == -1)
        {
            LOG_DEBUG("No depth data found for security: {}", security_code);
            return 0.0;
        }
        else
        {
            LOG_ERROR("Failed to get depth data for security: {}, error code: {}", security_code, result);
            return 0.0;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception getting real time price for {}: {}", security_code, e.what());
        return 0.0;
    }
}

uint64_t TwapAlgorithm::_generate_task_id()
{
    // 获取当前时间戳（秒级）
    auto now = std::chrono::system_clock::now();
    uint32_t timestamp = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch())
            .count());

    // 获取递增序列号（线程安全）
    uint32_t sequence = _task_sequence.fetch_add(1);

    // 组合成64位ID：高32位时间戳 + 低32位序列
    uint64_t task_id = (static_cast<uint64_t>(timestamp) << 32) | sequence;

    return task_id;
}

uint32_t TwapAlgorithm::_extract_timestamp(uint64_t task_id)
{
    return static_cast<uint32_t>(task_id >> 32);
}

uint32_t TwapAlgorithm::_extract_sequence(uint64_t task_id)
{
    return static_cast<uint32_t>(task_id & 0xFFFFFFFF);
}

std::string TwapAlgorithm::format_task_id(uint64_t task_id)
{
    uint32_t timestamp = _extract_timestamp(task_id);
    uint32_t sequence = _extract_sequence(task_id);

    // 转换时间戳为可读格式
    auto time_point = std::chrono::system_clock::from_time_t(timestamp);
    auto time_t = std::chrono::system_clock::to_time_t(time_point);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(8) << sequence;

    return oss.str();
}

int TwapAlgorithm::_calculate_twap_slices(TwapTask &task)
{
    if (!_trading_time_manager)
    {
        _set_error("TradingTimeManager not available");
        return -1;
    }

    try
    {
        // 计算从start_time到end_time的总时间间隔（秒）
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(
            task.end_time - task.start_time);

        if (total_duration <= std::chrono::seconds(0))
        {
            _set_error("End time must be after start time");
            return -1;
        }

        // 基于实际时间间隔计算拆单数量
        int total_slices = std::max(1, static_cast<int>(total_duration.count() / task.interval.count()));

        if (task.total_quantity <= 0)
        {
            _set_error("Total quantity must be positive");
            return -1;
        }

        // A股交易规则：卖出时除最后一手外都必须是100股的整数倍
        const int64_t LOT_SIZE = 100;
        
        // 计算完整手数和剩余零股
        int64_t total_lots = task.total_quantity / LOT_SIZE;
        int64_t odd_lot = task.total_quantity % LOT_SIZE; // 零股（不足一手的部分）

        LOG_INFO("Calculating TWAP slices: total_duration={}s, interval={}s, total_slices={}, total_quantity={}, lots={}, odd_lot={}",
                 total_duration.count(), task.interval.count(), total_slices, task.total_quantity, total_lots, odd_lot);

        task.slices.clear();
        auto current_time = task.start_time;

        // 如果只有1个切片，直接分配所有数量（包括零股）
        if (total_slices == 1)
        {
            auto adjusted_time = current_time;
            if (!_trading_time_manager->is_trading_time(current_time))
            {
                adjusted_time = _adjust_to_next_trading_time(current_time);
                if (adjusted_time >= task.end_time)
                {
                    _set_error("No valid trading time found in the specified period");
                    return -1;
                }
            }
            
            task.slices.emplace_back(task.total_quantity, adjusted_time);
            LOG_INFO("Single slice created: quantity={} (including {} odd lot)", task.total_quantity, odd_lot);
        }
        else
        {
            // 多个切片的均匀分配算法
            
            // 计算每个切片的基础手数（向下取整）
            int64_t base_lots_per_slice = total_lots / total_slices;
            // 计算需要额外分配1手的切片数量
            int64_t extra_lot_slices = total_lots % total_slices;
            
            LOG_DEBUG("Distribution plan: base_lots_per_slice={}, extra_lot_slices={}, total_slices={}",
                     base_lots_per_slice, extra_lot_slices, total_slices);

            int64_t remaining_quantity = task.total_quantity;
            
            auto handle_remaining_quantity = [&](const char* reason) {
                if (remaining_quantity > 0 && !task.slices.empty())
                {
                    task.slices.back().quantity += remaining_quantity;
                    LOG_INFO("Added remaining quantity {} to last slice due to {}", remaining_quantity, reason);
                    remaining_quantity = 0;
                }
            };

            for (int i = 0; i < total_slices && remaining_quantity > 0; ++i)
            {
                // 检查当前时间是否超出结束时间
                if (current_time >= task.end_time)
                {
                    handle_remaining_quantity("time limit");
                    break;
                }

                auto adjusted_time = current_time;
                
                // 检查当前时间是否在交易时间内
                if (!_trading_time_manager->is_trading_time(current_time))
                {
                    adjusted_time = _adjust_to_next_trading_time(current_time);
                    if (adjusted_time >= task.end_time)
                    {
                        handle_remaining_quantity("adjusted time limit");
                        LOG_WARN("Adjusted time {} exceeds end time, stopping slice generation", 
                                 _format_time_point(adjusted_time));
                        break;
                    }
                }

                int64_t slice_quantity = 0;
                
                if (i == total_slices - 1)
                {
                    // 最后一个切片：包含所有剩余数量（确保总和正确）
                    slice_quantity = remaining_quantity;
                }
                else
                {
                    // 前面的切片：基础手数 + 可能的额外1手
                    int64_t slice_lots = base_lots_per_slice;
                    
                    // 前extra_lot_slices个切片额外分配1手
                    if (i < extra_lot_slices)
                    {
                        slice_lots += 1;
                    }
                    
                    slice_quantity = slice_lots * LOT_SIZE;
                }

                if (slice_quantity > 0)
                {
                    task.slices.emplace_back(slice_quantity, adjusted_time);
                    remaining_quantity -= slice_quantity;
                    
                    int64_t slice_lots = slice_quantity / LOT_SIZE;
                    int64_t slice_odd_lot = slice_quantity % LOT_SIZE;
                    
                    LOG_DEBUG("Created slice {}: quantity={} (lots={}, odd_lot={}), time={}", 
                             i, slice_quantity, slice_lots, slice_odd_lot, _format_time_point(adjusted_time));
                }

                // 移动到下一个执行时间
                current_time += task.interval;
            }
        }

        // 检查是否有有效的交易时间切片
        if (task.slices.empty())
        {
            _set_error("No valid trading time found in the specified period");
            return -1;
        }

        // 验证切片数量分配的合理性
        int64_t total_allocated = 0;
        int slices_with_odd_lots = 0;
        int64_t max_slice_quantity = 0;
        int64_t min_slice_quantity = INT64_MAX;
        
        for (size_t i = 0; i < task.slices.size(); ++i)
        {
            const auto& slice = task.slices[i];
            total_allocated += slice.quantity;
            max_slice_quantity = std::max(max_slice_quantity, slice.quantity);
            min_slice_quantity = std::min(min_slice_quantity, slice.quantity);
            
            // 检查是否有零股
            if (slice.quantity % LOT_SIZE != 0)
            {
                slices_with_odd_lots++;
                // 只有最后一个切片可以有零股
                if (i != task.slices.size() - 1)
                {
                    LOG_ERROR("Non-final slice {} has odd lot: quantity={}", i, slice.quantity);
                    _set_error("Only the final slice can have odd lots");
                    return -1;
                }
            }
        }

        // 最多只能有1个切片包含零股（最后一个）
        if (slices_with_odd_lots > 1)
        {
            LOG_ERROR("Multiple slices have odd lots: count={}", slices_with_odd_lots);
            _set_error("Multiple slices cannot have odd lots");
            return -1;
        }

        // 检查分配的均匀性（最大最小差异不应超过2手）
        int64_t max_diff = max_slice_quantity - min_slice_quantity;
        if (max_diff > 2 * LOT_SIZE && task.slices.size() > 1)
        {
            LOG_WARN("Slice distribution may be uneven: max={}, min={}, diff={}", 
                     max_slice_quantity, min_slice_quantity, max_diff);
        }

        LOG_INFO("Generated {} TWAP slices: total_quantity={}, allocated={}, min_slice={}, max_slice={}, slices_with_odd_lots={}",
                 task.slices.size(), task.total_quantity, total_allocated, min_slice_quantity, max_slice_quantity, slices_with_odd_lots);

        // 验证分配的总数量
        if (total_allocated != task.total_quantity)
        {
            LOG_ERROR("Total allocated quantity {} does not match task quantity {}", 
                     total_allocated, task.total_quantity);
            _set_error("Quantity allocation mismatch");
            return -1;
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        _set_error("Failed to calculate TWAP slices: " + std::string(e.what()));
        return -1;
    }
}

bool TwapAlgorithm::_can_execute_next_slice(const TwapTask &task) const
{
    if (!_trading_time_manager)
    {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    bool is_trading = _trading_time_manager->is_trading_time(now);
    
    return is_trading;
}

std::chrono::system_clock::time_point TwapAlgorithm::_adjust_to_next_trading_time(
    const std::chrono::system_clock::time_point &time_point) const
{

    if (!_trading_time_manager)
    {
        return time_point;
    }

    auto adjusted_time = _trading_time_manager->adjust_to_trading_time(time_point, true);

    return adjusted_time;
}

void TwapAlgorithm::_set_error(const std::string &error)
{
    _last_error = error;
    LOG_ERROR("TwapAlgorithm error: {}", error);
}

int64_t TwapAlgorithm::_execute_sell_order(const TwapTask &task, const TwapSlice &slice)
{
    if (!_trade_handler)
    {
        return 0;
    }

    try
    {
        // 实时获取当前价格
        double current_price = _get_real_time_price(task.security_code, task.market);
        if (current_price <= 0.0)
        {
            LOG_ERROR("Failed to get real-time price for security: {}", task.security_code);
            return 0;
        }

        // 构造卖出订单请求
        TradeOrderRequest order_req = {}; // 初始化为零
        order_req.account = task.account;

        // 安全的字符串复制
        if (task.security_code.length() >= sizeof(order_req.security_code))
        {
            LOG_ERROR("Security code too long: {}", task.security_code);
            return 0;
        }
        strcpy(order_req.security_code, task.security_code.c_str());

        order_req.market = task.market;
        order_req.side = SIDE_SELL;
        order_req.order_type = ORDER_TYPE_LIMIT;  // 使用限价单
        order_req.qty = slice.quantity;
        order_req.price = current_price;  // 使用实时获取的价格
        order_req.batch_id = 0; // 或者生成一个批次ID

        // 提交订单
        int64_t order_id = _trade_handler->send_order(&order_req);

        if (order_id <= 0)
        {
            LOG_ERROR("Failed to send order, returned order_id: {}", order_id);
        }
        else
        {
            LOG_INFO("TWAP order sent: security={}, quantity={}, price={:.3f}, order_id={}", 
                     task.security_code, slice.quantity, current_price, order_id);
        }

        return order_id;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in execute_sell_order: {}", e.what());
        return 0;
    }
}

std::string TwapAlgorithm::_format_time_point(const std::chrono::system_clock::time_point &tp) const
{
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

NS_FT_END

