#include "utils/url_parser.h"
#include <sstream>
#include <iomanip>

NS_FT_BEGIN

UrlParseResult UrlParser::parse(const std::string& url) {
    UrlParseResult result;
    
    // 查找 ? 分隔符
    size_t query_pos = url.find('?');
    
    if (query_pos == std::string::npos) {
        // 没有查询参数
        result.path = url;
    } else {
        // 分割路径和查询参数
        result.path = url.substr(0, query_pos);
        std::string query_string = url.substr(query_pos + 1);
        
        if (!query_string.empty()) {
            parse_query_string(query_string, result.query_params);
        }
    }
    
    return result;
}

void UrlParser::parse_query_string(const std::string& query_string, 
                                  std::unordered_map<std::string, std::string>& params) {
    std::stringstream ss(query_string);
    std::string pair;
    
    // 按 & 分割参数对
    while (std::getline(ss, pair, '&')) {
        if (pair.empty()) continue;
        
        // 查找 = 分隔符
        size_t equal_pos = pair.find('=');
        
        if (equal_pos == std::string::npos) {
            // 没有值的参数，如 ?flag
            params[url_decode(pair)] = "";
        } else {
            // 有值的参数，如 ?name=value
            std::string key = pair.substr(0, equal_pos);
            std::string value = pair.substr(equal_pos + 1);
            params[url_decode(key)] = url_decode(value);
        }
    }
}

std::string UrlParser::url_decode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        char c = encoded[i];
        
        if (c == '%' && i + 2 < encoded.length()) {
            // URL 编码字符 %XX
            std::string hex = encoded.substr(i + 1, 2);
            try {
                int value = std::stoi(hex, nullptr, 16);
                result += static_cast<char>(value);
                i += 2; // 跳过接下来的两个字符
            } catch (const std::exception&) {
                // 解析失败，保持原字符
                result += c;
            }
        } else if (c == '+') {
            // + 表示空格
            result += ' ';
        } else {
            result += c;
        }
    }
    
    return result;
}


NS_FT_END