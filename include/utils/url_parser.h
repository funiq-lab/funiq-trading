#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "core/macros.h"

NS_FT_BEGIN

/**
 * @brief URL 解析结果
 */
struct UrlParseResult {
    std::string path;                                               ///< 路径部分 (不包含查询参数)
    std::unordered_map<std::string, std::string> query_params;      ///< 查询参数映射
    
    /**
     * @brief 获取查询参数值
     * @param name 参数名
     * @param default_val 默认值
     * @return 参数值或默认值
     */
    std::string get_param(const std::string& name, const std::string& default_val = "") const {
        auto it = query_params.find(name);
        return (it != query_params.end()) ? it->second : default_val;
    }
    
    /**
     * @brief 检查是否包含指定参数
     * @param name 参数名
     * @return 是否存在该参数
     */
    bool has_param(const std::string& name) const {
        return query_params.find(name) != query_params.end();
    }
};

/**
 * @brief 简单的 URL 解析器
 * 
 * 解析 URL 的路径和查询参数，如：
 * - /api/users?id=123&name=test -> path="/api/users", params={"id":"123", "name":"test"}
 * - /api/data -> path="/api/data", params={}
 */
class UrlParser {
public:
    /**
     * @brief 解析 URL
     * @param url 完整的 URL 字符串
     * @return 解析结果
     */
    static UrlParseResult parse(const std::string& url);
    
    /**
     * @brief URL 解码
     * @param encoded 编码后的字符串
     * @return 解码后的字符串
     */
    static std::string url_decode(const std::string& encoded);

private:
    /**
     * @brief 解析查询字符串
     * @param query_string 查询字符串 (不包含?)
     * @param params 输出参数映射
     */
    static void parse_query_string(const std::string& query_string, 
                                 std::unordered_map<std::string, std::string>& params);
};

NS_FT_END