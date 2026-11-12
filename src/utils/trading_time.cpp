#include "utils/trading_time.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "laserpants/dotenv/dotenv.h"

NS_FT_BEGIN

TradingTimeManager::TradingTimeManager(const TradingTimeConfig& config)
    : _config(config)
{
}

// ===== 配置管理 =====

void TradingTimeManager::set_config(const TradingTimeConfig& config)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _config = config;
}

const TradingTimeConfig& TradingTimeManager::get_config() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _config;
}

int TradingTimeManager::load_config_from_file(const std::string& file_path)
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (file_path.empty()) {
        _set_error("Config file path is empty");
        return -1;
    }
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            _set_error("Failed to open config file: " + file_path);
            return -1;
        }
        
        nlohmann::json config_json;
        file >> config_json;
        
        // 创建新的配置对象
        TradingTimeConfig new_config;
        new_config.trading_sessions.clear();  // 清空默认配置
        
        // 解析交易时间配置
        if (config_json.contains("trading_time") && config_json["trading_time"].is_array()) {
            new_config.is_24x7 = false;  // 有交易时间配置，不是7*24小时
            
            for (const auto& session : config_json["trading_time"]) {
                if (session.is_array() && session.size() == 4) {
                    int start_hour = session[0];
                    int start_minute = session[1];
                    int end_hour = session[2];
                    int end_minute = session[3];
                    
                    // 验证时间范围
                    if (start_hour < 0 || start_hour > 23 || start_minute < 0 || start_minute > 59 ||
                        end_hour < 0 || end_hour > 23 || end_minute < 0 || end_minute > 59) {
                        _set_error("Invalid time range in trading session");
                        return -1;
                    }
                    
                    new_config.trading_sessions.emplace_back(start_hour, start_minute, end_hour, end_minute);
                }
            }
        } else {
            // 没有交易时间配置，保持7*24小时模式
            new_config.is_24x7 = true;
        }
        
        // 解析节假日配置
        if (config_json.contains("holiday") && config_json["holiday"].is_array()) {
            new_config.no_holiday = false;  // 有节假日配置，不忽略节假日
            new_config.clear_holidays();
            
            for (const auto& holiday : config_json["holiday"]) {
                if (holiday.is_string()) {
                    std::string date_str = holiday.get<std::string>();
                    // 简单验证日期格式 (YYYY-MM-DD)
                    if (date_str.length() == 10 && date_str[4] == '-' && date_str[7] == '-') {
                        new_config.add_holiday(date_str);
                    }
                }
            }
        } else {
            // 没有节假日配置，忽略节假日
            new_config.no_holiday = true;
        }
        
        // 解析交易日配置（可选）
        if (config_json.contains("trading_weekdays") && config_json["trading_weekdays"].is_array()) {
            new_config.trading_weekdays.clear();
            for (const auto& weekday : config_json["trading_weekdays"]) {
                if (weekday.is_number_integer()) {
                    int day = weekday.get<int>();
                    if (day >= 1 && day <= 7) {
                        new_config.trading_weekdays.push_back(day);
                    }
                }
            }
            
            // 如果配置文件中的 trading_weekdays 为空，则使用默认值 1-7
            if (new_config.trading_weekdays.empty()) {
                new_config.trading_weekdays = {1, 2, 3, 4, 5, 6, 7};
            }
        } else {
            // 没有 trading_weekdays 配置，使用默认值 1-7
            new_config.trading_weekdays = {1, 2, 3, 4, 5, 6, 7};
        }
        
        // 更新配置
        _config = new_config;
        
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Failed to parse config file: " + std::string(e.what()));
        return -1;
    }
}

int TradingTimeManager::load_config_from_env(const std::string& env_name)
{
    // 加载 .env 文件
    dotenv::init();
    
    const char* config_path_env = std::getenv(env_name.c_str());
    if (!config_path_env) {
        _set_error("Environment variable '" + env_name + "' not found");
        return -1;
    }
    
    std::string config_path = std::string(config_path_env);
    return load_config_from_file(config_path);
}

int TradingTimeManager::load_holidays_from_file(const std::string& file_path)
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            _set_error("Failed to open holidays file: " + file_path);
            return -1;
        }
        
        _config.no_holiday = false;  // 有节假日文件，不忽略节假日
        
        std::string line;
        while (std::getline(file, line)) {
            // 去除空白字符
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            // 跳过空行和注释行
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // 简单验证日期格式 (YYYY-MM-DD)
            if (line.length() == 10 && line[4] == '-' && line[7] == '-') {
                _config.add_holiday(line);
            }
        }
        
        return 0;
    } catch (const std::exception& e) {
        _set_error("Failed to load holidays file: " + std::string(e.what()));
        return -1;
    }
}

const std::string& TradingTimeManager::get_last_error() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_error;
}

// ===== 核心判断方法 =====

bool TradingTimeManager::is_trading_time(const std::chrono::system_clock::time_point& time_point) const
{
    // 移除锁 - 只是读取操作
    if (_config.is_24x7) {
        return true;
    }
    
    if (!is_trading_day(time_point)) {
        return false;
    }
    
    int hour, minute;
    _get_hour_minute(time_point, hour, minute);
    
    return _is_in_trading_session(hour, minute);
}

bool TradingTimeManager::is_trading_day(const std::chrono::system_clock::time_point& time_point) const
{
    // 移除锁 - 只是读取操作
    if (_config.is_24x7) {
        return true;
    }
    
    if (is_holiday(time_point)) {
        return false;
    }
    
    int weekday = _get_weekday(time_point);
    return std::find(_config.trading_weekdays.begin(), _config.trading_weekdays.end(), weekday) 
           != _config.trading_weekdays.end();
}

bool TradingTimeManager::is_holiday(const std::chrono::system_clock::time_point& time_point) const
{
    // 移除锁 - 只是读取操作
    if (_config.no_holiday) {
        return false;
    }
    
    std::string date_str = _time_point_to_date_string(time_point);
    return _config.holidays.find(date_str) != _config.holidays.end();
}

// ===== 时间计算方法 =====

std::chrono::system_clock::time_point TradingTimeManager::get_next_trading_time(
    const std::chrono::system_clock::time_point& from_time) const
{
    // 移除锁 - 只是读取操作
    if (_config.is_24x7) {
        return from_time;
    }
    
    auto current_time = from_time;
    
    if (is_trading_time(current_time)) {
        return current_time;
    }
    
    if (is_trading_day(current_time)) {
        auto next_session = _get_next_session_today(current_time);
        if (next_session.has_value()) {
            return next_session.value();
        }
    }
    
    return _get_next_trading_day_start(current_time);
}

std::chrono::system_clock::time_point TradingTimeManager::get_previous_trading_time(
    const std::chrono::system_clock::time_point& from_time) const
{
    // 移除锁 - 只是读取操作
    if (_config.is_24x7) {
        return from_time;
    }
    
    auto current_time = from_time;
    
    if (is_trading_time(current_time)) {
        return current_time;
    }
    
    if (is_trading_day(current_time)) {
        auto prev_session = _get_previous_session_today(current_time);
        if (prev_session.has_value()) {
            return prev_session.value();
        }
    }
    
    return _get_previous_trading_day_end(current_time);
}

std::chrono::system_clock::time_point TradingTimeManager::adjust_to_trading_time(
    const std::chrono::system_clock::time_point& time_point, 
    bool forward) const
{
    if (is_trading_time(time_point)) {
        return time_point;
    }
    
    if (forward) {
        return get_next_trading_time(time_point);
    } else {
        return get_previous_trading_time(time_point);
    }
}

std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>>
TradingTimeManager::get_trading_sessions_for_date(const std::chrono::system_clock::time_point& date) const
{
    // 移除锁 - 只是读取操作
    std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>> sessions;
    
    if (_config.is_24x7) {
        auto start_time = _make_time_point(date, 0, 0);
        auto end_time = _make_time_point(date, 23, 59);
        sessions.emplace_back(start_time, end_time);
        return sessions;
    }
    
    if (!is_trading_day(date)) {
        return sessions;
    }
    
    for (const auto& session : _config.trading_sessions) {
        auto start_time = _make_time_point(date, session.start_hour, session.start_minute);
        auto end_time = _make_time_point(date, session.end_hour, session.end_minute);
        sessions.emplace_back(start_time, end_time);
    }
    
    return sessions;
}

// ===== 时间区间方法 =====

std::chrono::seconds TradingTimeManager::calculate_trading_duration(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time) const
{
    if (start_time >= end_time) {
        return std::chrono::seconds(0);
    }
    
    // 如果是7*24小时模式，直接返回时间差
    if (_config.is_24x7) {
        return std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    }
    
    std::chrono::seconds total_duration(0);
    
    auto sessions = get_trading_sessions_in_range(start_time, end_time);
    
    for (const auto& session : sessions) {
        auto session_start = std::max(start_time, session.first);
        auto session_end = std::min(end_time, session.second);
        
        if (session_start < session_end) {
            total_duration += std::chrono::duration_cast<std::chrono::seconds>(session_end - session_start);
        }
    }
    
    return total_duration;
}

std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>>
TradingTimeManager::get_trading_sessions_in_range(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time) const
{
    std::vector<std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>> sessions;
    
    if (start_time >= end_time) {
        return sessions;
    }
    
    // 如果是7*24小时模式，返回整个时间段
    if (_config.is_24x7) {
        sessions.emplace_back(start_time, end_time);
        return sessions;
    }
    
    // 从开始时间开始，逐日检查
    auto current_date = start_time;
    auto end_date = end_time;
    
    while (current_date <= end_date) {
        auto daily_sessions = get_trading_sessions_for_date(current_date);
        
        for (const auto& session : daily_sessions) {
            // 检查会话是否与时间范围重叠
            if (session.first < end_time && session.second > start_time) {
                sessions.push_back(session);
            }
        }
        
        // 移动到下一天
        current_date += std::chrono::hours(24);
    }
    
    return sessions;
}

// ===== 内部辅助方法 =====

bool TradingTimeManager::_is_in_trading_session(int hour, int minute) const
{
    // 如果是7*24小时模式，直接返回true
    if (_config.is_24x7) {
        return true;
    }
    
    int current_minutes = hour * 60 + minute;
    
    for (const auto& session : _config.trading_sessions) {
        int start_minutes = session.start_hour * 60 + session.start_minute;
        int end_minutes = session.end_hour * 60 + session.end_minute;
        
        if (current_minutes >= start_minutes && current_minutes < end_minutes) {
            return true;
        }
    }
    
    return false;
}

std::optional<std::chrono::system_clock::time_point> TradingTimeManager::_get_next_session_today(
    const std::chrono::system_clock::time_point& time_point) const
{
    // 如果是7*24小时模式，直接返回当前时间
    if (_config.is_24x7) {
        return time_point;
    }
    
    int current_hour, current_minute;
    _get_hour_minute(time_point, current_hour, current_minute);
    int current_minutes = current_hour * 60 + current_minute;
    
    for (const auto& session : _config.trading_sessions) {
        int start_minutes = session.start_hour * 60 + session.start_minute;
        
        if (current_minutes < start_minutes) {
            return _make_time_point(time_point, session.start_hour, session.start_minute);
        }
    }
    
    return std::nullopt;
}

std::optional<std::chrono::system_clock::time_point> TradingTimeManager::_get_previous_session_today(
    const std::chrono::system_clock::time_point& time_point) const
{
    // 如果是7*24小时模式，直接返回当前时间
    if (_config.is_24x7) {
        return time_point;
    }
    
    int current_hour, current_minute;
    _get_hour_minute(time_point, current_hour, current_minute);
    int current_minutes = current_hour * 60 + current_minute;
    
    // 从后往前查找
    for (auto it = _config.trading_sessions.rbegin(); it != _config.trading_sessions.rend(); ++it) {
        int end_minutes = it->end_hour * 60 + it->end_minute;
        
        if (current_minutes > end_minutes) {
            return _make_time_point(time_point, it->end_hour, it->end_minute);
        }
    }
    
    return std::nullopt;
}

std::chrono::system_clock::time_point TradingTimeManager::_get_next_trading_day_start(
    const std::chrono::system_clock::time_point& time_point) const
{
    // 如果是7*24小时模式，直接返回当前时间
    if (_config.is_24x7) {
        return time_point;
    }
    
    auto next_day = time_point + std::chrono::hours(24);
    
    // 最多查找30天
    for (int i = 0; i < 30; ++i) {
        if (is_trading_day(next_day)) {
            if (!_config.trading_sessions.empty()) {
                const auto& first_session = _config.trading_sessions.front();
                return _make_time_point(next_day, first_session.start_hour, first_session.start_minute);
            }
        }
        next_day += std::chrono::hours(24);
    }
    
    // 如果30天内都没有交易日，返回当前时间
    return time_point;
}

std::chrono::system_clock::time_point TradingTimeManager::_get_previous_trading_day_end(
    const std::chrono::system_clock::time_point& time_point) const
{
    // 如果是7*24小时模式，直接返回当前时间
    if (_config.is_24x7) {
        return time_point;
    }
    
    auto prev_day = time_point - std::chrono::hours(24);
    
    // 最多查找30天
    for (int i = 0; i < 30; ++i) {
        if (is_trading_day(prev_day)) {
            if (!_config.trading_sessions.empty()) {
                const auto& last_session = _config.trading_sessions.back();
                return _make_time_point(prev_day, last_session.end_hour, last_session.end_minute);
            }
        }
        prev_day -= std::chrono::hours(24);
    }
    
    // 如果30天内都没有交易日，返回当前时间
    return time_point;
}

// ===== 时间工具方法 =====

std::string TradingTimeManager::_time_point_to_date_string(const std::chrono::system_clock::time_point& time_point) const
{
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    std::tm* tm = std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}

void TradingTimeManager::_get_hour_minute(const std::chrono::system_clock::time_point& time_point, int& hour, int& minute) const
{
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    std::tm* tm = std::localtime(&time_t);
    
    hour = tm->tm_hour;
    minute = tm->tm_min;
}

int TradingTimeManager::_get_weekday(const std::chrono::system_clock::time_point& time_point) const
{
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    std::tm* tm = std::localtime(&time_t);
    
    // tm_wday: 0=Sunday, 1=Monday, ..., 6=Saturday
    // 转换为 1=Monday, 2=Tuesday, ..., 7=Sunday
    return (tm->tm_wday == 0) ? 7 : tm->tm_wday;
}

std::chrono::system_clock::time_point TradingTimeManager::_make_time_point(
    const std::chrono::system_clock::time_point& base_time, int hour, int minute) const
{
    auto time_t = std::chrono::system_clock::to_time_t(base_time);
    std::tm* tm = std::localtime(&time_t);
    
    // 设置时间
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = 0;
    
    return std::chrono::system_clock::from_time_t(std::mktime(tm));
}

void TradingTimeManager::_set_error(const std::string& error) const
{
    _last_error = error;
}

NS_FT_END