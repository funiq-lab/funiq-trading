#ifndef TRADING_TIME_H
#define TRADING_TIME_H

#include <chrono>
#include <vector>
#include <string>
#include <optional>
#include <unordered_set>
#include <mutex>
#include "core/macros.h"

NS_FT_BEGIN

/**
 * 交易时间段结构
 */
struct TradingSession {
    int start_hour;
    int start_minute;
    int end_hour;
    int end_minute;
    
    TradingSession(int sh, int sm, int eh, int em) 
        : start_hour(sh), start_minute(sm), end_hour(eh), end_minute(em) {}
};

/**
 * 交易时间配置
 */
struct TradingTimeConfig {
    std::vector<TradingSession> trading_sessions;   // 交易时间段
    std::vector<int> trading_weekdays;              // 交易日（1-7，周一到周日）
    std::unordered_set<std::string> holidays;       // 节假日集合 (格式: "2024-01-01")
    bool is_24x7;                                   // 是否7*24小时模式
    bool no_holiday;                                // 是否忽略节假日
    
    TradingTimeConfig() 
        : is_24x7(true)       // 默认7*24小时模式
        , no_holiday(true)    // 默认忽略节假日
    {
        // 默认交易日：全周
        trading_weekdays = {1, 2, 3, 4, 5, 6, 7};
    }
    
    /**
     * 添加节假日
     * @param date 日期字符串，格式："2024-01-01"
     */
    void add_holiday(const std::string& date) {
        holidays.insert(date);
    }
    
    /**
     * 移除节假日
     * @param date 日期字符串，格式："2024-01-01"
     */
    void remove_holiday(const std::string& date) {
        holidays.erase(date);
    }
    
    /**
     * 批量添加节假日
     * @param holiday_list 节假日列表
     */
    void add_holidays(const std::vector<std::string>& holiday_list) {
        for (const auto& date : holiday_list) {
            holidays.insert(date);
        }
    }
    
    /**
     * 清空节假日
     */
    void clear_holidays() {
        holidays.clear();
    }
};

/**
 * 交易时间工具类
 * 
 * 功能：
 * 1. 交易时间判断：检查指定时间是否为交易时间
 * 2. 时间调整：将任意时间调整到最近的交易时间
 * 3. 交易日管理：判断交易日、节假日
 * 4. 时间计算：获取下一个/上一个交易时间
 * 5. 配置管理：支持灵活的交易时间配置
 * 
 * 使用场景：
 * - 策略算法中的时间控制
 * - 订单调度的时间判断
 * - 系统任务的时间规划
 * - 报告生成的时间筛选
 * 
 * 控制模式：
 * - is_24x7: true表示7*24小时模式，false表示按交易时间段运行
 * - no_holiday: true表示忽略节假日，false表示遵守节假日
 * 
 * 线程安全：所有方法都是线程安全的
 */
class TradingTimeManager
{
public:
    /**
     * 构造函数
     * @param config 交易时间配置（可选，默认为7*24小时模式）
     */
    explicit TradingTimeManager(const TradingTimeConfig& config = TradingTimeConfig());
    
    /**
     * 析构函数
     */
    ~TradingTimeManager() = default;

    // ===== 配置管理 =====
    
    /**
     * 设置交易时间配置
     * @param config 交易时间配置
     */
    void set_config(const TradingTimeConfig& config);
    
    /**
     * 获取交易时间配置
     * @return 交易时间配置的常引用
     */
    const TradingTimeConfig& get_config() const;

    /**
     * 从配置文件加载完整的交易时间配置
     * @param file_path 配置文件路径
     * @return 成功返回0，失败返回负数
     */
    int load_config_from_file(const std::string& file_path);

    /**
     * 从环境变量获取配置文件路径并加载配置
     * @param env_name 环境变量名称（默认为"SYSTEM_CONFIG_FILE"）
     * @return 成功返回0，失败返回负数
     */
    int load_config_from_env(const std::string& env_name = "SYSTEM_CONFIG_FILE");

    /**
     * 加载节假日配置文件
     * @param file_path 配置文件路径
     * @return 成功返回0，失败返回负数
     */
    int load_holidays_from_file(const std::string& file_path);

    /**
     * 获取最后一次的错误信息
     * @return 错误信息字符串
     */
    const std::string& get_last_error() const;

    // ===== 核心判断方法 =====
    
    /**
     * 检查指定时间是否在交易时间内
     * @param time_point 时间点
     * @return true在交易时间内，false不在交易时间内
     */
    bool is_trading_time(const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 检查是否为交易日
     * @param time_point 时间点
     * @return true为交易日，false为非交易日
     */
    bool is_trading_day(const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 检查是否为节假日
     * @param time_point 时间点
     * @return true为节假日，false为非节假日
     */
    bool is_holiday(const std::chrono::system_clock::time_point& time_point) const;

    // ===== 时间计算方法 =====
    
    /**
     * 获取下一个交易时间
     * @param from_time 起始时间
     * @return 下一个交易时间点
     */
    std::chrono::system_clock::time_point get_next_trading_time(
        const std::chrono::system_clock::time_point& from_time) const;
    
    /**
     * 获取上一个交易时间
     * @param from_time 起始时间
     * @return 上一个交易时间点
     */
    std::chrono::system_clock::time_point get_previous_trading_time(
        const std::chrono::system_clock::time_point& from_time) const;
    
    /**
     * 调整时间到交易时间内
     * @param time_point 原时间点
     * @param forward 是否向前调整（true向前，false向后）
     * @return 调整后的交易时间点
     */
    std::chrono::system_clock::time_point adjust_to_trading_time(
        const std::chrono::system_clock::time_point& time_point, 
        bool forward = true) const;
    
    /**
     * 获取指定日期的交易时间段列表
     * @param date 日期
     * @return 当日的交易时间段列表，如果不是交易日则返回空列表
     */
    std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>> 
    get_trading_sessions_for_date(const std::chrono::system_clock::time_point& date) const;

    // ===== 时间区间方法 =====
    
    /**
     * 计算两个时间点之间的交易时间长度
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @return 交易时间长度（秒）
     */
    std::chrono::seconds calculate_trading_duration(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;
    
    /**
     * 获取指定时间范围内的所有交易时间段
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @return 交易时间段列表
     */
    std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>> 
    get_trading_sessions_in_range(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;

private:
    TradingTimeConfig _config;
    mutable std::mutex _mutex;  // 保护配置的读写
    mutable std::string _last_error;  // 最后的错误信息

    // ===== 内部辅助方法 =====
    
    /**
     * 检查时间是否在交易时段内
     * @param hour 小时
     * @param minute 分钟
     * @return true在交易时段内，false不在交易时段内
     */
    bool _is_in_trading_session(int hour, int minute) const;
    
    /**
     * 获取当天的下一个交易时段开始时间
     * @param time_point 当前时间
     * @return 下一个交易时段开始时间，如果没有则返回空
     */
    std::optional<std::chrono::system_clock::time_point> _get_next_session_today(
        const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 获取当天的上一个交易时段结束时间
     * @param time_point 当前时间
     * @return 上一个交易时段结束时间，如果没有则返回空
     */
    std::optional<std::chrono::system_clock::time_point> _get_previous_session_today(
        const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 获取下一个交易日的第一个交易时段开始时间
     * @param time_point 当前时间
     * @return 下一个交易日的第一个交易时段开始时间
     */
    std::chrono::system_clock::time_point _get_next_trading_day_start(
        const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 获取上一个交易日的最后一个交易时段结束时间
     * @param time_point 当前时间
     * @return 上一个交易日的最后一个交易时段结束时间
     */
    std::chrono::system_clock::time_point _get_previous_trading_day_end(
        const std::chrono::system_clock::time_point& time_point) const;

    // ===== 时间工具方法 =====
    
    /**
     * 时间点转日期字符串
     * @param time_point 时间点
     * @return 日期字符串 (格式: "2024-01-01")
     */
    std::string _time_point_to_date_string(const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 获取时间点的小时和分钟
     * @param time_point 时间点
     * @param hour 输出小时
     * @param minute 输出分钟
     */
    void _get_hour_minute(const std::chrono::system_clock::time_point& time_point, int& hour, int& minute) const;
    
    /**
     * 获取时间点的星期几
     * @param time_point 时间点
     * @return 星期几（1-7，周一到周日）
     */
    int _get_weekday(const std::chrono::system_clock::time_point& time_point) const;
    
    /**
     * 构造指定日期和时间的时间点
     * @param base_time 基准时间（用于获取日期）
     * @param hour 小时
     * @param minute 分钟
     * @return 构造的时间点
     */
    std::chrono::system_clock::time_point _make_time_point(
        const std::chrono::system_clock::time_point& base_time, int hour, int minute) const;

    /**
     * 设置错误信息
     * @param error 错误信息
     */
    void _set_error(const std::string& error) const;
};

NS_FT_END

#endif // TRADING_TIME_H