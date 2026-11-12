#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 市场枚举
typedef enum {
    MARKET_UNKNOWN = 0,
    MARKET_SSE = 1,      // 上交所
    MARKET_SZSE = 2,      // 深交所
} MarketType;

// 统一数据类型枚举 - 支持行情和交易数据
typedef enum {
    // 行情数据类型 (1-100)
    DATA_TYPE_INDEX = 1,        // 指数行情
    DATA_TYPE_DEPTH = 2,        // 竞价行情快照
    DATA_TYPE_DEPTH_FIFTY = 3,  // 竞价行情队列(50笔)
    DATA_TYPE_ENTRUST = 4,      // 逐笔委托
    DATA_TYPE_TRADE = 5,        // 逐笔成交
    
    // 交易数据类型 (101-200) - 扩展
    DATA_TYPE_TRADE_ORDER_RTN = 101,      // 交易委托
    DATA_TYPE_TRADE_ORDER_QUERY = 102, // 交易委托查询
    DATA_TYPE_TRADE_FILL = 103,       // 交易成交
    DATA_TYPE_TRADE_CANCEL = 104,     // 交易撤单
    DATA_TYPE_TRADE_POSITION = 105,   // 持仓数据
    DATA_TYPE_TRADE_ACCOUNT = 106,    // 账户数据
    DATA_TYPE_BANK_SECURITIES_TRANSFER = 107,     // 银证转账
    DATA_TYPE_FUND_TRANSFER = 108,                // 资金划拨

    // 交易响应类型 (121-140)
    DATA_TYPE_ORDER_RSP = 111,        // 委托响应
    DATA_TYPE_CANCEL_RSP = 112,       // 撤单响应
    DATA_TYPE_BANK_SECURITIES_TRANSFER_RSP = 113, // 银证转账响应
    DATA_TYPE_FUND_TRANSFER_RSP = 114, 
    
    // 系统数据类型 (201-300)
    DATA_TYPE_SYSTEM_STATUS = 201,    // 系统状态
    DATA_TYPE_SYSTEM_LOG = 202,       // 系统日志
    DATA_TYPE_SYSTEM_CONFIG = 203,    // 系统配置
} FTDataType;

// 状态枚举
typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR = -1,
    STATUS_INITIALIZED = 1,
    STATUS_CONNECTING = 2,
    STATUS_CONNECTED = 3,
    STATUS_DISCONNECTED = 4,
    STATUS_LOGIN_SUCCESS = 5,
    STATUS_LOGOUT_SUCCESS = 6,
    STATUS_LOGIN_FAILED = 7,
} Status;

// 证券基础信息结构
typedef struct {
    char code[32];            // 证券代码
    MarketType market;          // 市场
    char name[64];       // 证券名称(可选)
    char security_type[16];     // 证券类型(股票/基金/债券/期权等)
} SecurityInfo;

typedef struct {
    char code[32];            // 证券代码
} SecurityInfoInConfig;

// 账户信息结构
typedef struct {
    char login_id[32];               // 登陆账号
    char cust_id[32];               // 客户号
    char fund_account_id[32];       // 资金账户ID
    char account_id_sh[32];         // 上海证券账户ID
    char account_id_sz[32];         // 深圳证券账户ID
    char branch_id[32];             // 营业部ID
    char password[64];              // 密码(可选)
} AccountInfo;

// 订阅配置 - 动态版本
typedef struct {
    MarketType* markets;            // 订阅的市场列表(动态分配)
    int market_count;               // 市场数量
    
    FTDataType* data_types;          // 订阅的数据类型列表(动态分配)
    int data_type_count;            // 数据类型数量
    
    SecurityInfoInConfig* securities;  // 订阅的证券列表(动态分配)
    int security_count;                // 证券数量
} SubscriptionConfig;

// 账号配置
typedef struct {
    // 账户列表(动态分配)
    AccountInfo* accounts;
    int account_count;
} AccountList;

// 数据输出类型枚举
typedef enum {
    DATA_OUTPUT_NONE = 0,           // 不输出
    DATA_OUTPUT_SHARED_MEMORY = 1,  // 共享内存
    DATA_OUTPUT_UDP_BROADCAST = 2,  // UDP广播
    DATA_OUTPUT_FILE = 3,           // 文件输出
} DataOutputType;

// 数据输出配置
typedef struct {
    DataOutputType output_type;
    char shm_name[128];        // 共享内存名称
    char file_path[256];  // 文件输出目录
    char file_format[32];      // 文件格式
    int udp_port;              // UDP端口
    char udp_multicast[64];    // UDP组播地址
} DataConfig;

// 性能统计配置
typedef struct {
    bool enabled;                   // 是否启用性能统计，默认false
    uint32_t report_interval_ms;    // 统计报告间隔(毫秒)，默认5000
    bool enable_latency_stats;      // 是否启用延迟统计，默认false
} PerformanceConfig;

// Mock模式配置
typedef struct {
    bool enabled;                   // 是否启用Mock模式，默认false
    char mock_data_file[256];       // Mock数据文件路径（传统模式）
    bool loop_replay;               // 是否循环回放，默认true
    
    // ===== CSV模式新增字段 =====
    bool csv_mode;                  // 是否启用CSV模式，默认false
    char csv_depth_file[256];       // CSV深度数据文件路径
    char csv_index_file[256];       // CSV指数数据文件路径
    char csv_entrust_file[256];     // CSV委托数据文件路径
    char csv_trade_file[256];       // CSV成交数据文件路径
    char csv_depth_fifty_file[256]; // CSV 50档数据文件路径
} MockConfig;

// 共享内存数据包结构
typedef struct {
    uint64_t timestamp;     // 时间戳(微秒)
    FTDataType data_type;   // 数据类型
    uint32_t data_size;     // 数据大小
#ifdef __cplusplus
    char data[1];           // C++兼容：固定长度为1，通过指针算术访问
#else
    char data[];            // C99柔性数组
#endif
} ShmDataPacket;

#ifdef __cplusplus
}
#endif

#endif