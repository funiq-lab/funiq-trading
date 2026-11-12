#ifndef STRATEGY_TYPES_H
#define STRATEGY_TYPES_H

#include "types/trade_types.h"
#include "types/quote_types.h"
#include <string>
#include <functional>
#include <eventpp/callbacklist.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 策略状态枚举
 */
enum class StrategyState {
    UNDEFINED = 0,      ///< 未定义
    UNINITIALIZED = 1,  ///< 未初始化
    INITIALIZED = 2,    ///< 已初始化
    STOPPED = 3,        ///< 已停止
    RUNNING = 4,        ///< 运行中
    ERROR = 5           ///< 错误状态
};

/**
 * 策略配置信息
 */
struct StrategyConfig {
    std::string id;          ///< 策略ID
    std::string name;        ///< 策略名称
    std::string description; ///< 策略描述
    std::string module;      ///< 模块名称
    std::string config;      ///< 默认配置JSON字符串
};

// 策略数据回调函数类型
typedef void (*StrategyDataCallbackType)(const char* strategy_id, const char* data_type, const char* data);

// 策略数据回调列表和句柄类型定义
using StrategyDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<StrategyDataCallbackType>>;
using StrategyDataCallbackHandle = typename StrategyDataCallbackList::Handle;


// 策略挂单表结构 - 策略监控配置
/**
 * 挂单状态
 */
enum class PendingOrderStatus {
    UNKNOWN = 0,     // 未知状态
    ACTIVE = 1,      // 激活状态
    EXECUTED = 2,    // 已执行
    CANCELLED = 3     // 已取消
};

struct StrategyPendingOrder
{
    int64_t id = 0;                        // 主键ID
    std::string strategy_id;               // 策略ID
    std::string pending_order_info;        // 策略挂单信息(JSON) - 包含监控条件、板块、股票列表等
    uint64_t create_time = 0;              // 创建时间
    uint64_t update_time = 0;              // 更新时间
    PendingOrderStatus status = PendingOrderStatus::ACTIVE; // 挂单状态
};

// 策略订单表结构 - 具体的订单执行信息
struct StrategyOrder
{
    int64_t id = 0;                        // 主键ID
    int64_t pending_order_id = 0;          // 关联的策略挂单ID（外键）
    std::string strategy_id;               // 策略ID（冗余字段，便于查询）
    std::string pending_order_info;        // 策略挂单信息(JSON) - 冗余字段，便于查询
    uint64_t pending_submit_time = 0;      // 策略挂单提交时间（冗余字段）
    
    // 账户信息
    std::string login_id;                  // 登陆账号
    std::string cust_id;                   // 客户号
    std::string fund_account_id;           // 资金账户ID
    std::string account_id;                // 证券账户ID（对应current_account_id）
    
    // 订单核心信息
    std::string security_code;             // 证券代码（具体的股票）
    int64_t order_id = 0;                  // 委托编号（初始为0，等待回调更新）
    std::string exchange_order_id;         // 交易所订单编号
    uint64_t batch_id = 0;                 // 批次编号
    
    OrderSide side = SIDE_UNKNOWN;             // 买卖方向
    OrderSign sign = ORDER_SIGN_UNKNOWN;       // 订单标识
    OrderType order_type = ORDER_TYPE_UNKNOWN; // 订单类型
    OrderStatus status = ORDER_STATUS_UNKNOWN; // 订单状态
    std::string status_msg;                    // 状态描述
    
    double price = 0.0;                    // 委托价格
    uint64_t qty = 0;                      // 委托数量
    uint64_t leaves_qty = 0;               // 剩余数量
    uint64_t cum_qty = 0;                  // 累计成交数量
    double avg_price = 0.0;                // 平均成交价格
    double cum_amt = 0.0;                  // 累计成交金额
    
    uint64_t order_time = 0;               // 委托时间
    uint64_t update_time = 0;              // 更新时间
    uint64_t create_time = 0;              // 订单创建时间
    
    int32_t error_code = 0;                // 错误码
    std::string error_msg;                 // 错误信息
};

// 联表查询结果
struct StrategyPendingOrderWithOrders
{
    StrategyPendingOrder pending_order;
    std::vector<StrategyOrder> orders;
};

// ===== 查询条件结构 =====

struct StrategyPendingOrderQuery
{
    std::string strategy_id;                   // 策略ID筛选
    PendingOrderStatus status = PendingOrderStatus::ACTIVE; // 状态筛选
    bool filter_by_status = false;             // 是否启用状态筛选
    uint64_t start_time = 0;                   // 开始时间
    uint64_t end_time = 0;                     // 结束时间
    int limit = 100;                           // 返回条数限制
    int offset = 0;                            // 偏移量
    std::string order_by = "create_time";      // 排序字段
    bool desc = true;                          // 是否降序
};

struct StrategyOrderQuery
{
    std::string strategy_id;                   // 策略ID筛选
    int64_t pending_order_id = 0;              // 策略挂单ID筛选
    std::string security_code;                 // 股票代码筛选
    int market = 0;                            // 市场筛选（0=所有，1=上交所，2=深交所）
    OrderStatus status = ORDER_STATUS_UNKNOWN; // 状态筛选
    uint64_t start_time = 0;                   // 开始时间
    uint64_t end_time = 0;                     // 结束时间
    int limit = 100;                           // 返回条数限制
    int offset = 0;                            // 偏移量
    std::string order_by = "create_time";      // 排序字段
    bool desc = true;                          // 是否降序
};


#ifdef __cplusplus
}
#endif

#endif // STRATEGY_TYPES_H