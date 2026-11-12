#include "utils/performance_stats_collector.h"
#include "logger/logger.hpp"
#include <sstream>

namespace funiq_trading {

void PerformanceStatsCollector::record_total(const std::string& operation_name,
                                            int64_t stage_elapsed_ns,
                                             int64_t total_elapsed_ns,
                                             const std::string& log_prefix) {
    record_stage(operation_name, "__total__", stage_elapsed_ns, total_elapsed_ns, log_prefix);
    auto& current_stages = _current_operation_stages[operation_name];

    // 输出单次操作的详细日志
    log_single_operation(operation_name, current_stages, log_prefix);

    // 添加到统计数据
    auto& stats = _stats[operation_name];
    stats.samples.push_back(current_stages);
    stats.total_operations++;

    // 累计各阶段统计
    for (const auto& stage : current_stages)
    {
        stats.stage_total_ns[stage.stage_name] += stage.stage_elapsed_ns;
        stats.stage_count[stage.stage_name]++;
    }

    // 达到样本数，输出汇总统计
    if (stats.total_operations >= _sample_count)
    {
        log_summary(operation_name, stats, log_prefix);

        // 重置统计
        stats.samples.clear();
        stats.total_operations = 0;
        stats.stage_total_ns.clear();
        stats.stage_count.clear();
    }
}

void PerformanceStatsCollector::record_end(const std::string& operation_name) {
    // 清空当前操作的阶段记录
    _current_operation_stages[operation_name].clear();
}
void PerformanceStatsCollector::record_stage(const std::string& operation_name,
                                             const std::string& stage_name,
                                             int64_t stage_elapsed_ns,
                                             int64_t total_elapsed_ns,
                                             const std::string& log_prefix)
{
    // 记录当前阶段
    StageRecord record{stage_name, stage_elapsed_ns, total_elapsed_ns};
    _current_operation_stages[operation_name].push_back(record);
}

void PerformanceStatsCollector::log_single_operation(const std::string& operation_name,
                                                     const std::vector<StageRecord>& stages,
                                                     const std::string& log_prefix)
{
    std::ostringstream oss;
    oss << log_prefix << " [PERF] " << operation_name << ": ";

    for (size_t i = 0; i < stages.size(); ++i)
    {
        const auto& stage = stages[i];
        if (i > 0)
            oss << ", ";

        oss << stage.stage_name << "=" << stage.stage_elapsed_ns << "ns";

        // 对于非 total 阶段，显示累计时间
        if (stage.stage_name != "total")
        {
            oss << "(累计:" << stage.total_elapsed_ns << "ns)";
        }
    }

    STRATEGY_LOG_INFO("{}", oss.str());
}

void PerformanceStatsCollector::log_summary(const std::string& operation_name,
                                           const OperationStats& stats,
                                           const std::string& log_prefix)
{
    std::ostringstream oss;
    oss << log_prefix << " [PERF_SUMMARY] " << operation_name
        << " (samples=" << stats.total_operations << "): ";

    bool first = true;
    for (const auto& pair : stats.stage_total_ns)
    {
        const std::string& stage_name = pair.first;
        int64_t total_ns = pair.second;
        int count = stats.stage_count.at(stage_name);
        int64_t avg_ns = total_ns / count;

        if (!first)
            oss << ", ";
        first = false;

        oss << stage_name << "_avg=" << avg_ns << "ns"
            << "(total=" << total_ns << "ns)";
    }

    STRATEGY_LOG_INFO("{}", oss.str());
}

} // namespace funiq_trading
