#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include "core/macros.h"

NS_FT_BEGIN

/**
 * @brief 路径匹配结果
 */
struct PathMatchResult {
    bool matched = false;
    std::unordered_map<std::string, std::string> params;
    
    std::string get(const std::string& name, const std::string& default_val = "") const {
        auto it = params.find(name);
        return (it != params.end()) ? it->second : default_val;
    }
};

/**
 * @brief 简单的路径参数匹配工具
 * 
 * 支持 {param} 风格的路径参数匹配，如：
 * - /users/{id} 匹配 /users/123
 * - /users/{uid}/orders/{oid} 匹配 /users/123/orders/456
 */
class PathMatcher {
public:
    PathMatcher() = default;
    explicit PathMatcher(const std::string& pattern);
    
    bool compile(const std::string& pattern);
    PathMatchResult match(const std::string& path) const;
    
    const std::string& pattern() const { return _pattern; }
    bool has_params() const { return !_param_names.empty(); }

private:
    std::string _pattern;
    std::regex _regex;
    std::vector<std::string> _param_names;
    bool _compiled = false;
    
    std::vector<std::string> extract_param_names(const std::string& pattern) const;
    std::string pattern_to_regex(const std::string& pattern) const;
};

// 便捷函数
PathMatchResult match_path(const std::string& pattern, const std::string& path);
bool is_path_match(const std::string& pattern, const std::string& path);

NS_FT_END