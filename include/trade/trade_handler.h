#ifndef TRADE_HANDLER_H
#define TRADE_HANDLER_H

#include "utils/sync_call_framework.h"
#include "types/trade_types.h"
#include "database/database_manager.hpp"
#include "core/macros.h"
#include "quote/quote_parser.h"

// ===== C 接口部分 - 全局命名空间 =====
#ifdef __cplusplus
extern "C"
{
#endif

    // 前向声明基类接口
    typedef struct ITradeHandler ITradeHandler;

    /**
     * 创建交易处理器实例
     *
     * 这是动态库的工厂函数，用于创建具体的交易处理器实例。
     * 每个交易处理器动态库都必须实现这个函数。
     *
     * @return 处理器实例指针，创建失败时返回NULL
     *
     * @note 这是C接口，用于动态库加载
     * @note 返回的实例需要调用delete_trade_handler()释放
     * @note 函数名必须严格匹配，用于dlsym查找
     */
    EXPORT_FLAG ITradeHandler *create_trade_handler(void);

    /**
     * 销毁交易处理器实例
     *
     * 释放由create_trade_handler()创建的处理器实例。
     * 会自动调用实例的release()方法清理资源。
     *
     * @param handler 处理器实例指针的引用，销毁后会被设置为NULL
     *
     * @note 这是C接口，用于动态库加载
     * @note 传入NULL指针是安全的（不执行任何操作）
     * @note 函数名必须严格匹配，用于dlsym查找
     */
    EXPORT_FLAG void delete_trade_handler(ITradeHandler *&handler);

#ifdef __cplusplus
}

// ===== C++ 接口部分 - ft 命名空间 =====
#include <string>
#include <memory>
#include <vector>

NS_FT_BEGIN

/**
 * 交易处理器抽象接口类
 *
 * 定义了所有交易处理器必须实现的标准接口，支持完整的交易功能：
 *
 * **核心功能**：
 * - 生命周期管理（初始化、启动、连接、停止、清理）
 * - 多账户管理（支持多个客户账户的统一管理）
 * - 交易操作（下单、撤单、查询）
 * - 银证转账（银行与证券账户间的资金转移）
 * - 资金划拨（沪深两市间的资金调拨）
 * - 数据获取（轮询模式和回调模式）
 * - 性能统计（延迟和吞吐量监控）
 *
 * **设计原则**：
 * - 接口与实现分离：具体处理器通过动态库实现
 * - 支持多种交易系统：统一接口适配不同的交易接口
 * - 线程安全：接口设计考虑多线程环境
 * - 高性能：支持高频交易和低延迟要求
 * - 多账户支持：原生支持多个交易账户管理
 *
 * **账户管理模型**：
 * - 登陆账号(login_id)：唯一标识一个客户
 * - 客户号(cust_id)：唯一标识一个客户
 * - 资金账户(fund_account_id)：客户的资金管理账户
 * - 证券账户(account_id)：分沪市和深市，用于证券交易
 * - 支持一个客户多个资金账户，一个资金账户对应沪深两个证券账户
 *
 * **使用模式**：
 * 1. 通过动态库工厂函数创建实例
 * 2. 调用init()初始化配置（包含多账户信息）
 * 3. 调用start()启动服务
 * 4. 调用connect()连接交易系统
 * 5. 设置回调或轮询获取数据
 * 6. 进行交易操作（下单、撤单、查询等）
 * 7. 调用disconnect()、stop()、release()清理
 */
class ITradeHandler
{
public:
    /**
     * 虚析构函数
     * 确保派生类能够正确析构
     */
    virtual ~ITradeHandler() {}

    // ===== 生命周期管理接口 =====

    /**
     * 初始化交易处理器
     *
     * 根据配置信息初始化处理器的各个组件，包括：
     * - 连接参数配置
     * - 多账户信息配置
     * - 性能统计配置
     * - 数据输出配置
     *
     * @param config 交易配置结构体指针，包含所有初始化参数
     * @return 0成功，负数失败
     *
     * @pre config不能为NULL且必须包含有效配置
     * @post 成功时处理器进入已初始化状态
     * @note 可以重复调用，但只有第一次调用生效
     */
    virtual int init(const TradeConfig *config) = 0;

    /**
     * 启动交易处理器服务
     *
     * 启动内部处理线程和服务组件，准备接收数据。
     * 不会自动连接到交易系统，需要单独调用connect()。
     *
     * @return 0成功，负数失败
     *
     * @pre 必须先成功调用init()
     * @post 成功时处理器进入运行状态
     * @note 重复调用是安全的
     */
    virtual int start() = 0;

    /**
     * 连接到交易系统
     *
     * 建立到交易服务器的网络连接，执行登录认证。
     * 支持断线重连机制。
     *
     * @return 0成功，负数失败
     *
     * @pre 必须先成功调用start()
     * @post 成功时可以进行交易操作
     * @note 支持重连，连接失败时可以重试
     */
    virtual int connect() = 0;

    /**
     * 断开交易系统连接
     *
     * 断开网络连接但保持服务运行状态。
     * 不影响已缓存的数据和配置。
     *
     * @return 0成功，负数失败
     *
     * @post 停止接收新的交易数据
     * @note 可以重新调用connect()恢复连接
     */
    virtual int disconnect() = 0;

    /**
     * 释放交易处理器资源
     *
     * 清理所有资源，包括：
     * - 停止所有线程
     * - 断开所有连接
     * - 释放内存缓冲区
     * - 清理配置信息
     *
     * @post 处理器回到未初始化状态
     * @note 此方法是幂等的，可以安全地多次调用
     */
    virtual void release() = 0;

    /**
     * 停止交易处理器服务
     *
     * 停止所有处理线程，但保持初始化状态。
     * 会自动断开连接。
     *
     * @return 0成功，负数失败
     *
     * @post 处理器进入停止状态
     * @note 可以重新调用start()恢复服务
     */
    virtual int stop() = 0;

    // ===== 性能管理接口 =====

    /**
     * 获取订单延迟统计
     * @param avg_latency_us 平均延迟（微秒）
     * @param max_latency_us 最大延迟（微秒）
     * @param min_latency_us 最小延迟（微秒）
     */
    virtual void get_order_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us) = 0;

    /**
     * 获取撤单延迟统计
     * @param avg_latency_us 平均延迟（微秒）
     * @param max_latency_us 最大延迟（微秒）
     * @param min_latency_us 最小延迟（微秒）
     */
    virtual void get_cancel_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us) = 0;

    /**
     * 重置性能统计数据
     * @note 清零所有统计计数器，重新开始统计
     */
    virtual void reset_performance_stats() = 0;

    // ===== 连接状态查询接口 =====

    /**
     * 检查是否已连接到交易系统
     * @return true已连接，false未连接
     * @note 线程安全
     */
    virtual bool is_connected() const = 0;

    /**
     * 检查指定账户是否已登录交易系统
     * @param login_id 登陆账号
     * @return true已登录，false未登录
     * @note 线程安全，查询指定账户的登录状态
     */
    virtual bool is_account_login(const char *login_id) const = 0;

    /**
     * 获取所有已登录账户的登陆账号列表
     * @return 已登录账户的登陆账号字符串向量
     * @note 线程安全
     */
    virtual std::vector<std::string> get_logged_in_login_ids() const = 0;

    /**
     * 获取已登录账户总数
     * @return 已登录账户数量，负数表示错误
     * @note 线程安全
     */
    virtual int get_logged_in_account_count() const = 0;

    /**
     * 检查服务是否正在运行
     * @return true运行中，false已停止
     * @note 线程安全
     */
    virtual bool is_running() const = 0;

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     * @note 线程安全
     */
    virtual bool is_initialized() const = 0;

    /**
     * 获取当前运行状态
     * @return 状态枚举值
     * @note 线程安全，提供比布尔值更详细的状态信息
     */
    virtual Status get_status() = 0;

    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     * @note 线程安全，返回最近一次操作的错误信息
     */
    virtual std::string get_last_error() const = 0;

    // ===== 多账户管理接口 =====

    /**
     * 根据登陆账号获取账户信息
     * @param login_id 登陆账号
     * @param account 输出账户信息
     * @return 0成功，负数失败
     * @note 线程安全，从内存缓存中获取
     */
    virtual int get_account_info_by_id(const char *login_id, AccountInfo *account) = 0;

    /**
     * 获取所有账户的登陆账号列表
     * @return 登陆账号字符串向量
     * @note 线程安全，返回所有已配置账户的登陆账号
     */
    virtual std::vector<std::string> get_all_login_ids() const = 0;

    /**
     * 获取账户总数
     * @return 账户数量，负数表示错误
     * @note 线程安全
     */
    virtual int get_account_count() const = 0;

    /**
     * 检查是否存在指定账户
     * @param login_id 登陆账号
     * @return true存在，false不存在
     * @note 线程安全
     */
    virtual bool has_account(const char *login_id) const = 0;

    // ===== 交易操作接口 =====

    /**
     * 委托下单
     *
     * 向交易系统发送委托下单请求。支持多种订单类型和交易市场。
     *
     * @param order 下单请求数据，包含账户、证券、价格、数量等信息
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     *
     * @pre 必须已连接并登录
     * @pre order中的账户信息必须是已配置的有效账户
     * @post 异步操作，结果通过回调或轮询获取
     * @note 线程安全，支持并发下单
     * @note 返回的request_id全局唯一，用于关联响应和回调
     */
    virtual int64_t send_order(const TradeOrderRequest *order) = 0;

    /**
     * 同步下单
     * @param order 订单数据
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    virtual SyncResult<TradeOrderRspData> send_order_sync(const TradeOrderRequest *order, int timeout_ms = 30000) = 0;

    /**
     * 撤销委托
     *
     * 撤销之前提交的委托订单。
     *
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     *
     * @pre 必须已连接并登录
     * @pre 原订单必须存在且可撤销
     * @post 异步操作，结果通过回调或轮询获取
     * @note 线程安全，支持并发撤单
     */
    virtual int64_t cancel_order(const AccountInfo *account, const int64_t orig_order_id,
                                 uint64_t orig_batch_id, MarketType market) = 0;

    /**
     * 同步撤单
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    virtual SyncResult<TradeOrderCancelRsqData> cancel_order_sync(const AccountInfo *account, const int64_t orig_order_id,
                                                                  uint64_t orig_batch_id, MarketType market, int timeout_ms = 30000) = 0;

    // ===== 查询接口 =====

    /**
     * 查询订单信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param order_id 订单编号（可选，0表示查询所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    virtual int64_t query_orders(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                                 const int64_t order_id = 0, const QueryRequestParams *params = nullptr) = 0;

    /**
     * 同步查询订单信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param order_id 订单编号（可选，0表示查询所有）
     * @param params 查询参数
     * @param timeout_ms 超时时间
     * @return 同步结果(结果数组)
     */
    virtual SyncResult<std::vector<TradeOrderData>> query_orders_sync(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                                                                      const int64_t order_id = 0, const QueryRequestParams *params = nullptr, int timeout_ms = 30000) = 0;

    /**
     * 查询持仓信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    virtual int64_t query_positions(const AccountInfo *account, MarketType market, const char *security_code = nullptr, const QueryRequestParams *params = nullptr) = 0;

    /**
     * 同步查询持仓信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param params 查询参数
     * @param timeout_ms 超时时间
     * @return 同步结果(结果数组)
     */
    virtual SyncResult<std::vector<PositionData>> query_positions_sync(const AccountInfo *account, MarketType market, const char *security_code = nullptr, const QueryRequestParams *params = nullptr, int timeout_ms = 30000) = 0;

    /**
     * 查询资金信息
     * @param account 账户信息
     * @param market 市场代码
     * @param currency 货币种类（可选，NULL表示默认货币）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    virtual int64_t query_funds(const AccountInfo *account, MarketType market, const char *currency = nullptr, const QueryRequestParams *params = nullptr) = 0;

    /**
     * 同步查询资金信息
     * @param account 账户信息
     * @param market 市场代码
     * @param currency 货币种类（可选，NULL表示默认货币）
     * @param params 查询参数
     * @param timeout_ms 超时时间
     * @return 同步结果(结果数组)
     */
    virtual SyncResult<std::vector<AccountData>> query_funds_sync(const AccountInfo *account, MarketType market, const char *currency = nullptr, const QueryRequestParams *params = nullptr, int timeout_ms = 30000) = 0;

    /**
     * 查询成交信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param order_id 订单编号（可选，0表示查询所有）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    virtual int64_t query_trades(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                                 const int64_t order_id = 0, const QueryRequestParams *params = nullptr) = 0;

    /**
     * 同步查询成交信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选，NULL表示查询所有）
     * @param order_id 订单编号（可选，0表示查询所有）
     * @param params 查询参数
     * @param timeout_ms 超时时间
     * @return 同步结果(结果数组)
     */
    virtual SyncResult<std::vector<TradeFillData>> query_trades_sync(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                                                                     const int64_t order_id = 0, const QueryRequestParams *params = nullptr, int timeout_ms = 30000) = 0;

    // ===== 银证转账和资金划拨接口 =====

    /**
     * 银证转账操作
     *
     * 在银行账户和证券账户之间进行资金转移。
     *
     * @param account 账户信息
     * @param direction 转账方向（银行转证券或证券转银行）
     * @param market 市场代码
     * @param transfer_amount 转账金额
     * @param currency 货币种类（可选，默认人民币）
     * @param bank_password 银行密码（可选）
     * @param fund_password 资金密码（可选）
     * @param remark 备注信息（可选）
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     *
     * @pre 必须已连接并登录
     * @pre 账户必须已绑定银行账户
     * @post 异步操作，结果通过回调或轮询获取
     * @note 转账有时间限制，通常在交易时间内进行
     */
    virtual int64_t bank_securities_transfer(const AccountInfo *account,
                                             BankSecuritiesTransferDirection direction,
                                             MarketType market,
                                             double transfer_amount,
                                             const char *currency = nullptr,
                                             const char *bank_password = nullptr,
                                             const char *fund_password = nullptr,
                                             const char *remark = nullptr) = 0;

    /**
     * 同步银证转账操作
     * @param account 账户信息
     * @param direction 转账方向（银行转证券或证券转银行）
     * @param market 市场代码
     * @param transfer_amount 转账金额
     * @param currency 货币种类（可选，默认人民币）
     * @param bank_password 银行密码（可选）
     * @param fund_password 资金密码（可选）
     * @param remark 备注信息（可选）
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    virtual SyncResult<BankSecuritiesTransferData> bank_securities_transfer_sync(const AccountInfo *account,
                                                                                 BankSecuritiesTransferDirection direction,
                                                                                 MarketType market,
                                                                                 double transfer_amount,
                                                                                 const char *currency = nullptr,
                                                                                 const char *bank_password = nullptr,
                                                                                 const char *fund_password = nullptr,
                                                                                 const char *remark = nullptr,
                                                                                 int timeout_ms = 30000) = 0;

    /**
     * 沪深资金划拨操作
     *
     * 在沪市和深市证券账户之间进行资金调拨。
     *
     * @param account 账户信息
     * @param fund_out_account_id 资金转出账户ID
     * @param fund_out_market 资金转出市场
     * @param fund_in_account_id 资金转入账户ID
     * @param fund_in_market 资金转入市场
     * @param transfer_amount 划拨金额
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     *
     * @pre 必须已连接并登录
     * @pre 转出账户必须有足够资金
     * @post 异步操作，结果通过回调或轮询获取
     * @note 通常用于在沪深两市间调配资金
     */
    virtual int64_t fund_transfer(const AccountInfo *account,
                                  const char *fund_out_account_id,
                                  MarketType fund_out_market,
                                  const char *fund_in_account_id,
                                  MarketType fund_in_market,
                                  double transfer_amount) = 0;

    /**
     * 同步沪深资金划拨操作
     * @param account 账户信息
     * @param fund_out_account_id 资金转出账户ID
     * @param fund_out_market 资金转出市场
     * @param fund_in_account_id 资金转入账户ID
     * @param fund_in_market 资金转入市场
     * @param transfer_amount 划拨金额
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    virtual SyncResult<FundTransferData> fund_transfer_sync(const AccountInfo *account,
                                                            const char *fund_out_account_id,
                                                            MarketType fund_out_market,
                                                            const char *fund_in_account_id,
                                                            MarketType fund_in_market,
                                                            double transfer_amount,
                                                            int timeout_ms = 30000) = 0;

    // ===== 回调函数注册接口 =====

    /**
     * 设置订单主推数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     * @note 如果callback为nullptr，返回无效句柄
     */
    virtual TradeOrderCallbackHandle set_order_rtn_callback(TradeOrderCallbackType callback) = 0;

    /**
     * 移除订单主推数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_order_rtn_callback(TradeOrderCallbackHandle handle) = 0;

    /**
     * 设置订单数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     * @note 如果callback为nullptr，返回无效句柄
     */
    virtual TradeOrderQueryCallbackHandle set_order_query_callback(TradeOrderQueryCallbackType callback) = 0;

    /**
     * 移除订单数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_order_query_callback(TradeOrderQueryCallbackHandle handle) = 0;

    /**
     * 设置成交数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual TradeFillCallbackHandle set_fill_callback(TradeFillCallbackType callback) = 0;

    /**
     * 移除成交数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_fill_callback(TradeFillCallbackHandle handle) = 0;

    /**
     * 设置持仓数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual PositionCallbackHandle set_position_callback(PositionCallbackType callback) = 0;

    /**
     * 移除持仓数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_position_callback(PositionCallbackHandle handle) = 0;

    /**
     * 设置账户数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual AccountCallbackHandle set_account_callback(AccountCallbackType callback) = 0;

    /**
     * 移除账户数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_account_callback(AccountCallbackHandle handle) = 0;

    /**
     * 设置错误回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual TradeErrorCallbackHandle set_error_callback(TradeErrorCallbackType callback) = 0;

    /**
     * 移除错误回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_error_callback(TradeErrorCallbackHandle handle) = 0;

    /**
     * 设置银证转账回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual BankSecuritiesTransferCallbackHandle set_bank_securities_transfer_callback(BankSecuritiesTransferCallbackType callback) = 0;

    /**
     * 移除银证转账回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_bank_securities_transfer_callback(BankSecuritiesTransferCallbackHandle handle) = 0;

    /**
     * 设置资金划拨回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual FundTransferCallbackHandle set_fund_transfer_callback(FundTransferCallbackType callback) = 0;

    /**
     * 移除资金划拨回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_fund_transfer_callback(FundTransferCallbackHandle handle) = 0;

    /**
     * 设置订单响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual TradeOrderRspCallbackHandle set_order_rsp_callback(TradeOrderRspCallbackType callback) = 0;

    /**
     * 移除订单响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_order_rsp_callback(TradeOrderRspCallbackHandle handle) = 0;

    /**
     * 设置撤单响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    virtual TradeOrderCancelRspCallbackHandle set_cancel_rsp_callback(TradeOrderCancelRspCallbackType callback) = 0;

    /**
     * 移除撤单响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    virtual int remove_cancel_rsp_callback(TradeOrderCancelRspCallbackHandle handle) = 0;

    // ===== 数据获取接口（轮询模式）=====

    /**
     * 获取最新主推订单数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    virtual int get_order_rtn_data(TradeOrderData *data) = 0;

    /**
     * 获取最新订单数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    virtual int get_order_query_data(TradeOrderData *data) = 0;

    /**
     * 获取最新成交数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    virtual int get_fill_data(TradeFillData *data) = 0;

    /**
     * 获取最新持仓数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    virtual int get_position_data(PositionData *data) = 0;

    /**
     * 获取最新银证转账数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取银证转账结果
     */
    virtual int get_bank_securities_transfer_data(BankSecuritiesTransferData *data) = 0;

    /**
     * 获取最新资金划拨数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取沪深资金划拨结果
     */
    virtual int get_fund_transfer_data(FundTransferData *data) = 0;

    /**
     * 根据完整账户标识获取资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param account_id 证券账户ID（用于区分沪深两市）
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取指定账户的最新资金数据
     */
    virtual int get_account_data_by_id(const char *login_id, const char *fund_account_id, const char *account_id, AccountData *data) = 0;

    /**
     * 根据登陆账号和资金账户获取所有市场的资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param sh_data 上海市场资金数据（可为NULL）
     * @param sz_data 深圳市场资金数据（可为NULL）
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，同时获取沪深两市的资金数据
     */
    virtual int get_all_account_data_by_fund_id(const char *login_id, const char *fund_account_id, AccountData *sh_data, AccountData *sz_data) = 0;

    /**
     * 获取最新委托响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取委托下单的响应信息
     */
    virtual int get_order_rsp_data(TradeOrderRspData *data) = 0;

    /**
     * 获取最新撤单响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取撤单操作的响应信息
     */
    virtual int get_cancel_rsp_data(TradeOrderCancelRsqData *data) = 0;

    /**
     * 保存数据库管理器实例
     * @param db_manager 数据库管理器指针
     * @note 子类在接收到数据库管理器指针时调用，用于保存数据库管理器指针
     */
    virtual void set_db_manager(DatabaseManager *db_manager) = 0;

    /**
     * 获取数据库管理器
     * @return 数据库管理器指针
     * @note 子类在需要使用数据库管理器时调用，用于获取数据库管理器指针
     */
    virtual DatabaseManager *get_db_manager() const = 0;

    /**
     * 获取登录模式
     * @return 登录模式
     * @note 子类在需要使用登录模式时调用，用于获取登录模式
     */
    virtual LoginMode get_login_mode() const = 0;

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器接口
     */
    virtual void set_quote_parser(IQuoteParser *quote_parser) = 0;

    /**
     * 获取行情解析器（由StrategyAdapter注入）
     * @return 行情解析器接口指针
     */
    virtual IQuoteParser *get_quote_parser() const = 0;

protected:
    // ===== 子类实现的数据推送接口（内部使用）=====

    /**
     * 订单状态更新推送（内部接口）
     * @param data 订单数据引用
     * @note 子类在接收到订单状态变化时调用，触发回调和缓存更新
     */
    virtual void on_order_update(const TradeOrderData &data) = 0;

    /**
     * 订单查询结果更新推送（内部接口）
     * @param data 订单数据引用
     * @note 子类在接收到订单查询结果时调用，用于数据同步，不触发策略逻辑
     */
    virtual void on_order_query_result(const TradeOrderData &data) = 0;

    /**
     * 成交查询结果更新推送（内部接口）
     * @param data 成交数据引用
     * @note 子类在接收到成交查询结果时调用
     */
    virtual void on_fill_query_result(const TradeFillData &data) = 0;

    /**
     * 持仓查询结果更新推送（内部接口）
     * @param data 持仓数据引用
     * @note 子类在接收到持仓查询结果时调用
     */
    virtual void on_position_query_result(const PositionData &data) = 0;

    /**
     * 资金查询结果更新推送（内部接口）
     * @param data 资金数据引用
     * @param account_id 账户标识
     * @note 子类在接收到资金查询结果时调用
     */
    virtual void on_account_query_result(const AccountData &data, const char *account_id) = 0;

    /**
     * 错误信息推送（内部接口）
     * @param error_code 错误代码
     * @param error_msg 错误消息
     * @param request_id 关联的请求ID
     * @note 子类在发生错误时调用，触发错误回调
     */
    virtual void on_error_received(int32_t error_code, const char *error_msg, int64_t request_id) = 0;

    /**
     * 状态变化推送（内部接口）
     * @param status 新状态
     * @param message 状态消息
     * @note 子类在状态发生变化时调用，触发状态回调
     */
    virtual void on_status_changed(Status status, const char *message) = 0;

    /**
     * 银证转账结果推送（内部接口）
     * @param data 转账结果数据引用
     * @note 子类在接收到银证转账结果时调用，触发回调和缓存更新
     */
    virtual void on_bank_securities_transfer_update(const BankSecuritiesTransferData &data) = 0;

    /**
     * 资金划拨结果推送（内部接口）
     * @param data 划拨结果数据引用
     * @note 子类在接收到资金划拨结果时调用，触发回调和缓存更新
     */
    virtual void on_fund_transfer_update(const FundTransferData &data) = 0;

    /**
     * 委托响应推送（内部接口）
     * @param data 委托响应数据引用
     * @note 子类在接收到委托响应时调用，触发回调和缓存更新
     */
    virtual void on_order_rsp_update(const TradeOrderRspData &data) = 0;

    /**
     * 撤单响应推送（内部接口）
     * @param data 撤单响应数据引用
     * @note 子类在接收到撤单响应时调用，触发回调和缓存更新
     */
    virtual void on_cancel_rsp_update(const TradeOrderCancelRsqData &data) = 0;
};

/**
 * 处理器工厂函数类型定义
 *
 * 用于动态库中的工厂函数类型定义：
 * - CreateTradeHandlerFuncType: 创建处理器实例的函数类型
 * - DeleteTradeHandlerFuncType: 销毁处理器实例的函数类型
 *
 * @note 这些函数类型用于dlsym查找动态库中的导出函数
 */
typedef ITradeHandler *(*CreateTradeHandlerFuncType)();       ///< 创建处理器函数类型
typedef void (*DeleteTradeHandlerFuncType)(ITradeHandler *&); ///< 销毁处理器函数类型

NS_FT_END

#endif // __cplusplus

#endif // TRADE_HANDLER_H