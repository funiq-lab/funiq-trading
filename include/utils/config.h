#ifndef UTILS_CONFIG_H
#define UTILS_CONFIG_H

#include "core/macros.h"
#include "types/base_types.h"
#include "types/trade_types.h"
#include "types/quote_types.h"
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 字符串到市场类型的转换
 * @param market_str 市场字符串
 * @return 市场类型枚举值
 */
MarketType parse_market_type(const char* market_str);

/**
 * 市场类型到字符串的转换
 * @param market 市场类型
 * @return 市场字符串
 */
const char* market_type_to_string(MarketType market);

/**
 * 字符串到数据类型的转换
 * @param data_type_str 数据类型字符串
 * @return 数据类型枚举值
 */
FTDataType parse_data_type(const char* data_type_str);

/**
 * 数据类型到字符串的转换
 * @param data_type 数据类型
 * @return 数据类型字符串
 */
const char* data_type_to_string(FTDataType data_type);

/**
 * 字符串到数据输出类型的转换
 * @param output_type_str 数据输出类型字符串
 * @return 数据输出类型枚举值
 */
DataOutputType parse_data_output_type(const char* output_type_str);

/**
 * 数据输出类型到字符串的转换
 * @param output_type 数据输出类型
 * @return 数据输出类型字符串
 */
const char* data_output_type_to_string(DataOutputType output_type);

#ifdef __cplusplus
}

// C++ 重载版本
NS_FT_BEGIN
namespace utils {

inline MarketType parse_market_type(const std::string& market_str) {
    return parse_market_type(market_str.c_str());
}

inline FTDataType parse_data_type(const std::string& data_type_str) {
    return parse_data_type(data_type_str.c_str());
}

} // namespace utils
NS_FT_END

#endif

#endif // UTILS_CONFIG_H