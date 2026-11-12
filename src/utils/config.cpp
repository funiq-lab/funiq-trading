#include "utils/config.h"
#include <cstring>

// 字符串到市场类型的转换
MarketType parse_market_type(const char* market_str) {
    if (!market_str) return MARKET_UNKNOWN;
    
    if (strcmp(market_str, "sse") == 0) return MARKET_SSE;
    if (strcmp(market_str, "szse") == 0) return MARKET_SZSE;
    if (strcmp(market_str, "unknown") == 0) return MARKET_UNKNOWN;

    return MARKET_UNKNOWN;
}

// 市场类型到字符串的转换
const char* market_type_to_string(MarketType market) {
    switch (market) {
        case MARKET_SSE: return "sse";
        case MARKET_SZSE: return "szse";
        case MARKET_UNKNOWN: 
        default: return "unknown";
    }
}

// 字符串到数据类型的转换
FTDataType parse_data_type(const char* data_type_str) {
    if (!data_type_str) return DATA_TYPE_INDEX;
    
    // 行情数据类型
    if (strcmp(data_type_str, "index") == 0) return DATA_TYPE_INDEX;
    if (strcmp(data_type_str, "depth") == 0) return DATA_TYPE_DEPTH;
    if (strcmp(data_type_str, "depth_fifty") == 0) return DATA_TYPE_DEPTH_FIFTY;
    if (strcmp(data_type_str, "entrust") == 0) return DATA_TYPE_ENTRUST;
    if (strcmp(data_type_str, "trade") == 0) return DATA_TYPE_TRADE;
    
    // 交易数据类型
    if (strcmp(data_type_str, "trade_order") == 0) return DATA_TYPE_TRADE_ORDER_RTN;
    if (strcmp(data_type_str, "trade_fill") == 0) return DATA_TYPE_TRADE_FILL;
    if (strcmp(data_type_str, "trade_cancel") == 0) return DATA_TYPE_TRADE_CANCEL;
    if (strcmp(data_type_str, "trade_position") == 0) return DATA_TYPE_TRADE_POSITION;
    if (strcmp(data_type_str, "trade_account") == 0) return DATA_TYPE_TRADE_ACCOUNT;
    if (strcmp(data_type_str, "bank_securities_transfer") == 0) return DATA_TYPE_BANK_SECURITIES_TRANSFER;
    if (strcmp(data_type_str, "fund_transfer") == 0) return DATA_TYPE_FUND_TRANSFER;

    // 回调响应
    if (strcmp(data_type_str, "order_rsp") == 0) return DATA_TYPE_ORDER_RSP;
    if (strcmp(data_type_str, "cancel_rsp") == 0) return DATA_TYPE_CANCEL_RSP;
    if (strcmp(data_type_str, "bank_securities_transfer_rsp") == 0) return DATA_TYPE_BANK_SECURITIES_TRANSFER_RSP;
    if (strcmp(data_type_str, "fund_transfer_rsp") == 0) return DATA_TYPE_FUND_TRANSFER_RSP;
    
    // 系统数据类型
    if (strcmp(data_type_str, "system_status") == 0) return DATA_TYPE_SYSTEM_STATUS;
    if (strcmp(data_type_str, "system_log") == 0) return DATA_TYPE_SYSTEM_LOG;
    if (strcmp(data_type_str, "system_config") == 0) return DATA_TYPE_SYSTEM_CONFIG;
    
    return DATA_TYPE_INDEX; // 默认返回指数类型
}

// 数据类型到字符串的转换
const char* data_type_to_string(FTDataType data_type) {
    switch (data_type) {
        // 行情数据类型
        case DATA_TYPE_INDEX: return "index";
        case DATA_TYPE_DEPTH: return "depth";
        case DATA_TYPE_DEPTH_FIFTY: return "depth_fifty";
        case DATA_TYPE_ENTRUST: return "entrust";
        case DATA_TYPE_TRADE: return "trade";
        
        // 交易数据类型
        case DATA_TYPE_TRADE_ORDER_RTN: return "trade_order";
        case DATA_TYPE_TRADE_FILL: return "trade_fill";
        case DATA_TYPE_TRADE_CANCEL: return "trade_cancel";
        case DATA_TYPE_TRADE_POSITION: return "trade_position";
        case DATA_TYPE_TRADE_ACCOUNT: return "trade_account";
        case DATA_TYPE_BANK_SECURITIES_TRANSFER: return "bank_securities_transfer";
        case DATA_TYPE_FUND_TRANSFER: return "fund_transfer";
        
        // 交易响应类型
        case DATA_TYPE_ORDER_RSP: return "order_rsp";
        case DATA_TYPE_CANCEL_RSP: return "cancel_rsp";
        case DATA_TYPE_BANK_SECURITIES_TRANSFER_RSP: return "bank_securities_transfer_rsp";
        case DATA_TYPE_FUND_TRANSFER_RSP: return "fund_transfer_rsp";
        
        // 系统数据类型
        case DATA_TYPE_SYSTEM_STATUS: return "system_status";
        case DATA_TYPE_SYSTEM_LOG: return "system_log";
        case DATA_TYPE_SYSTEM_CONFIG: return "system_config";
        
        default: return "unknown";
    }
}

// 字符串到数据输出类型的转换
DataOutputType parse_data_output_type(const char* output_type_str) {
    if (!output_type_str) return DATA_OUTPUT_NONE;
    
    if (strcmp(output_type_str, "shared_memory") == 0) return DATA_OUTPUT_SHARED_MEMORY;
    if (strcmp(output_type_str, "udp_broadcast") == 0) return DATA_OUTPUT_UDP_BROADCAST;
    if (strcmp(output_type_str, "file") == 0) return DATA_OUTPUT_FILE;
    if (strcmp(output_type_str, "none") == 0) return DATA_OUTPUT_NONE;
    
    return DATA_OUTPUT_NONE;
}

// 数据输出类型到字符串的转换
const char* data_output_type_to_string(DataOutputType output_type) {
    switch (output_type) {
        case DATA_OUTPUT_SHARED_MEMORY: return "shared_memory";
        case DATA_OUTPUT_UDP_BROADCAST: return "udp_broadcast";
        case DATA_OUTPUT_FILE: return "file";
        case DATA_OUTPUT_NONE:
        default: return "none";
    }
}