#include "utils/cron_scheduler.h"
#include "logger/logger.hpp"
#include <sstream>
#include <ctime>
#include <iomanip>

NS_FT_BEGIN

CronScheduler::CronScheduler(boost::asio::io_context* external_ioc) 
    : _ioc(external_ioc) {
    if (!_ioc) {
        _owned_ioc = std::make_unique<boost::asio::io_context>();
        _ioc = _owned_ioc.get();
    }
}

CronScheduler::~CronScheduler() {
    stop();
}

bool CronScheduler::start() {
    if (_running.load()) {
        SCHEDULER_LOG_WARN("CronScheduler is already running");
        return true;
    }

    try {
        _running.store(true);
        
        if (_owned_ioc) {
            // 创建工作守护，防止io_context退出
            _work_guard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
                boost::asio::make_work_guard(*_ioc));
            
            // 启动工作线程
            _worker_thread = std::make_unique<std::thread>([this]() {
                SCHEDULER_LOG_INFO("CronScheduler worker thread started");
                try {
                    _ioc->run();
                } catch (const std::exception& e) {
                    SCHEDULER_LOG_ERROR("CronScheduler worker thread error: {}", e.what());
                }
                SCHEDULER_LOG_INFO("CronScheduler worker thread stopped");
            });
        }

        SCHEDULER_LOG_INFO("CronScheduler started successfully");
        return true;
    } catch (const std::exception& e) {
        SCHEDULER_LOG_ERROR("Failed to start CronScheduler: {}", e.what());
        _running.store(false);
        return false;
    }
}

void CronScheduler::stop() {
    if (!_running.load()) {
        return;
    }

    SCHEDULER_LOG_INFO("Stopping CronScheduler...");
    _running.store(false);

    // 清理所有定时器
    {
        std::lock_guard<std::mutex> lock(_jobs_mutex);
        for (auto& entry : _jobs) {
            auto& job = entry.second;
            if (job.timer) {
                job.timer->cancel();
            }
        }
    }

    // 停止工作线程
    if (_owned_ioc) {
        _work_guard.reset();
        if (_worker_thread && _worker_thread->joinable()) {
            _worker_thread ->join();
        }
        _worker_thread.reset();
    }

    SCHEDULER_LOG_INFO("CronScheduler stopped");
}

CronScheduler::JobId CronScheduler::add_job(const std::string& name, 
                                           const std::string& cron_expr,
                                           JobCallback callback) {
    try {
        // 验证 cron 表达式
        auto cron_expression = cron::make_cron(cron_expr);

        std::lock_guard<std::mutex> lock(_jobs_mutex);

        JobId job_id = _next_job_id++;
        CronJob job;
        job.name = name;
        job.cron_expression = cron_expr;
        job.callback = std::move(callback);
        job.cron_expr = cron_expression;
        job.timer = std::make_unique<boost::asio::system_timer>(*_ioc);
        job.enabled = true;

        // 计算下次执行时间并调度
        update_next_execution(job);
        
        _jobs[job_id] = std::move(job);
        
        if (_running.load()) {
            schedule_job(job_id, _jobs[job_id]);
        }

        SCHEDULER_LOG_INFO("Added cron job '{}' (ID: {}) with expression '{}', next execution: {}",
                           name, job_id, cron_expr, format_time(job.next_execution));

        return job_id;
    } catch (const std::exception& e) {
        SCHEDULER_LOG_ERROR("Failed to add cron job '{}': {}", name, e.what());
        return 0;
    }
}

bool CronScheduler::remove_job(JobId job_id) {
    std::lock_guard<std::mutex> lock(_jobs_mutex);

    auto it = _jobs.find(job_id);
    if (it == _jobs.end()) {
        SCHEDULER_LOG_WARN("Cron job ID {} not found", job_id);
        return false;
    }

    std::string name = it->second.name;
    
    // 取消定时器
    if (it->second.timer) {
        it->second.timer->cancel();
    }
    
    _jobs.erase(it);

    SCHEDULER_LOG_INFO("Removed cron job '{}' (ID: {})", name, job_id);
    return true;
}

bool CronScheduler::set_job_enabled(JobId job_id, bool enabled) {
    std::lock_guard<std::mutex> lock(_jobs_mutex);

    auto it = _jobs.find(job_id);
    if (it == _jobs.end()) {
        SCHEDULER_LOG_WARN("Cron job ID {} not found", job_id);
        return false;
    }

    it->second.enabled = enabled;

    if (enabled && _running.load()) {
        // 重新计算下次执行时间并调度
        update_next_execution(it->second);
        schedule_job(job_id, it->second);
    } else if (!enabled && it->second.timer) {
        // 取消定时器
        it->second.timer->cancel();
    }

    SCHEDULER_LOG_INFO("Cron job '{}' (ID: {}) {}", it->second.name, job_id,
                       enabled ? "enabled" : "disabled");
    return true;
}

bool CronScheduler::execute_job_now(JobId job_id) {
    std::lock_guard<std::mutex> lock(_jobs_mutex);

    auto it = _jobs.find(job_id);
    if (it == _jobs.end()) {
        SCHEDULER_LOG_WARN("Cron job ID {} not found", job_id);
        return false;
    }

    if (!it->second.enabled) {
        SCHEDULER_LOG_WARN("Cron job '{}' is disabled", it->second.name);
        return false;
    }

    SCHEDULER_LOG_INFO("Manually executing cron job '{}'", it->second.name);
    
    // 异步执行
    boost::asio::post(*_ioc, [this, job_id]() {
        std::lock_guard<std::mutex> lock(_jobs_mutex);
        auto it = _jobs.find(job_id);
        if (it != _jobs.end()) {
            execute_job(job_id, it->second);
        }
    });
    
    return true;
}

void CronScheduler::schedule_job(JobId job_id, CronJob& job) {
    if (!job.enabled || !job.timer) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto next_time = std::chrono::system_clock::from_time_t(job.next_execution);
    
    // 增加最小间隔保护
    auto min_interval = std::chrono::seconds(1);
    if (next_time <= now + min_interval) {
        SCHEDULER_LOG_WARN("Cron job '{}' next execution time too close, delaying by 1 second", job.name);
        next_time = now + min_interval;
        job.next_execution = std::chrono::system_clock::to_time_t(next_time);
    }

    // 设置定时器
    job.timer->expires_at(next_time);
    job.timer->async_wait([this, job_id](const boost::system::error_code& ec) {
        if (!ec && _running.load()) {
            std::lock_guard<std::mutex> lock(_jobs_mutex);
            auto it = _jobs.find(job_id);
            if (it != _jobs.end()) {
                execute_job(job_id, it->second);
            }
        }
    });
}

void CronScheduler::execute_job(JobId job_id, CronJob& job) {
    if (!job.enabled) {
        return;
    }

    SCHEDULER_LOG_INFO("Executing cron job: {} (expression: {})", job.name, job.cron_expression);

    auto start_time = std::chrono::steady_clock::now();

    try {
        job.callback();
        job.last_execution = std::time(nullptr);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        SCHEDULER_LOG_INFO("Cron job '{}' completed in {}ms", job.name, duration.count());
    } catch (const std::exception& e) {
        SCHEDULER_LOG_ERROR("Cron job '{}' failed with exception: {}", job.name, e.what());
    } catch (...) {
        SCHEDULER_LOG_ERROR("Cron job '{}' failed with unknown exception", job.name);
    }

    // 重新调度下次执行
    if (_running.load() && job.enabled) {
        update_next_execution(job);
        schedule_job(job_id, job);
    }
}

void CronScheduler::update_next_execution(CronJob& job) {
    auto current_time = std::time(nullptr);
    auto next_time = cron::cron_next(job.cron_expr, current_time);
    
    // 确保下次执行时间至少比当前时间晚1秒
    if (next_time <= current_time) {
        next_time = cron::cron_next(job.cron_expr, current_time + 1);
    }
    
    job.next_execution = next_time;
}

std::string CronScheduler::format_time(std::time_t t) const {
    if (t == 0) {
        return "never";
    }

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string CronScheduler::get_jobs_info() const {
    std::lock_guard<std::mutex> lock(_jobs_mutex);

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"scheduler_running\": " << (_running.load() ? "true" : "false") << ",\n";
    oss << "  \"job_count\": " << _jobs.size() << ",\n";
    oss << "  \"jobs\": [\n";

    bool first = true;
    for (const auto& [job_id, job] : _jobs) {
        if (!first) oss << ",\n";
        first = false;

        oss << "    {\n";
        oss << "      \"id\": " << job_id << ",\n";
        oss << "      \"name\": \"" << job.name << "\",\n";
        oss << "      \"cron_expression\": \"" << job.cron_expression << "\",\n";
        oss << "      \"enabled\": " << (job.enabled ? "true" : "false") << ",\n";
        oss << "      \"next_execution\": \"" << format_time(job.next_execution) << "\"";

        if (job.last_execution > 0) {
            oss << ",\n      \"last_execution\": \"" << format_time(job.last_execution) << "\"";
        }

        oss << "\n    }";
    }

    oss << "\n  ]\n}";
    return oss.str();
}


NS_FT_END
