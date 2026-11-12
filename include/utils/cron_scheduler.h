#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <ctime>
#include "utils/croncpp.h"
#include "core/macros.h"

namespace ft {

class CronScheduler {
public:
    using JobId = uint64_t;
    using JobCallback = std::function<void()>;

    CronScheduler(boost::asio::io_context* external_ioc = nullptr);
    ~CronScheduler();

    bool start();
    void stop();
    bool is_running() const { return _running.load(); }

    JobId add_job(const std::string& name, const std::string& cron_expr, JobCallback callback);
    bool remove_job(JobId job_id);
    bool set_job_enabled(JobId job_id, bool enabled);
    bool execute_job_now(JobId job_id);
    std::string get_jobs_info() const;

private:
    struct CronJob {
        std::string name;
        std::string cron_expression;
        JobCallback callback;
        cron::cronexpr cron_expr;
        std::unique_ptr<boost::asio::system_timer> timer;
        bool enabled = true;
        std::time_t last_execution = 0;
        std::time_t next_execution = 0;
    };

    void schedule_job(JobId job_id, CronJob& job);
    void execute_job(JobId job_id, CronJob& job);
    void update_next_execution(CronJob& job);
    std::string format_time(std::time_t t) const;

    std::atomic<bool> _running{false};
    std::unique_ptr<boost::asio::io_context> _owned_ioc;
    boost::asio::io_context* _ioc;
    std::unique_ptr<std::thread> _worker_thread;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> _work_guard;
    
    JobId _next_job_id = 1;
    std::unordered_map<JobId, CronJob> _jobs;
    mutable std::mutex _jobs_mutex;
};

} // namespace ft