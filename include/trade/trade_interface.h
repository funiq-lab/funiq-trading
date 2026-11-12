#ifndef TRADE_INTERFACE_H
#define TRADE_INTERFACE_H

#include "types/trade_types.h"
#include "core/macros.h"
#include "database/database_manager.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

#define TRADE_ERROR_MSG_SIZE 256

    // ===== 服务生命周期管理接口 =====

    /**
     * 初始化交易服务（使用默认配置文件）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_init(void);

    /**
     * 使用指定配置文件初始化交易服务
     * @param config_file 配置文件路径
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_init_with_config(const char *config_file);

    /**
     * 启动交易服务
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_start(void);

    /**
     * 停止交易服务
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_stop(void);

    /**
     * 清理交易服务资源
     */
    EXPORT_FLAG void trade_release(void);

    /**
     * 获取交易服务处理器
     */
    EXPORT_FLAG void *trade_get_handler(void);

    // ===== 连接和登录管理接口 =====

    /**
     * 连接交易系统
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_connect(void);

    /**
     * 断开交易系统连接
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_disconnect(void);

    // ===== 状态查询接口 =====

    /**
     * 检查是否正在运行
     * @return 1运行中，0已停止
     */
    EXPORT_FLAG int trade_is_running(void);

    /**
     * 检查是否已连接
     * @return 1已连接，0未连接
     */
    EXPORT_FLAG int trade_is_connected(void);

    /**
     * 检查指定账户是否已登录交易系统
     * @param login_id 登陆账号
     * @return 1已登录，0未登录
     * @note 线程安全，查询指定账户的登录状态
     */
    EXPORT_FLAG int trade_is_account_login(const char *login_id);

    /**
     * 获取所有已登录账户的登陆账号列表
     * @param account_ids 输出登陆账号数组（调用者负责释放内存）
     * @param count 输出账户数量
     * @return 0成功，负数失败
     * @note 线程安全
     */
    EXPORT_FLAG int trade_get_logged_in_login_ids(char ***account_ids, int *count);

    /**
     * 获取已登录账户总数
     * @return 已登录账户数量，负数表示错误
     * @note 线程安全
     */
    EXPORT_FLAG int trade_get_logged_in_account_count(void);

    /**
     * 获取当前状态
     * @return 状态值
     */
    EXPORT_FLAG Status trade_get_status(void);

    /**
     * 获取最后的错误信息
     * @return 错误信息字符串
     */
    EXPORT_FLAG const char *trade_get_last_error(void);

    /**
     * 获取当前交易接口ID
     * @return 接口ID字符串，NULL表示未初始化
     */
    EXPORT_FLAG const char *trade_get_interface_id(void);

    // ===== 多账户管理接口 =====

    /**
     * 根据登陆账号获取账户信息
     * @param login_id 登陆账号
     * @param account 输出账户信息
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_get_account_info_by_id(const char *login_id, AccountInfo *account);

    /**
     * 获取所有账户的登陆账号列表
     * @param account_ids 输出登陆账号数组（调用者负责释放内存）
     * @param count 输出账户数量
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_get_all_login_ids(char ***account_ids, int *count);

    /**
     * 释放账户ID列表内存
     * @param account_ids 账户ID数组
     * @param count 数组大小
     */
    EXPORT_FLAG void trade_free_account_ids(char **account_ids, int count);

    /**
     * 获取账户数量
     * @return 账户数量，负数表示错误
     */
    EXPORT_FLAG int trade_get_account_count(void);

    /**
     * 检查是否存在指定账户
     * @param login_id 登陆账号
     * @return 1存在，0不存在
     */
    EXPORT_FLAG int trade_has_account(const char *login_id);

    /**
     * 根据完整账户标识获取最新资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param account_id 证券账户ID（用于区分沪深两市）
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_account_data_by_id(const char *login_id, const char *fund_account_id, const char *account_id, AccountData *data);

    /**
     * 根据登陆账号和资金账户获取所有市场的资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param sh_data 上海市场资金数据（可为NULL）
     * @param sz_data 深圳市场资金数据（可为NULL）
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_all_account_data_by_fund_id(const char *login_id, const char *fund_account_id, AccountData *sh_data, AccountData *sz_data);

    /**
     * 根据登陆账号获取所有资金账户的所有市场数据
     * @param login_id 登陆账号
     * @param account_data_array 输出账户数据数组（调用者负责释放内存）
     * @param count 输出数据数量
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_get_all_account_data_by_login_id(const char *login_id, AccountData **account_data_array, int *count);

    // ===== 交易操作接口 =====

    /**
     * 委托下单
     * @param order 下单请求
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_send_order(const TradeOrderRequest *order);

    /**
     * 同步委托下单
     * @param order 下单请求
     * @param result 输出结果数据
     * @param timeout_ms 超时时间（毫秒）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_send_order_sync(const TradeOrderRequest *order, TradeOrderRspData *result, int timeout_ms);

    /**
     * 撤单
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_cancel_order(const AccountInfo *account, const int64_t orig_order_id,
                                           uint64_t orig_batch_id, MarketType market);

    /**
     * 同步撤单
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @param result 输出结果数据
     * @param timeout_ms 超时时间（毫秒）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_cancel_order_sync(const AccountInfo *account, const int64_t orig_order_id,
                                                uint64_t orig_batch_id, MarketType market,
                                                TradeOrderCancelRsqData *result, int timeout_ms);

    // ===== 查询接口 =====
    /**
     * 查询订单
     * @param account 账户信息
     * @param market 市场代码（必填，用于指定查询哪个市场）
     * @param security_code 证券代码（可选，NULL表示查所有）
     * @param order_id 订单编号（可选，0表示查所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_query_orders(const AccountInfo *account, MarketType market, const char *security_code,
                                           const int64_t order_id, const QueryRequestParams *params);

    /**
     * 同步查询订单
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param order_id 订单编号（可选）
     * @param params 查询参数
     * @param results 输出结果数组（调用者负责释放内存）
     * @param count 输出结果数量
     * @param timeout_ms 超时时间（毫秒）
     * @param error_msg 错误信息输出缓冲区（可选，NULL表示不需要，建议大小为TRADE_ERROR_MSG_SIZE）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_query_orders_sync(const AccountInfo *account, MarketType market,
                                                const char *security_code, const int64_t order_id,
                                                const QueryRequestParams *params,
                                                TradeOrderData **results, int *count, int timeout_ms,
                                                char *error_msg);

    /**
     * 查询持仓
     * @param account 账户信息
     * @param market 市场代码（必填，用于指定查询哪个市场）
     * @param security_code 证券代码（可选，NULL表示查所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_query_positions(const AccountInfo *account, MarketType market, const char *security_code, const QueryRequestParams *params);

    /**
     * 同步查询持仓
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param params 查询参数
     * @param results 输出结果数组（调用者负责释放内存）
     * @param count 输出结果数量
     * @param timeout_ms 超时时间（毫秒）
     * @param error_msg 错误信息输出缓冲区（可选，NULL表示不需要，建议大小为TRADE_ERROR_MSG_SIZE）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_query_positions_sync(const AccountInfo *account, MarketType market,
                                                   const char *security_code,
                                                   const QueryRequestParams *params,
                                                   PositionData **results, int *count, int timeout_ms,
                                                   char *error_msg);

    /**
     * 查询资金
     * @param account 账户信息
     * @param market 市场代码（必填，用于指定查询哪个市场的资金）
     * @param currency 货币种类（可选，NULL表示默认）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_query_funds(const AccountInfo *account, MarketType market, const char *currency, const QueryRequestParams *params);

    /**
     * 同步查询资金
     * @param account 账户信息
     * @param market 市场代码
     * @param currency 货币种类（可选）
     * @param params 查询参数
     * @param results 输出结果数组（调用者负责释放内存）
     * @param count 输出结果数量
     * @param timeout_ms 超时时间（毫秒）
     * @param error_msg 错误信息输出缓冲区（可选，NULL表示不需要，建议大小为TRADE_ERROR_MSG_SIZE）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_query_funds_sync(const AccountInfo *account, MarketType market,
                                               const char *currency,
                                               const QueryRequestParams *params,
                                               AccountData **results, int *count, int timeout_ms,
                                               char *error_msg);
    /**
     * 查询成交
     * @param account 账户信息
     * @param market 市场代码（必填，用于指定查询哪个市场）
     * @param security_code 证券代码（可选，NULL表示查所有）
     * @param order_id 订单编号（可选，0表示查所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_query_trades(const AccountInfo *account, MarketType market, const char *security_code,
                                           const int64_t order_id, const QueryRequestParams *params);

    /**
     * 同步查询成交
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param order_id 订单编号（可选）
     * @param params 查询参数
     * @param results 输出结果数组（调用者负责释放内存）
     * @param count 输出结果数量
     * @param timeout_ms 超时时间（毫秒）
     * @param error_msg 错误信息输出缓冲区（可选，NULL表示不需要，建议大小为TRADE_ERROR_MSG_SIZE）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_query_trades_sync(const AccountInfo *account, MarketType market,
                                                const char *security_code, const int64_t order_id,
                                                const QueryRequestParams *params,
                                                TradeFillData **results, int *count, int timeout_ms,
                                                char *error_msg);

    /**
     * 银证转账
     * @param account 账户信息
     * @param direction 转账方向（1-银证转入，2-银证转出）
     * @param market 市场代码
     * @param transfer_amount 转账金额
     * @param currency 货币种类（可选，默认CNY）
     * @param bank_password 银行密码（可选）
     * @param fund_password 资金密码（可选）
     * @param remark 备注（可选）
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_bank_securities_transfer(const AccountInfo *account,
                                                       BankSecuritiesTransferDirection direction,
                                                       MarketType market,
                                                       double transfer_amount,
                                                       const char *currency,
                                                       const char *bank_password,
                                                       const char *fund_password,
                                                       const char *remark);

    /**
     * 同步银证转账
     * @param account 账户信息
     * @param direction 转账方向
     * @param market 市场代码
     * @param transfer_amount 转账金额
     * @param currency 货币种类（可选）
     * @param bank_password 银行密码（可选）
     * @param fund_password 资金密码（可选）
     * @param remark 备注（可选）
     * @param result 输出结果数据
     * @param timeout_ms 超时时间（毫秒）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_bank_securities_transfer_sync(const AccountInfo *account,
                                                            BankSecuritiesTransferDirection direction,
                                                            MarketType market,
                                                            double transfer_amount,
                                                            const char *currency,
                                                            const char *bank_password,
                                                            const char *fund_password,
                                                            const char *remark,
                                                            BankSecuritiesTransferData *result,
                                                            int timeout_ms);

    /**
     * 沪深资金划拨
     * @param account 账户信息
     * @param fund_out_account_id 资金转出账户ID
     * @param fund_out_market 资金转出市场
     * @param fund_in_account_id 资金转入账户ID
     * @param fund_in_market 资金转入市场
     * @param transfer_amount 划拨金额
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     */
    EXPORT_FLAG int64_t trade_fund_transfer(const AccountInfo *account,
                                            const char *fund_out_account_id,
                                            MarketType fund_out_market,
                                            const char *fund_in_account_id,
                                            MarketType fund_in_market,
                                            double transfer_amount);
    /**
     * 同步资金划拨
     * @param account 账户信息
     * @param fund_out_account_id 资金转出账户ID
     * @param fund_out_market 资金转出市场
     * @param fund_in_account_id 资金转入账户ID
     * @param fund_in_market 资金转入市场
     * @param transfer_amount 划拨金额
     * @param result 输出结果数据
     * @param timeout_ms 超时时间（毫秒）
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int32_t trade_fund_transfer_sync(const AccountInfo *account,
                                                 const char *fund_out_account_id,
                                                 MarketType fund_out_market,
                                                 const char *fund_in_account_id,
                                                 MarketType fund_in_market,
                                                 double transfer_amount,
                                                 FundTransferData *result,
                                                 int timeout_ms);

    // ===== 回调函数管理 =====

    /**
     * 设置订单回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeOrderQueryCallbackHandle trade_set_order_query_callback(TradeOrderQueryCallbackType callback);

    /**
     * 移除订单回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_order_query_callback(TradeOrderQueryCallbackHandle handle);

    /**
     * 设置成交回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeFillCallbackHandle trade_set_fill_callback(TradeFillCallbackType callback);

    /**
     * 移除成交回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_fill_callback(TradeFillCallbackHandle handle);

    /**
     * 设置持仓回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG PositionCallbackHandle trade_set_position_callback(PositionCallbackType callback);

    /**
     * 移除持仓回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_position_callback(PositionCallbackHandle handle);

    /**
     * 设置资金回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG AccountCallbackHandle trade_set_account_callback(AccountCallbackType callback);

    /**
     * 移除资金回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_account_callback(AccountCallbackHandle handle);

    /**
     * 设置错误回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeErrorCallbackHandle trade_set_error_callback(TradeErrorCallbackType callback);

    /**
     * 移除错误回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_error_callback(TradeErrorCallbackHandle handle);

    /**
     * 设置银证转账回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG BankSecuritiesTransferCallbackHandle trade_set_bank_securities_transfer_callback(BankSecuritiesTransferCallbackType callback);

    /**
     * 移除银证转账回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_bank_securities_transfer_callback(BankSecuritiesTransferCallbackHandle handle);

    /**
     * 设置资金划拨回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG FundTransferCallbackHandle trade_set_fund_transfer_callback(FundTransferCallbackType callback);

    /**
     * 移除资金划拨回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_fund_transfer_callback(FundTransferCallbackHandle handle);

    /**
     * 设置委托响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeOrderRspCallbackHandle trade_set_order_rsp_callback(TradeOrderRspCallbackType callback);

    /**
     * 移除委托响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_order_rsp_callback(TradeOrderRspCallbackHandle handle);

    /**
     * 设置委托主推回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeOrderCallbackHandle trade_set_order_rtn_callback(TradeOrderCallbackType callback);

    /**
     * 移除委托主推回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_order_rtn_callback(TradeOrderCallbackHandle handle);

    /**
     * 设置撤单响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄（可用if(handle)检查是否设置成功）
     */
    EXPORT_FLAG TradeOrderCancelRspCallbackHandle trade_set_cancel_rsp_callback(TradeOrderCancelRspCallbackType callback);

    /**
     * 移除撤单响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    EXPORT_FLAG int trade_remove_cancel_rsp_callback(TradeOrderCancelRspCallbackHandle handle);

    // ===== 数据获取接口（轮询模式）=====
    /**
     * 获取最新银证转账数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_bank_securities_transfer_data(BankSecuritiesTransferData *data);

    /**
     * 获取最新资金划拨数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_fund_transfer_data(FundTransferData *data);

    /**
     * 获取最新委托响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_order_rsp_data(TradeOrderRspData *data);

    /**
     * 获取最新撤单响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_cancel_rsp_data(TradeOrderCancelRsqData *data);

    /**
     * 获取最新订单数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_order_data(TradeOrderData *data);

    /**
     * 获取最新成交数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_fill_data(TradeFillData *data);

    /**
     * 获取最新持仓数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     */
    EXPORT_FLAG int trade_get_position_data(PositionData *data);

    /**
     * 释放查询结果数组内存
     * @param results 结果数组
     * @param count 数组大小
     */
    EXPORT_FLAG void trade_free_query_results(void *results, int count);

    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     */
    EXPORT_FLAG int trade_set_db_manager(void *db_manager);

    /**
     * 获取数据库管理器
     * @return 数据库管理器指针
     */
    EXPORT_FLAG void *trade_get_db_manager();

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器指针
     */
    EXPORT_FLAG int trade_set_quote_parser(void *quote_parser);

    /**
     * 获取行情解析器
     * @return 行情解析器指针
     */
    EXPORT_FLAG void *trade_get_quote_parser();

#ifdef __cplusplus
}
#endif

#endif // TRADE_INTERFACE_H