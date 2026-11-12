#ifndef QUOTE_TYPES_H
#define QUOTE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "base_types.h"
#include <functional>
#include <eventpp/callbacklist.h>

#ifdef __cplusplus
extern "C" {
#endif

// 交易阶段枚举
typedef enum {
    TRADING_PHASE_UNKNOWN = 0,
    TRADING_PHASE_BEFORE_OPEN = 1,      // 开盘前
    TRADING_PHASE_OPEN_AUCTION = 2,     // 开盘集合竞价
    TRADING_PHASE_CONTINUOUS = 3,       // 连续竞价
    TRADING_PHASE_CLOSE_AUCTION = 4,    // 收盘集合竞价
    TRADING_PHASE_AFTER_CLOSE = 5,      // 收盘后
    TRADING_PHASE_SUSPENDED = 6,        // 停牌
} TradingPhase;


// 行情配置结构体
typedef struct {
    // 基本配置
    char extended_config_file[256];
    bool callback_mode;
    
    // 订阅配置
    SubscriptionConfig subscription;
    
    // 性能统计配置
    PerformanceConfig performance;
    
    // Mock模式配置
    MockConfig mock;
    
    // 数据管理配置
    DataConfig data_config;
} QuoteConfig;

// 统一指数行情数据结构
typedef struct {
    SecurityInfo security_info;
    uint64_t timestamp;          // 时间戳(微秒)
    uint32_t update_time;        // 更新时间(HHMMSSMMM)
    TradingPhase trading_phase;  // 交易阶段
    
    double last_index;              // 最新指数
    double pre_close_index;         // 昨收盘指数
    double open_index;              // 开盘指数
    double high_index;              // 最高指数
    double low_index;               // 最低指数
    double close_index;             // 收盘指数
    
    uint64_t total_volume;          // 成交总量
    uint64_t total_value;           // 成交总额
    uint64_t num_trades;            // 成交笔数
    
    uint32_t sequence;              // 序列号
} IndexData;

// 统一竞价行情快照数据结构
typedef struct {
    SecurityInfo security_info;
    uint64_t timestamp;             // 时间戳(微秒)
    uint32_t update_time;           // 更新时间(HHMMSSMMM)
    TradingPhase trading_phase;     // 交易阶段
    char instrument_status[16];     // 合约状态
    
    // 价格信息
    double last_price;              // 最新价
    double pre_close_price;         // 昨收盘价
    double open_price;              // 开盘价
    double high_price;              // 最高价
    double low_price;               // 最低价
    double close_price;             // 收盘价
    double upper_limit_price;       // 涨停价
    double lower_limit_price;       // 跌停价
    double avg_price;               // 均价
    
    // 成交信息
    uint64_t total_volume;          // 成交总量
    uint64_t total_value;           // 成交总额
    uint64_t num_trades;            // 成交笔数
    
    // 委托信息
    uint64_t total_bid_qty;         // 委托买入总量
    uint64_t total_ask_qty;         // 委托卖出总量
    double weighted_avg_bid_price;  // 加权平均委买价
    double weighted_avg_ask_price;  // 加权平均委卖价
    uint32_t total_bid_number;      // 买入总笔数
    uint32_t total_ask_number;      // 卖出总笔数
    
    // 十档行情
    double bid_price[10];           // 十档买价
    uint64_t bid_qty[10];           // 十档买量
    double ask_price[10];           // 十档卖价
    uint64_t ask_qty[10];           // 十档卖量
    
    // 期权特有字段(非期权时为0)
    uint64_t total_position;        // 持仓量
    double reference_price;         // 参考价
    
    // 基金特有字段(非基金时为0)
    double iopv;                    // 实时净值
    double pre_iopv;                // 前日净值
    
    uint16_t channel_no;            // 频道号
    uint32_t sequence;              // 序列号
} DepthData;

// 统一竞价行情队列数据结构(50笔委托)
typedef struct {
    SecurityInfo security_info;
    uint64_t timestamp;             // 时间戳(微秒)
    uint32_t update_time;           // 更新时间(HHMMSSMMM)
    
    // 买一档信息
    double bid_price;               // 买一档价格
    uint64_t bid_qty;               // 买一档数量
    uint32_t bid_num_orders;        // 买一档总委托笔数
    uint8_t bid_show_orders;        // 买一档揭示委托笔数
    
    // 卖一档信息
    double ask_price;               // 卖一档价格
    uint64_t ask_qty;               // 卖一档数量
    uint32_t ask_num_orders;        // 卖一档总委托笔数
    uint8_t ask_show_orders;        // 卖一档揭示委托笔数
    
    // 50笔委托明细(买卖各50笔)
    uint32_t orders[100];           // 委托明细
    
    uint16_t channel_no;            // 频道号
    uint32_t sequence;              // 序列号
} DepthFiftyData;

// 统一逐笔委托数据结构
typedef struct {
    SecurityInfo security_info;
    uint64_t timestamp;             // 时间戳(微秒)
    uint64_t transact_time;         // 委托时间
    
    char order_type;                // 订单类型: 'A'-新增, 'D'-删除, 'S'-状态
    char side;                      // 买卖方向: 'B'-买, 'S'-卖, ' '-状态
    char bs_flag;                   // 买卖标志(兼容字段)
    
    uint64_t order_no;              // 订单号
    uint32_t order_index;           // 委托序号
    double price;                   // 委托价格
    uint64_t qty;                   // 委托数量
    uint64_t balance;               // 剩余数量
    
    uint16_t channel_no;            // 频道号
    uint64_t biz_index;             // 业务序列号
    uint64_t appl_seq_num;          // 应用序列号
    uint32_t sequence;              // 序列号
} EntrustData;

// 统一逐笔成交数据结构
typedef struct {
    SecurityInfo security_info;
    uint64_t timestamp;             // 时间戳(微秒)
    uint64_t transact_time;         // 成交时间
    
    uint64_t trade_id;              // 成交编号
    uint32_t trade_index;           // 成交序号
    double price;                   // 成交价格
    uint64_t qty;                   // 成交数量
    uint64_t value;                 // 成交金额
    
    uint64_t bid_order_no;          // 买方订单号
    uint64_t ask_order_no;          // 卖方订单号
    uint64_t bid_appl_seq_num;      // 买方委托索引
    uint64_t ask_appl_seq_num;      // 卖方委托索引
    
    char exec_type;                 // 成交类别: 'F'-成交, '4'-撤销
    char trade_flag;                // 成交标志: 内外盘等
    
    uint16_t channel_no;            // 频道号
    uint64_t biz_index;             // 业务序列号
    uint64_t appl_seq_num;          // 应用序列号
    uint32_t sequence;              // 序列号
} TradeData;

// 统一数据联合体
typedef union {
    // 行情数据
    IndexData index;
    DepthData depth;
    DepthFiftyData depth_fifty;
    EntrustData entrust;
    TradeData trade;

    // 原始数据缓冲区(用于未知或自定义类型)
    uint8_t raw_data[4096];
} UnifiedQuoteData;

// 统一行情数据消息结构
typedef struct {
    FTDataType data_type;         // 数据类型
    uint64_t recv_timestamp;    // 接收时间戳
    uint32_t data_size;         // 实际数据大小
    UnifiedQuoteData data;           // 数据内容
} UnifiedQuoteMessage;


// 行情回调函数类型
typedef void (*QuoteStatusCallbackType)(Status status, const char* message);
typedef void (*IndexDataCallbackType)(const IndexData* data);
typedef void (*DepthDataCallbackType)(const DepthData* data);
typedef void (*DepthFiftyDataCallbackType)(const DepthFiftyData* data);
typedef void (*EntrustDataCallbackType)(const EntrustData* data);
typedef void (*TradeDataCallbackType)(const TradeData* data);

using IndexDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<IndexDataCallbackType>>;
using DepthDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<DepthDataCallbackType>>;
using DepthFiftyDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<DepthFiftyDataCallbackType>>;
using EntrustDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<EntrustDataCallbackType>>;
using TradeDataCallbackList = eventpp::CallbackList<std::remove_pointer_t<TradeDataCallbackType>>;
using StatusCallbackList = eventpp::CallbackList<std::remove_pointer_t<QuoteStatusCallbackType>>;

using IndexDataCallbackHandle = typename IndexDataCallbackList::Handle;
using DepthDataCallbackHandle = typename DepthDataCallbackList::Handle;
using DepthFiftyDataCallbackHandle = typename DepthFiftyDataCallbackList::Handle;
using EntrustDataCallbackHandle = typename EntrustDataCallbackList::Handle;
using TradeDataCallbackHandle = typename TradeDataCallbackList::Handle;
using StatusCallbackHandle = typename StatusCallbackList::Handle;


#ifdef __cplusplus
}
#endif

#endif