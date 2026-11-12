#ifndef TRADE_TYPES_H
#define TRADE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "base_types.h"
#include "eventpp/callbacklist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_RETURN_NUM 1000

typedef enum {
    LOGIN_MODE_CUST_ID = 1,
    LOGIN_MODE_FUND_ACCOUNT_ID = 2,
} LoginMode;

// 银证转账方向枚举
typedef enum {
    BANK_SECURITIES_TRANSFER_IN = 1,   // 银证转入
    BANK_SECURITIES_TRANSFER_OUT = 2,  // 银证转出
} BankSecuritiesTransferDirection;

// 资金划拨方向枚举
typedef enum {
    FUND_TRANSFER_IN = 1,    // 转入
    FUND_TRANSFER_OUT = 2,   // 转出
} FundTransferDirection;

// 订单状态枚举
typedef enum {
    ORDER_STATUS_UNKNOWN = 0,
    ORDER_STATUS_NEW = 1,             // 新订单
    ORDER_STATUS_PARTIAL_FILLED = 2,  // 部分成交
    ORDER_STATUS_FILLED = 3,          // 全部成交
    ORDER_STATUS_CANCELLED = 4,        // 已撤销
    ORDER_STATUS_REJECTED = 5,        // 已拒绝
    ORDER_STATUS_PENDING_CANCEL = 6,  // 待撤销
    ORDER_STATUS_PENDING_NEW = 7,     // 待确认
} OrderStatus;

// 订单类型枚举
typedef enum {
    ORDER_TYPE_UNKNOWN = 0,
    ORDER_TYPE_LIMIT = 1,           // 限价单
    ORDER_TYPE_MARKET = 2,          // 市价单
    ORDER_TYPE_STOP = 3,            // 停损单
    ORDER_TYPE_BEST_PRICE = 4,      // 最优价格单
} OrderType;

// 买卖方向枚举
typedef enum {
    SIDE_UNKNOWN = 0,
    SIDE_BUY = 'B',                 // 买入
    SIDE_SELL = 'S',                // 卖出
} OrderSide;

typedef enum {
    ORDER_SIGN_ORDER = 0,
    ORDER_SIGN_CANCEL = 1,
    ORDER_SIGN_UNKNOWN = 2,
} OrderSign;

// 委托查询条件
typedef enum {
    ORDER_QUERY_CONDITION_ALL = 0,                  // 查询全部（不含撤单委托）
    ORDER_QUERY_CONDITION_CANCEL_AVAILABLE = 1,     // 查询可撤单订单
    ORDER_QUERY_CONDITION_TRADE_EXEC_OR_CANCELLED = 2, // 查询有成交或已撤单订单
    ORDER_QUERY_CONDITION_UNAVAILABLE_CANCEL = 3,    // 查询不可撤单订单
    ORDER_QUERY_CONDITION_ALL_WITH_CANCEL = 4,       // 查询全部委托（含撤单委托）
    ORDER_QUERY_CONDITION_ALL_EXCEPT_INTERNAL_REJ = 5, // 查询全部委托（不含TE内部拒单）
} OrderQueryCondition;

// 成交查询条件
typedef enum {
    TRADE_QUERY_CONDITION_GLOBAL_PARAMS = 0,         // 按全局参数设置的模式进行查询
    TRADE_QUERY_CONDITION_TRADE_REPORT = 1,          // 仅查询成交回报
    TRADE_QUERY_CONDITION_TRADE_AND_CANCEL = 2,      // 查询成交回报、撤单成功消息
    TRADE_QUERY_CONDITION_ALL_AVAILABLE = 3,         // 查询成交回报、撤单成功消息、委托拒单消息、撤单拒单消息
} TradeQueryCondition;

// 返回顺序
typedef enum {
    RETURN_SEQ_DEFAULT = 0,                       // 默认值
    RETURN_SEQ_TIME_ASC = 1,                         // 时间正序
    RETURN_SEQ_TIME_DESC = 2,                        // 时间倒序
} ReturnSeqType;

// 通用查询条件联合体 - 根据查询类型选择对应的条件
typedef union {
    OrderQueryCondition order_condition;            // 委托查询条件
    TradeQueryCondition trade_condition;            // 成交查询条件
} UniversalQueryCondition;

// 通用查询请求参数
typedef struct {    
    // 查询条件
    UniversalQueryCondition condition;         // 通用查询条件
    ReturnSeqType return_seq = RETURN_SEQ_DEFAULT;  // 返回顺序
    int64_t return_num = DEFAULT_RETURN_NUM;         // 返回数量
} QueryRequestParams;


// 交易配置结构体
typedef struct {
    // 基本配置
    char extended_config_file[256];
    bool callback_mode;
    LoginMode login_mode;
    
    // 账户列表
    AccountList account_list;

    // 性能统计配置
    PerformanceConfig performance;
    
    // Mock模式配置
    MockConfig mock;
    
    // 数据管理配置
    DataConfig data_config;
} TradeConfig;

typedef struct {
    AccountInfo account;            // 账户信息
    char security_code[32];         // 证券代码
    MarketType market;              // 市场类型
    
    OrderSide side;                 // 买卖方向
    OrderType order_type;           // 订单类型
    double price;                   // 委托价格
    uint64_t qty;                   // 委托数量
    uint64_t batch_id;              // 批次ID（可选）
} TradeOrderRequest;

// 买卖单响应数据
typedef struct {
    AccountInfo account;
    SecurityInfo security_info;
    uint64_t timestamp;             // 时间戳
    char current_account_id[32];
    double price;                   // 委托价格
    uint64_t qty;                   // 委托数量
    int64_t request_id;             // 请求ID
    int64_t order_id;
    uint64_t batch_id; 
    OrderSide side;                 // 买卖方向
    OrderType order_type;           // 订单类型
    uint8_t business_type;          // 业务类型
    int64_t response_time;          // 响应时间

    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
} TradeOrderRspData;               


// 撤单响应数据
typedef struct {
    AccountInfo account;
    uint64_t timestamp;             // 时间戳
    char current_account_id[32];
    int64_t request_id;             // 请求ID
    int64_t order_id;
    uint64_t batch_id; 
    int64_t response_time;          // 响应时间

    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
} TradeOrderCancelRsqData;          

// 订单数据
typedef struct {
    AccountInfo account;            // 账户信息
    SecurityInfo security_info;     // 证券信息
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID
    char current_account_id[32];    // 当前数据对应的具体证券账户ID（沪或深）
    MarketType current_market;      // 当前数据对应的市场
    
    int64_t order_id;              // 委托编号
    char exchange_order_id[32];     // 交易所订单编号
    uint64_t batch_id;              // 批次编号
    
    OrderSide side;                 // 买卖方向
    OrderType order_type;           // 订单类型
    OrderSign sign;                 // 订单标识
    OrderStatus status;             // 订单状态
    char status_msg[256];           // 状态描述
    
    double price;                   // 委托价格
    uint64_t qty;                   // 委托数量
    uint64_t leaves_qty;            // 剩余数量
    uint64_t cum_qty;               // 累计成交数量
    double avg_price;               // 平均成交价格
    double cum_amt;                 // 累计成交金额
    
    uint64_t order_time;            // 委托时间
    uint64_t update_time;           // 更新时间
    
    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
    
    uint8_t business_type;          // 业务类型
    uint16_t opu_id;                // 报盘通道ID
    bool is_last;                   // 是否最后一条
} TradeOrderData;                   

// 成交数据
typedef struct {
    AccountInfo account;            // 账户信息
    SecurityInfo security_info;     // 证券信息
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID
    char current_account_id[32];    // 当前数据对应的具体证券账户ID（沪或深）
    MarketType current_market;      // 当前数据对应的市场
    
    char trade_id[32];              // 成交编号
    int64_t order_id;              // 委托编号
    char exchange_order_id[32];     // 交易所订单编号
    uint64_t batch_id;              // 批次编号
    
    OrderSide side;                 // 买卖方向
    OrderType order_type;           // 订单类型
    uint8_t business_type;          // 业务类型
    
    double price;                   // 成交价格
    uint64_t qty;                   // 成交数量
    double value;                   // 成交金额
    double fee;                     // 手续费
    
    uint64_t trade_time;            // 成交时间
    char currency[8];               // 货币种类

    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
    bool is_last;                   // 是否最后一条
} TradeFillData;                    

// 持仓数据结构
typedef struct {
    AccountInfo account;            // 账户信息
    SecurityInfo security_info;     // 证券信息
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID
    char current_account_id[32];    // 当前数据对应的具体证券账户ID（沪或深）
    MarketType current_market;      // 当前数据对应的市场
    
    uint64_t total_qty;             // 总持仓量
    uint64_t available_qty;         // 可用数量
    uint64_t frozen_qty;            // 冻结数量
    uint64_t today_buy_qty;         // 今日买入数量
    uint64_t today_sell_qty;        // 今日卖出数量
    
    double cost_price;              // 持仓成本价
    double market_price;            // 最新市价
    double market_value;            // 市值
    double profit_loss;             // 浮动盈亏
    double profit_loss_ratio;       // 盈亏比例

    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
    
    uint64_t update_time;           // 更新时间
    uint16_t security_type;         // 证券类型
    bool is_last;                   // 是否最后一条
} PositionData;

// 资金数据结构
typedef struct {
    AccountInfo account;            // 账户信息
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID
    char currency[8];               // 货币种类
    char current_account_id[32];    // 当前数据对应的具体证券账户ID（沪或深）
    MarketType current_market;      // 当前数据对应的市场
    
    double total_asset;             // 总资产
    double cash_balance;            // 现金余额
    double available_cash;          // 可用资金
    double frozen_cash;             // 冻结资金
    double market_value;            // 证券市值
    
    double buying_power;            // 购买力
    double withdrawal_cash;         // 可取资金
    double profit_loss;             // 当日盈亏
    double profit_loss_ratio;       // 盈亏比例

    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
    
    uint64_t update_time;           // 更新时间
    bool is_last;                   // 是否最后一条
} AccountData;

// 银证转账数据结构
typedef struct {
    char login_id[32];               // 登陆账号
    char cust_id[32];               // 客户号
    char fund_account_id[32];       // 资金账号
    char branch_id[32];             // 分支机构ID
    char account_id[32];            // 账户ID
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID

    BankSecuritiesTransferDirection direction;  // 转账方向
    
    // 响应信息
    int64_t serial_no;              // 流水号
    int64_t transfer_serial_no;     // 转账流水号
    int64_t bank_transfer_serial_no; // 银行转账流水号
    uint8_t business_status;        // 业务状态
    int64_t transact_time;          // 成交时间
    
    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
} BankSecuritiesTransferData;

// 资金划拨数据结构
typedef struct {
    char login_id[32];               // 登陆账号
    char cust_id[32];               // 客户号
    char fund_account_id[32];       // 资金账号
    char branch_id[32];             // 分支机构ID
    uint64_t timestamp;             // 时间戳
    int64_t request_id;             // 请求ID
    
    int32_t error_code;             // 错误码
    char error_msg[256];            // 错误信息
} FundTransferData;

typedef union {    
    // 交易数据
    TradeOrderData trade_order;
    TradeFillData trade_fill;
    PositionData position;
    AccountData account;
    BankSecuritiesTransferData bank_securities_transfer;
    FundTransferData fund_transfer;
    TradeOrderRspData trade_order_rsp;
    TradeOrderCancelRsqData trade_order_cancel_rsp;
    
    // 原始数据缓冲区(用于未知或自定义类型)
    uint8_t raw_data[4096];
} UnifiedTradeData;

// 统一交易数据消息结构
typedef struct {
    FTDataType data_type;         // 数据类型
    uint64_t recv_timestamp;    // 接收时间戳
    uint32_t data_size;         // 实际数据大小
    int32_t error_code;         // 错误码(0表示成功)
    char error_msg[256];        // 错误信息
    int64_t request_id;         // 请求ID
    bool is_last;               // 是否最后一条(用于查询)
    UnifiedTradeData data;           // 数据内容
} UnifiedTradeMessage;


// 交易回调函数类型
typedef void (*TradeOrderCallbackType)(const TradeOrderData* data);
typedef void (*TradeOrderQueryCallbackType)(const TradeOrderData* data);
typedef void (*TradeFillCallbackType)(const TradeFillData* data);
typedef void (*PositionCallbackType)(const PositionData* data);
typedef void (*AccountCallbackType)(const AccountData* data);
typedef void (*TradeErrorCallbackType)(int32_t error_code, const char* error_msg, int64_t request_id);
typedef void (*TradeOrderRspCallbackType)(const TradeOrderRspData* data);
typedef void (*TradeOrderCancelRspCallbackType)(const TradeOrderCancelRsqData* data);
typedef void (*BankSecuritiesTransferCallbackType)(const BankSecuritiesTransferData* data);
typedef void (*FundTransferCallbackType)(const FundTransferData* data);

using TradeOrderCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeOrderCallbackType>>;
using TradeOrderQueryCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeOrderQueryCallbackType>>;
using TradeFillCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeFillCallbackType>>;
using PositionCallbackList = eventpp::CallbackList<std::remove_pointer_t<PositionCallbackType>>;
using AccountCallbackList = eventpp::CallbackList<std::remove_pointer_t<AccountCallbackType>>;
using TradeErrorCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeErrorCallbackType>>;
using TradeOrderRspCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeOrderRspCallbackType>>;
using TradeOrderCancelRspCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeOrderCancelRspCallbackType>>;
using BankSecuritiesTransferCallbackList = eventpp::CallbackList<std::remove_pointer_t<BankSecuritiesTransferCallbackType>>;
using FundTransferCallbackList = eventpp::CallbackList<std::remove_pointer_t<FundTransferCallbackType>>;

using TradeOrderCallbackHandle = typename TradeOrderCallbackList::Handle;
using TradeOrderQueryCallbackHandle = typename TradeOrderQueryCallbackList::Handle;
using TradeFillCallbackHandle = typename TradeFillCallbackList::Handle;
using PositionCallbackHandle = typename PositionCallbackList::Handle;
using AccountCallbackHandle = typename AccountCallbackList::Handle;
using TradeErrorCallbackHandle = typename TradeErrorCallbackList::Handle;
using TradeOrderRspCallbackHandle = typename TradeOrderRspCallbackList::Handle;
using TradeOrderCancelRspCallbackHandle = typename TradeOrderCancelRspCallbackList::Handle;
using BankSecuritiesTransferCallbackHandle = typename BankSecuritiesTransferCallbackList::Handle;
using FundTransferCallbackHandle = typename FundTransferCallbackList::Handle;

#ifdef __cplusplus
}
#endif

#endif