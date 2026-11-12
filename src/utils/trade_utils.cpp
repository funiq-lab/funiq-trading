#include "utils/trade_utils.h"
#include <string.h>
#include <algorithm>
#include <cmath>

/**
 * @brief 判断证券代码是否为科创板（STAR Market）
 * 
 * 科创板代码规则：
 * - 股票代码：以 688 开头（例如：688001）
 * - 存托凭证：以 689 开头（例如：689009）
 * 
 * @param security_code 证券代码字符串
 * @return true 表示科创板，false 表示非科创板
 */
bool is_star_market_stock(const char* security_code) {
    if (!security_code || strlen(security_code) < 3) {
        return false;
    }
    // 科创板：688开头的股票或689开头的存托凭证
    return strncmp(security_code, "688", 3) == 0 || 
           strncmp(security_code, "689", 3) == 0;
}

/**
 * @brief 判断证券代码是否为创业板（ChiNext）
 * 
 * @param security_code 证券代码字符串
 * @return true 表示创业板，false 表示非创业板
 */
bool is_chinext_stock(const char* security_code) {
    if (!security_code || strlen(security_code) < 3) {
        return false;
    }
    // 创业板：300开头（老创业板）或301开头（新创业板）
    return strncmp(security_code, "300", 3) == 0 || 
           strncmp(security_code, "301", 3) == 0;
}

/**
 * @brief 判断证券代码是否为北京证券交易所（BSE）
 * 
 * 北交所代码规则：
 * - 自2025年10月9日起，北交所股票代码已全面切换为以 "920" 开头的独立代码
 * - 新代码：920XXX（6位数字，920开头）
 * - 旧代码（已废弃）：43XXXX、83XXXX、87XXXX等前缀
 * - 新代码体系与新三板脱钩，提高了股票辨识度
 * 
 * 注意：本函数仅识别新代码体系（920开头），不支持已废弃的旧代码
 * 
 * @param security_code 证券代码字符串
 * @return true 表示北交所，false 表示非北交所
 */
bool is_bse_stock(const char* security_code) {
    if (!security_code || strlen(security_code) < 3) {
        return false;
    }
    return strncmp(security_code, "920", 3) == 0;
}

/**
 * @brief 计算价格笼子上限
 * 
 * 价格笼子是交易所为防止股价异常波动而设置的价格波动限制机制。
 * 不同市场板块有不同的价格笼子规则：
 * 
 * 1. 北交所（BSE）：
 *    - 价格波动范围：±5%
 *    - 计算公式：基准价 × (1 + 5%) = 基准价 × 1.05
 * 
 * 2. 科创板（STAR Market）：
 *    - 价格波动范围：±2%
 *    - 计算公式：基准价 × (1 + 2%) = 基准价 × 1.02
 * 
 * 3. 沪深主板和创业板（ChiNext）：
 *    - 价格波动范围：±2%
 *    - 特殊规则：如果 2% 的价格变动小于等于 0.1 元，则按 0.1 元计算
 *    - 计算公式：max(基准价 × 2%, 0.1)
 *    - 示例：
 *      * 基准价 10 元：10 × 2% = 0.2 元 > 0.1 元，使用 0.2 元
 *      * 基准价 3 元：3 × 2% = 0.06 元 < 0.1 元，使用 0.1 元
 * 
 * @param base_price 基准价格
 * @param security_code 证券代码
 * @return 价格笼子上限；如果参数无效返回 0.0
 */
double calculate_price_cage_upper_limit(double base_price, const char* security_code) {
    if (base_price <= 0.0 || !security_code) {
        return 0.0;
    }
    
    double price_change;
    
    // 北交所：5% 价格笼子
    if (is_bse_stock(security_code)) {
        price_change = base_price * 0.05;
    }
    // 科创板：2% 价格笼子
    else if (is_star_market_stock(security_code)) {
        price_change = base_price * 0.02;
    }
    // 沪深主板和创业板：2% 价格笼子，最小 0.1 元
    else {
        price_change = base_price * 0.02;
        if (price_change <= 0.1) {
            price_change = 0.1;
        }
    }
    
    double result = base_price + price_change;
    
    // 格式化为两位小数，向下取整
    // 方法：乘以 100，向下取整，再除以 100
    return std::floor(result * 100.0) / 100.0;
}

/**
 * @brief 根据总金额和价格计算买入股数
 * 
 * 简单的买入数量计算函数，只负责根据金额和价格计算股数，
 * 并根据市场规则调整为有效的交易单位。
 * 
 * 交易单位规则：
 * - 科创板（688/689开头）：支持零股交易，无需整手限制
 * - 沪深主板和其他：必须按 100 股为一手交易，向下取整
 * 
 * 注意：本函数不处理价格笼子逻辑，调用方应根据需要自行决定是否使用价格笼子。
 * 
 * 示例 1（科创板）：
 * - 总金额：1 万元 = 10,000 元
 * - 买入价：50 元
 * - 可买股数：10,000 / 50 = 200 股
 * - 科创板支持零股，返回：200 股
 *
 * 示例 2（其他）：
 * - 总金额：1 万元 = 10,000 元
 * - 买入价：33 元
 * - 可买股数：10,000 / 33 = 303.03 股
 * - 按 100 股一手，向下取整：300 股
 * 
 * @param amount 总金额（单位：万元）
 * @param price 买入价格（单位：元）
 * @param security_code 证券代码
 * @return 计算后的买入股数；如果参数无效或计算失败返回 0
 */
int64_t calculate_buy_quantity(double amount, double price, const char* security_code) {
    if (amount <= 0.0 || price <= 0.0 || !security_code) {
        return 0;
    }
    
    // 将万元转换为元
    double amount_in_yuan = amount * 10000.0;
    
    // 计算理论可买股数
    int64_t theoretical_quantity = static_cast<int64_t>(amount_in_yuan / price);
    
    // 科创板支持零股交易，无需整手限制
    if (is_star_market_stock(security_code)) {
        return theoretical_quantity;
    }
    
    // 北交所、沪深主板和创业板：必须按 100 股为最小交易单位，向下取整
    const int LOT_SIZE = 100;
    int64_t quantity = (theoretical_quantity / LOT_SIZE) * LOT_SIZE;
    
    return quantity;
}
