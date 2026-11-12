#ifndef QUOTE_INTERFACE_H
#define QUOTE_INTERFACE_H

#include "types/quote_types.h"
#include "core/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===== 服务生命周期管理接口 =====

/**
 * 初始化行情服务（使用默认配置文件）
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_init(void);

/**
 * 启动行情服务
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_start(void);

/**
 * 停止行情服务
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_stop(void);

/**
 * 清理行情服务资源
 */
EXPORT_FLAG void quote_release(void);

/** 
 * 获取行情服务处理器
 */
EXPORT_FLAG void* quote_get_parser(void);

// ===== 状态查询接口 =====

/**
 * 检查是否正在运行
 * @return 1运行中，0已停止
 */
EXPORT_FLAG int quote_is_running(void);

/**
 * 检查是否已连接
 * @return 1已连接，0未连接
 */
EXPORT_FLAG int quote_is_connected(void);

/**
 * 获取当前状态
 * @return 状态值
 */
EXPORT_FLAG Status quote_get_status(void);

/**
 * 获取最后的错误信息
 * @return 错误信息字符串
 */
EXPORT_FLAG const char* quote_get_last_error(void);

/**
 * 获取当前解析器ID
 * @return 解析器ID字符串，NULL表示未初始化
 */
EXPORT_FLAG const char* quote_get_parser_id(void);

// ===== 订阅管理接口 =====

/**
 * 设置订阅配置（完整替换当前订阅）
 * @param config 订阅配置
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_subscribe(const SubscriptionConfig* config);

/**
 * 取消所有订阅
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_unsubscribe(void);

/**
 * 添加订阅市场
 * @param markets 市场数组
 * @param count 市场数量
 * @return 实际添加的数量，负数失败
 */
EXPORT_FLAG int quote_add_markets(MarketType* markets, int count);

/**
 * 添加订阅数据类型
 * @param data_types 数据类型数组
 * @param count 数据类型数量
 * @return 实际添加的数量，负数失败
 */
EXPORT_FLAG int quote_add_data_types(FTDataType* data_types, int count);

/**
 * 添加订阅证券
 * @param securities 证券信息数组
 * @param count 证券数量
 * @return 实际添加的数量，负数失败
 */
EXPORT_FLAG int quote_add_securities(SecurityInfo* securities, int count);

/**
 * 移除订阅市场
 * @param markets 市场数组
 * @param count 市场数量
 * @return 实际移除的数量，负数失败
 */
EXPORT_FLAG int quote_remove_markets(MarketType* markets, int count);

/**
 * 移除订阅数据类型
 * @param data_types 数据类型数组
 * @param count 数据类型数量
 * @return 实际移除的数量，负数失败
 */
EXPORT_FLAG int quote_remove_data_types(FTDataType* data_types, int count);

/**
 * 移除订阅证券
 * @param securities 证券信息数组
 * @param count 证券数量
 * @return 实际移除的数量，负数失败
 */
EXPORT_FLAG int quote_remove_securities(SecurityInfo* securities, int count);

// ===== 订阅状态查询接口 =====

/**
 * 检查市场是否已订阅
 * @param market 市场类型
 * @return 1已订阅，0未订阅
 */
EXPORT_FLAG int quote_is_market_subscribed(MarketType market);

/**
 * 检查数据类型是否已订阅
 * @param data_type 数据类型
 * @return 1已订阅，0未订阅
 */
EXPORT_FLAG int quote_is_data_type_subscribed(FTDataType data_type);

/**
 * 检查证券是否已订阅
 * @param code 证券代码
 * @return 1已订阅，0未订阅
 */
EXPORT_FLAG int quote_is_code_subscribed(const char* code);

/**
 * 检查是否应该处理指定数据
 * @param market 市场类型
 * @param data_type 数据类型
 * @param code 证券代码
 * @return 1应该处理，0不应该处理
 */
EXPORT_FLAG int quote_should_process_data(MarketType market, FTDataType data_type, const char* code);

// ===== 回调函数管理 =====

/**
 * 设置指数数据回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG IndexDataCallbackHandle quote_set_index_data_callback(IndexDataCallbackType callback);

/**
 * 移除指数数据回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_index_data_callback(IndexDataCallbackHandle handle);

/**
 * 设置深度数据回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG DepthDataCallbackHandle quote_set_depth_data_callback(DepthDataCallbackType callback);

/**
 * 移除深度数据回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_depth_data_callback(DepthDataCallbackHandle handle);

/**
 * 设置深度50档数据回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG DepthFiftyDataCallbackHandle quote_set_depth_fifty_data_callback(DepthFiftyDataCallbackType callback);

/**
 * 移除深度50档数据回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_depth_fifty_data_callback(DepthFiftyDataCallbackHandle handle);

/**
 * 设置委托数据回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG EntrustDataCallbackHandle quote_set_entrust_data_callback(EntrustDataCallbackType callback);

/**
 * 移除委托数据回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_entrust_data_callback(EntrustDataCallbackHandle handle);

/**
 * 设置成交数据回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG TradeDataCallbackHandle quote_set_trade_data_callback(TradeDataCallbackType callback);

/**
 * 移除成交数据回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_trade_data_callback(TradeDataCallbackHandle handle);

/**
 * 设置状态变化回调函数
 * @param callback 回调函数指针
 * @return 回调句柄，可用if(handle)检查是否设置成功
 */
EXPORT_FLAG StatusCallbackHandle quote_set_status_callback(QuoteStatusCallbackType callback);

/**
 * 移除状态变化回调函数
 * @param handle 回调句柄
 * @return 0成功，负数失败
 */
EXPORT_FLAG int quote_remove_status_callback(StatusCallbackHandle handle);

// ===== 数据获取接口（轮询模式）=====

/**
 * 获取最新指数行情数据
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_index_data(IndexData* data);

/**
 * 获取最新深度行情数据
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_depth_data(DepthData* data);

/**
 * 获取最新深度50档行情数据
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_depth_fifty_data(DepthFiftyData* data);

/**
 * 获取最新委托数据
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_entrust_data(EntrustData* data);

/**
 * 获取最新成交数据
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_trade_data(TradeData* data);

// ===== 按证券代码获取数据接口 =====

/**
 * 获取最新指数行情数据
 * @param code 证券代码
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_index_data_by_code(const char* code, IndexData* data);

/**
 * 获取最新深度行情数据
 * @param code 证券代码
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_depth_data_by_code(const char* code, DepthData* data);

/**
 * 获取最新深度50档行情数据
 * @param code 证券代码
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_depth_fifty_data_by_code(const char* code, DepthFiftyData* data);

/**
 * 获取最新委托数据
 * @param code 证券代码
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_entrust_data_by_code(const char* code, EntrustData* data);

/**
 * 获取最新成交数据
 * @param code 证券代码
 * @param data 输出数据指针
 * @return 1成功获取，0无数据，-1失败
 */
EXPORT_FLAG int quote_get_trade_data_by_code(const char* code, TradeData* data);

#ifdef __cplusplus
}
#endif

#endif // QUOTE_INTERFACE_H