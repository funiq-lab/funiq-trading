#ifndef STRATEGY_INTERFACE_H
#define STRATEGY_INTERFACE_H

#include "types/strategy_types.h"
#include "database/database_manager.hpp"
#include "core/macros.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // ===== 服务生命周期管理接口 =====

    /**
     * 初始化策略服务（使用默认配置文件）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_init(void);

    /**
     * 使用指定配置文件初始化策略服务
     * @param config_file 配置文件路径
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_init_with_config(const char *config_file);

    /**
     * 启动策略服务
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_start_service(void);

    /**
     * 停止策略服务
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_stop_service(void);

    /**
     * 清理策略服务资源
     */
    EXPORT_FLAG void strategy_release(void);

    /**
     * 获取策略服务实例
     * @return 策略服务实例指针
     */
    EXPORT_FLAG void *strategy_get_service_instance(void);

    // ===== 策略实例管理接口 =====

    /**
     * 创建策略实例
     * @param strategy_id 策略ID
     * @param config JSON格式配置字符串，NULL使用默认配置
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_create_instance(const char *strategy_id, const char *config);

    /**
     * 启动指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_start_instance(const char *strategy_id);

    /**
     * 停止指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_stop_instance(const char *strategy_id);

    /**
     * 销毁指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_destroy_instance(const char *strategy_id);

    /**
     * 检查策略实例是否存在
     * @param strategy_id 策略ID
     * @return 1存在，0不存在
     */
    EXPORT_FLAG int strategy_instance_exists(const char *strategy_id);

    /**
     * 检查策略实例是否活跃（运行中）
     * @param strategy_id 策略ID
     * @return 1活跃，0未活跃
     */
    EXPORT_FLAG int strategy_instance_is_active(const char *strategy_id);

    /**
     * 获取策略实例状态
     * @param strategy_id 策略ID
     * @return 状态值，-1表示策略不存在
     */
    EXPORT_FLAG int strategy_get_instance_status(const char *strategy_id);

    // ===== 批量策略操作接口 =====

    /**
     * 启动所有策略实例
     * @return 成功启动的策略数量
     */
    EXPORT_FLAG int strategy_start_all_instances(void);

    /**
     * 停止所有活跃策略实例
     * @return 成功停止的策略数量
     */
    EXPORT_FLAG int strategy_stop_all_instances(void);

    /**
     * 销毁所有策略实例
     * @return 成功销毁的策略数量
     */
    EXPORT_FLAG int strategy_destroy_all_instances(void);

    // ===== 状态查询接口 =====

    /**
     * 检查服务是否正在运行
     * @return 1运行中，0已停止
     */
    EXPORT_FLAG int strategy_is_service_running(void);

    /**
     * 检查服务是否已初始化
     * @return 1已初始化，0未初始化
     */
    EXPORT_FLAG int strategy_is_service_initialized(void);

    /**
     * 获取活跃策略实例数量
     * @return 活跃策略数量
     */
    EXPORT_FLAG size_t strategy_get_active_instance_count(void);

    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     */
    EXPORT_FLAG const char *strategy_get_last_error(void);

    /**
     * 获取所有已注册策略ID列表（JSON格式）
     * @return JSON数组字符串，包含所有已注册策略ID
     */
    EXPORT_FLAG const char *strategy_get_registered_strategy_ids(void);

    /**
     * 获取所有活跃策略ID列表（JSON格式）
     * @return JSON数组字符串，包含所有活跃策略ID
     */
    EXPORT_FLAG const char *strategy_get_active_strategy_ids(void);

    /**
     * 获取线程池状态信息（JSON格式）
     * @return JSON格式的线程池状态信息
     */
    EXPORT_FLAG const char *strategy_get_thread_pool_status(void);

    // ===== 策略配置管理接口 =====

    /**
     * 更新指定策略配置
     * @param strategy_id 策略ID
     * @param config JSON格式配置字符串
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_update_instance_config(const char *strategy_id, const char *config);

    /**
     * 获取指定策略配置
     * @param strategy_id 策略ID
     * @return JSON格式配置字符串
     */
    EXPORT_FLAG const char *strategy_get_instance_config(const char *strategy_id);

    // ===== 挂单队列管理接口 =====

    /**
     * 新增挂单到指定策略
     * @param strategy_id 策略ID
     * @param order JSON格式挂单字符串
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_add_order_queue(const char *strategy_id, const char *order);

    /**
     * 更新指定挂单
     * @param strategy_id 策略ID
     * @param order_id 挂单ID
     * @param order JSON格式挂单字符串
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_update_order_queue(const char *strategy_id, int64_t order_id, const char *order);

    /**
     * 取消指定挂单
     * @param strategy_id 策略ID
     * @param order_id 挂单ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_cancel_order_queue(const char *strategy_id, int64_t order_id);

    /**
     * 删除指定策略的挂单
     * @param strategy_id 策略ID
     * @param order_id 挂单ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_remove_order_queue(const char *strategy_id, int64_t order_id);

    /**
     * 批量新增挂单到指定策略
     * @param strategy_id 策略ID
     * @param orders JSON格式挂单数组字符串
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_batch_add_order_queue(const char *strategy_id, const char *orders);

    /**
     * 取消指定策略的所有活跃挂单
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_cancel_all_active_order_queue(const char *strategy_id);

    /**
     * 删除指定策略的所有活跃挂单
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_remove_all_active_order_queue(const char *strategy_id);

    /**
     * 获取指定策略的挂单队列
     * @param strategy_id 策略ID
     * @return JSON格式挂单数组字符串
     */
    EXPORT_FLAG const char *strategy_get_order_queue(const char *strategy_id);

    // ===== 策略数据获取接口 =====

    /**
     * 获取指定策略运行时数据
     * @param strategy_id 策略ID
     * @param data_type 数据类型（如 "orders", "positions", "metrics", "status"）
     * @return JSON格式数据字符串，NULL表示不支持该数据类型或策略不存在
     */
    EXPORT_FLAG const char *strategy_get_instance_data(const char *strategy_id, const char *data_type);

    /**
     * 获取指定策略支持的所有数据类型列表
     * @param strategy_id 策略ID
     * @return JSON数组字符串，包含所有支持的data_type
     */
    EXPORT_FLAG const char *strategy_get_available_data_types(const char *strategy_id);

    /**
     * 设置指定策略数据变化回调函数
     * @param strategy_id 策略ID
     * @param callback 回调函数指针
     * @return 回调句柄，可用于后续删除回调
     * @note 回调函数在策略数据变化时执行，应避免阻塞操作
     */
    EXPORT_FLAG StrategyDataCallbackHandle strategy_set_data_change_callback(const char *strategy_id, StrategyDataCallbackType callback);

    /**
     * 移除指定策略数据变化回调函数
     * @param strategy_id 策略ID
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_remove_data_change_callback(const char *strategy_id, StrategyDataCallbackHandle handle);

    // ===== 策略元信息查询接口 =====

    /**
     * 获取策略名称
     * @param strategy_id 策略ID
     * @return 策略名称字符串
     */
    EXPORT_FLAG const char *strategy_get_strategy_name(const char *strategy_id);

    /**
     * 获取策略描述
     * @param strategy_id 策略ID
     * @return 策略描述字符串
     */
    EXPORT_FLAG const char *strategy_get_strategy_description(const char *strategy_id);

    /**
     * 检查策略是否已注册
     * @param strategy_id 策略ID
     * @return 1已注册，0未注册
     */
    EXPORT_FLAG int strategy_is_strategy_registered(const char *strategy_id);

    // ===== 依赖服务管理接口 =====

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器指针（来自行情服务）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_set_quote_parser(void *quote_parser);

    /**
     * 设置交易处理器
     * @param trade_handler 交易处理器指针（来自交易服务）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_set_trade_handler(void *trade_handler);

    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     */
    EXPORT_FLAG int strategy_set_db_manager(void *db_manager);

    /**
     * 获取数据库管理器
     * @return 数据库管理器指针
     */
    EXPORT_FLAG void *strategy_get_db_manager();

    /**
     * 获取行情解析器
     * @return 行情解析器指针
     */
    EXPORT_FLAG void *strategy_get_quote_parser(void);

    /**
     * 获取交易处理器
     * @return 交易处理器指针
     */
    EXPORT_FLAG void *strategy_get_trade_handler(void);

    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int strategy_set_trading_time_manager(void *trading_time_manager);

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    EXPORT_FLAG void *strategy_get_trading_time_manager(void);

    // ===== 数据分发接口 =====

    /**
     * 分发指数数据到所有活跃策略
     * @param data 指数数据指针
     */
    EXPORT_FLAG void strategy_dispatch_index_data(const void *data);

    /**
     * 分发深度数据到所有活跃策略
     * @param data 深度数据指针
     */
    EXPORT_FLAG void strategy_dispatch_depth_data(const void *data);

    /**
     * 分发50笔深度数据到所有活跃策略
     * @param data 50笔深度数据指针
     */
    EXPORT_FLAG void strategy_dispatch_depth_fifty_data(const void *data);

    /**
     * 分发委托数据到所有活跃策略
     * @param data 委托数据指针
     */
    EXPORT_FLAG void strategy_dispatch_entrust_data(const void *data);

    /**
     * 分发成交数据到所有活跃策略
     * @param data 成交数据指针
     */
    EXPORT_FLAG void strategy_dispatch_trade_data(const void *data);

    /**
     * 分发订单数据到所有活跃策略
     * @param data 订单数据指针
     */
    EXPORT_FLAG void strategy_dispatch_order_rtn_data(const void *data);

    /**
     * 分发订单查询数据到所有活跃策略
     * @param data 订单查询数据指针
     */
    EXPORT_FLAG void strategy_dispatch_order_query_data(const void *data);

    /**
     * 分发成交回报数据到所有活跃策略
     * @param data 成交回报数据指针
     */
    EXPORT_FLAG void strategy_dispatch_fill_data(const void *data);

    /**
     * 分发持仓数据到所有活跃策略
     * @param data 持仓数据指针
     */
    EXPORT_FLAG void strategy_dispatch_position_data(const void *data);

    /**
     * 分发账户数据到所有活跃策略
     * @param data 账户数据指针
     */
    EXPORT_FLAG void strategy_dispatch_account_data(const void *data);

    /**
     * 分发交易错误到所有活跃策略
     * @param error_code 错误代码
     * @param error_msg 错误信息
     */
    EXPORT_FLAG void strategy_dispatch_trade_error(int error_code, const char *error_msg);

    /**
     * 分发委托响应数据到所有活跃策略
     * @param data 委托响应数据指针
     */
    EXPORT_FLAG void strategy_dispatch_order_rsp_data(const void *data);

    /**
     * 分发撤单响应数据到所有活跃策略
     * @param data 撤单响应数据指针
     */
    EXPORT_FLAG void strategy_dispatch_cancel_rsp_data(const void *data);

    // ===== 全局策略模块管理接口 =====

    /**
     * 获取已注册的策略模块列表（JSON格式）
     * @return JSON数组字符串，包含所有已注册模块名称
     */
    EXPORT_FLAG const char *strategy_get_registered_modules(void);

    /**
     * 检查策略模块是否已注册
     * @param module_name 模块名称
     * @return 1已注册，0未注册
     */
    EXPORT_FLAG int strategy_is_module_registered(const char *module_name);

#ifdef __cplusplus
}
#endif

#endif // STRATEGY_INTERFACE_H