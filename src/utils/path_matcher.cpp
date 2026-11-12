#include "utils/path_matcher.h"
#include <regex>

NS_FT_BEGIN

PathMatcher::PathMatcher(const std::string& pattern) {
    compile(pattern);
}

bool PathMatcher::compile(const std::string& pattern) {
    _pattern = pattern;
    _compiled = false;
    
    try {
        _param_names = extract_param_names(pattern);
        std::string regex_str = pattern_to_regex(pattern);
        _regex = std::regex(regex_str);
        _compiled = true;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

PathMatchResult PathMatcher::match(const std::string& path) const {
    PathMatchResult result;
    
    if (!_compiled) return result;
    
    if (_param_names.empty()) {
        result.matched = (_pattern == path);
        return result;
    }
    
    try {
        std::smatch matches;
        if (std::regex_match(path, matches, _regex)) {
            result.matched = true;
            for (size_t i = 0; i < _param_names.size() && i + 1 < matches.size(); ++i) {
                result.params[_param_names[i]] = matches[i + 1].str();
            }
        }
    } catch (const std::exception&) {
        result.matched = false;
    }
    
    return result;
}

std::vector<std::string> PathMatcher::extract_param_names(const std::string& pattern) const {
    std::vector<std::string> names;
    std::regex param_regex(R"(\{([^}]+)\})");
    std::sregex_iterator iter(pattern.begin(), pattern.end(), param_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        names.push_back(iter->str(1));
    }
    return names;
}

std::string PathMatcher::pattern_to_regex(const std::string& pattern) const {
    std::string regex_pattern = pattern;
    
    // 转义特殊字符
    std::regex escape_chars(R"([.^$*+?()[\]{}|\\])");
    regex_pattern = std::regex_replace(regex_pattern, escape_chars, R"(\$&)");
    
    // {param} -> ([^/]+)
    std::regex param_regex(R"(\\\{[^}]+\\\})");
    regex_pattern = std::regex_replace(regex_pattern, param_regex, R"(([^/]+))");
    
    return "^" + regex_pattern + "$";
}

// 便捷函数
PathMatchResult match_path(const std::string& pattern, const std::string& path) {
    PathMatcher matcher(pattern);
    return matcher.match(path);
}

bool is_path_match(const std::string& pattern, const std::string& path) {
    return match_path(pattern, path).matched;
}

NS_FT_END