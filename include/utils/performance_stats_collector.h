#ifndef PERFORMANCE_STATS_COLLECTOR_H
#define PERFORMANCE_STATS_COLLECTOR_H

#include <string>
#include <vector>
#include <unordered_map>

namespace funiq_trading {

/**
 * 性能统计辅助类
 * 记录每次执行的详细信息，并在达到样本数后输出汇总统计
 */
class PerformanceStatsCollector {
public:
    struct StageRecord {
        std::string stage_name;
        int64_t stage_elapsed_ns;
        int64_t total_elapsed_ns;
    };

    struct OperationStats {
        std::vector<std::vector<StageRecord>> samples;  // 每次操作的所有阶段记录
        int64_t total_operations = 0;                   // 总操作次数

        // 每个阶段的累计统计
        std::unordered_map<std::string, int64_t> stage_total_ns;
        std::unordered_map<std::string, int> stage_count;
    };

    PerformanceStatsCollector(int sample_count = 5)
        : _sample_count(sample_count) {}

    // 记录一个阶段
    void record_stage(const std::string& operation_name,
                     const std::string& stage_name,
                     int64_t stage_elapsed_ns,
                     int64_t total_elapsed_ns,
                     const std::string& log_prefix);

    void record_total(const std::string& operation_name,
                      int64_t stage_elapsed_ns,
                      int64_t total_elapsed_ns,
                      const std::string& log_prefix);
                      
    void record_end(const std::string& operation_name);

private:
    int _sample_count;
    std::unordered_map<std::string, OperationStats> _stats;
    std::unordered_map<std::string, std::vector<StageRecord>> _current_operation_stages;

    // 输出单次操作的性能日志
    void log_single_operation(const std::string& operation_name,
                             const std::vector<StageRecord>& stages,
                             const std::string& log_prefix);

    // 输出汇总统计
    void log_summary(const std::string& operation_name,
                    const OperationStats& stats,
                    const std::string& log_prefix);
};

} // namespace funiq_trading

#endif // PERFORMANCE_STATS_COLLECTOR_H
