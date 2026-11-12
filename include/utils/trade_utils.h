#ifndef TRADE_UTILS_H
#define TRADE_UTILS_H

#include "core/macros.h"
#include "types/base_types.h"
#include "types/quote_types.h"
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 判断证券代码是否为科创板（STAR Market）
 * 
 * 科创板代码规则：
 * - 股票代码：以 688 开头（例如：688001）
 * - 存托凭证：以 689 开头（例如：689009）
 * 
 * @param security_code 证券代码
 * @return true表示科创板，false表示非科创板
 */
bool is_star_market_stock(const char* security_code);

/**
 * @brief 判断证券代码是否为创业板（ChiNext）
 * 
 * 创业板代码规则（分新旧两套）：
 * - 老创业板（2020年8月前）：300XXXX
 * - 新创业板（2020年8月后）：301XXXX
 * 
 * @param security_code 证券代码
 * @return true表示创业板，false表示非创业板
 */
bool is_chinext_stock(const char* security_code);

/**
 * @brief 判断证券代码是否为北京证券交易所（BSE）
 * 
 * 北交所代码规则：
 * - 新代码（2025年10月9日起）：920XXX（920开头，6位数字）
 * - 旧代码已废弃，本函数不支持
 * 
 * @param security_code 证券代码
 * @return true表示北交所，false表示非北交所
 */
bool is_bse_stock(const char* security_code);

/**
 * @brief 计算价格笼子上限
 * 
 * 价格笼子规则：
 * 1. 北交所（BSE，920开头）：基准价的 ±5%
 * 2. 科创板（STAR，688/689开头）：基准价的 ±2%
 * 3. 沪深主板和创业板（300/301等）：基准价的 ±2%，如果乘以2%后小于等于0.1元，按0.1元算
 * 
 * @param base_price 基准价格
 * @param security_code 证券代码
 * @return 价格笼子上限
 */
double calculate_price_cage_upper_limit(double base_price, const char* security_code);

/**
 * @brief 根据总金额和价格计算买入股数
 * 
 * 简单的数量计算函数，根据市场规则调整交易单位：
 * - 科创板（688/689）：支持零股交易
 * - 主板和其他：100股为一手，向下取整
 * 
 * @param amount 总金额（万元）
 * @param price 买入价格（元）
 * @param security_code 证券代码
 * @return 计算出的买入股数，如果计算失败返回0
 */
int64_t calculate_buy_quantity(double amount, double price, const char* security_code);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
NS_FT_BEGIN
namespace utils {

/**
 * C++ wrapper functions
 */
inline bool is_star_market_stock(const std::string& security_code) {
    return ::is_star_market_stock(security_code.c_str());
}

inline bool is_chinext_stock(const std::string& security_code) {
    return ::is_chinext_stock(security_code.c_str());
}

inline bool is_bse_stock(const std::string& security_code) {
    return ::is_bse_stock(security_code.c_str());
}

inline double calculate_price_cage_upper_limit(double base_price, const std::string& security_code) {
    return ::calculate_price_cage_upper_limit(base_price, security_code.c_str());
}

inline int64_t calculate_buy_quantity(double amount, double price, const std::string& security_code) {
    return ::calculate_buy_quantity(amount, price, security_code.c_str());
}

} // namespace utils
NS_FT_END
#endif

#endif // TRADE_UTILS_H
