#ifndef BASE_TRADE_HANDLER_H
#define BASE_TRADE_HANDLER_H

#include "trade/trade_handler.h"
#include "types/trade_types.h"
#include "data/data_manager.h"
#include "database/database_manager.hpp"
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "eventpp/callbacklist.h"
#include "utils/sync_call_framework.h"
#include "database/database_manager.hpp"

NS_FT_BEGIN

/**
 * 交易处理器基类
 *
 * 提供交易处理器的通用功能实现，作为所有具体交易处理器的基类。
 * 封装了交易系统的通用逻辑，让子类专注于具体的交易接口实现。
 *
 * **核心功能**：
 * 1. **生命周期管理**：统一的初始化、启动、停止、清理流程
 * 2. **连接管理**：连接状态监控和自动重连机制
 * 3. **多账户管理**：支持多个交易账户的统一管理
 * 4. **数据管理**：交易数据的缓存、落地和查询
 * 5. **回调机制**：支持事件驱动的数据推送
 * 6. **性能统计**：延迟统计和吞吐量监控
 * 7. **错误处理**：统一的错误信息管理
 *
 * **设计模式**：
 * - 模板方法模式：定义算法骨架，子类实现具体步骤
 * - 观察者模式：通过回调函数实现事件通知
 * - 状态模式：管理交易系统的各种状态
 * - RAII模式：自动资源管理
 *
 * **线程安全性**：
 * - 状态查询方法线程安全（使用原子变量）
 * - 数据获取方法线程安全（使用互斥锁）
 * - 回调函数在内部线程中执行
 * - 不支持并发的生命周期操作
 *
 * **子类实现要求**：
 * 子类需要实现所有以_impl结尾的虚函数，这些函数包含具体的交易接口逻辑。
 */
class BaseTradeHandler : public ITradeHandler
{
public:
    /**
     * 构造函数
     * 初始化基类成员变量和原子状态
     */
    BaseTradeHandler();

    /**
     * 虚析构函数
     * 确保派生类能够正确析构，自动调用release()清理资源
     */
    virtual ~BaseTradeHandler();

    // ===== 基类实现的通用接口 =====

    /**
     * 初始化交易处理器
     *
     * 执行完整的初始化流程：
     * 1. 解析和验证配置参数
     * 2. 初始化性能统计组件
     * 3. 初始化数据管理器
     * 4. 加载账户配置信息
     * 5. 调用子类的init_impl()方法
     *
     * @param config 交易配置结构体，包含连接参数、账户信息等
     * @return 0成功，负数失败
     *
     * @pre config不能为NULL且必须包含有效配置
     * @post 成功时处理器进入已初始化状态
     * @note 线程安全，可重复调用（已初始化时直接返回成功）
     */
    int init(const TradeConfig *config) override final;

    /**
     * 释放交易处理器资源
     *
     * 执行完整的清理流程：
     * 1. 停止所有处理线程
     * 2. 断开所有连接
     * 3. 清理数据缓存
     * 4. 释放内存资源
     * 5. 调用子类的release_impl()方法
     *
     * @post 处理器回到未初始化状态
     * @note 线程安全，幂等操作，可以安全地多次调用
     */
    void release() override final;

    /**
     * 检查是否已连接到交易系统
     * @return true已连接，false未连接
     * @note 线程安全，使用原子变量
     */
    bool is_connected() const override final;

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     * @note 线程安全
     */
    bool is_initialized() const override final;

    /**
     * 检查指定账户是否已登录交易系统
     * @param login_id 登陆账号
     * @return true已登录，false未登录
     * @note 线程安全，查询指定账户的登录状态
     */
    bool is_account_login(const char *login_id) const override final;

    /**
     * 获取所有已登录账户的登陆账号列表
     * @return 已登录账户的登陆账号字符串向量
     * @note 线程安全
     */
    std::vector<std::string> get_logged_in_login_ids() const override final;

    /**
     * 获取已登录账户总数
     * @return 已登录账户数量，负数表示错误
     * @note 线程安全
     */
    int get_logged_in_account_count() const override final;

    /**
     * 检查处理器是否正在运行
     * @return true运行中，false已停止
     * @note 线程安全，使用原子变量
     */
    bool is_running() const override final;

    /**
     * 获取当前运行状态
     * @return 状态枚举值
     * @note 线程安全，提供详细的状态信息
     */
    Status get_status() override final;

    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     * @note 线程安全，返回最近一次操作的错误信息
     */
    std::string get_last_error() const override final;

    /**
     * 启动交易处理器
     * @return 0成功，负数失败
     * @pre 必须先成功调用init()
     * @note 启动内部处理线程，准备接收交易数据
     */
    int start() override final;

    /**
     * 连接到交易系统
     * @return 0成功，负数失败
     * @pre 必须先成功调用start()
     * @note 建立到交易服务器的连接，执行登录流程
     */
    int connect() override final;

    /**
     * 断开交易系统连接
     * @return 0成功，负数失败
     * @note 断开连接但保持运行状态，支持重连
     */
    int disconnect() override final;

    /**
     * 停止交易处理器
     * @return 0成功，负数失败
     * @note 停止处理线程，自动断开连接
     */
    int stop() override final;

    // ===== 性能统计接口 =====

    /**
     * 获取订单延迟统计
     * @param avg_latency_us 平均延迟（微秒）
     * @param max_latency_us 最大延迟（微秒）
     * @param min_latency_us 最小延迟（微秒）
     */
    void get_order_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us);

    /**
     * 获取撤单延迟统计
     * @param avg_latency_us 平均延迟（微秒）
     * @param max_latency_us 最大延迟（微秒）
     * @param min_latency_us 最小延迟（微秒）
     */
    void get_cancel_latency_stats(double *avg_latency_us, double *max_latency_us, double *min_latency_us);

    /**
     * 重置性能统计数据
     */
    void reset_performance_stats();

    // ===== 账户管理接口 =====

    /**
     * 根据登陆账号获取账户信息
     * @param login_id 登陆账号
     * @param account 输出账户信息
     * @return 0成功，负数失败
     * @note 线程安全，从内存缓存中获取
     */
    int get_account_info_by_id(const char *login_id, AccountInfo *account) override final;

    /**
     * 获取所有账户的登陆账号列表
     * @return 登陆账号字符串向量
     * @note 线程安全，返回所有已配置账户的登陆账号
     */
    std::vector<std::string> get_all_login_ids() const override final;

    /**
     * 获取账户总数
     * @return 账户数量，负数表示错误
     * @note 线程安全
     */
    int get_account_count() const override final;

    /**
     * 检查是否存在指定账户
     * @param login_id 登陆账号
     * @return true存在，false不存在
     * @note 线程安全
     */
    bool has_account(const char *login_id) const override final;

    // ===== 回调函数注册接口 =====

    /**
     * 设置订单主推回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeOrderCallbackHandle set_order_rtn_callback(TradeOrderCallbackType callback) override final;

    /**
     * 移除订单主推回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_order_rtn_callback(TradeOrderCallbackHandle handle) override final;

    /**
     * 设置订单数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeOrderQueryCallbackHandle set_order_query_callback(TradeOrderQueryCallbackType callback) override final;

    /**
     * 移除订单数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_order_query_callback(TradeOrderQueryCallbackHandle handle) override final;

    /**
     * 设置成交数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeFillCallbackHandle set_fill_callback(TradeFillCallbackType callback) override final;

    /**
     * 移除成交数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_fill_callback(TradeFillCallbackHandle handle) override final;

    /**
     * 设置持仓数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    PositionCallbackHandle set_position_callback(PositionCallbackType callback) override final;

    /**
     * 移除持仓数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_position_callback(PositionCallbackHandle handle) override final;

    /**
     * 设置账户数据回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    AccountCallbackHandle set_account_callback(AccountCallbackType callback) override final;

    /**
     * 移除账户数据回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_account_callback(AccountCallbackHandle handle) override final;

    /**
     * 设置错误回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeErrorCallbackHandle set_error_callback(TradeErrorCallbackType callback) override final;

    /**
     * 移除错误回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_error_callback(TradeErrorCallbackHandle handle) override final;

    /**
     * 设置银证转账回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    BankSecuritiesTransferCallbackHandle set_bank_securities_transfer_callback(BankSecuritiesTransferCallbackType callback) override final;

    /**
     * 移除银证转账回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_bank_securities_transfer_callback(BankSecuritiesTransferCallbackHandle handle) override final;

    /**
     * 设置资金划拨回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    FundTransferCallbackHandle set_fund_transfer_callback(FundTransferCallbackType callback) override final;

    /**
     * 移除资金划拨回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_fund_transfer_callback(FundTransferCallbackHandle handle) override final;

    /**
     * 设置订单响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeOrderRspCallbackHandle set_order_rsp_callback(TradeOrderRspCallbackType callback) override final;

    /**
     * 移除订单响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_order_rsp_callback(TradeOrderRspCallbackHandle handle) override final;

    /**
     * 设置撤单响应回调函数
     * @param callback 回调函数指针
     * @return 回调句柄，可用if(handle)检查是否设置成功
     */
    TradeOrderCancelRspCallbackHandle set_cancel_rsp_callback(TradeOrderCancelRspCallbackType callback) override final;

    /**
     * 移除撤单响应回调函数
     * @param handle 回调句柄
     * @return 0成功，负数失败
     */
    int remove_cancel_rsp_callback(TradeOrderCancelRspCallbackHandle handle) override final;

    // ===== 数据获取接口（轮询模式）=====

    /**
     * 获取最新主推订单数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_order_rtn_data(TradeOrderData *data) override final;

    /**
     * 获取最新订单数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_order_query_data(TradeOrderData *data) override final;

    /**
     * 获取最新成交数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_fill_data(TradeFillData *data) override final;

    /**
     * 获取最新持仓数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，适用于轮询模式
     */
    int get_position_data(PositionData *data) override final;

    /**
     * 根据完整账户标识获取资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param account_id 证券账户ID（用于区分沪深两市）
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取指定账户的最新资金数据
     */
    int get_account_data_by_id(const char *login_id, const char *fund_account_id, const char *account_id, AccountData *data) override final;

    /**
     * 根据登陆账号和资金账户获取所有市场的资金数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param sh_data 上海市场资金数据（可为NULL）
     * @param sz_data 深圳市场资金数据（可为NULL）
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，同时获取沪深两市的资金数据
     */
    int get_all_account_data_by_fund_id(const char *login_id, const char *fund_account_id, AccountData *sh_data, AccountData *sz_data) override final;

    /**
     * 获取最新委托响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取委托下单的响应信息
     */
    int get_order_rsp_data(TradeOrderRspData *data) override final;

    /**
     * 获取最新撤单响应数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取撤单操作的响应信息
     */
    int get_cancel_rsp_data(TradeOrderCancelRsqData *data) override final;

    /**
     * 获取最新银证转账数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取银证转账结果
     */
    int get_bank_securities_transfer_data(BankSecuritiesTransferData *data) override final;

    /**
     * 获取最新资金划拨数据
     * @param data 输出数据指针
     * @return 1成功获取，0无数据，-1失败
     * @note 线程安全，获取沪深资金划拨结果
     */
    int get_fund_transfer_data(FundTransferData *data) override final;

    // ===== 交易操作接口 =====

    /**
     * 委托下单
     * @param order 下单请求数据
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @pre 必须已连接并登录
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t send_order(const TradeOrderRequest *order) override final;

    /**
     * 同步下单
     * @param order 下单请求数据
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    SyncResult<TradeOrderRspData> send_order_sync(const TradeOrderRequest *order, int timeout_ms = 30000) override final
    {
        return call_sync([this, order]()
                         { return this->send_order(order); },
                         _order_rsp_sync_mgr, timeout_ms);
    }

    /**
     * 撤销委托
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @pre 必须已连接并登录
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t cancel_order(const AccountInfo *account, const int64_t orig_order_id,
                         uint64_t orig_batch_id, MarketType market) override final;

    /**
     * 同步撤单
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @param timeout_ms 超时时间
     * @return 同步结果
     */
    SyncResult<TradeOrderCancelRsqData> cancel_order_sync(const AccountInfo *account,
                                                          const int64_t orig_order_id,
                                                          uint64_t orig_batch_id,
                                                          MarketType market,
                                                          int timeout_ms = 30000) override final
    {
        return call_sync([this, account, orig_order_id, orig_batch_id, market]()
                         { return this->cancel_order(account, orig_order_id, orig_batch_id, market); }, _cancel_rsp_sync_mgr, timeout_ms);
    }
    // ===== 查询接口 =====

    /**
     * 查询订单信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param order_id 订单编号（可选）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t query_orders(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                         const int64_t order_id = 0, const QueryRequestParams *params = nullptr) override final;

    /**
     * 同步查询订单信息
     */
    SyncResult<std::vector<TradeOrderData>> query_orders_sync(const AccountInfo *account,
                                                             MarketType market,
                                                             const char *security_code = nullptr,
                                                             const int64_t order_id = 0,
                                                             const QueryRequestParams *params = nullptr,
                                                             int timeout_ms = 30000) override final
    {
        return call_sync_with_array([this, account, market, security_code, order_id, params]()
                                    { return this->query_orders(account, market, security_code, order_id, params); },
                                    _order_query_sync_mgr, timeout_ms);
    }

    /**
     * 查询持仓信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t query_positions(const AccountInfo *account, MarketType market, const char *security_code = nullptr, const QueryRequestParams *params = nullptr) override final;

    /**
     * 同步查询持仓信息
     */
    SyncResult<std::vector<PositionData>> query_positions_sync(const AccountInfo *account,
                                                               MarketType market,
                                                               const char *security_code = nullptr,
                                                               const QueryRequestParams *params = nullptr,
                                                               int timeout_ms = 30000) override final
    {
        return call_sync_with_array([this, account, market, security_code, params]()
                                    { return this->query_positions(account, market, security_code, params); },
                                    _position_query_sync_mgr, timeout_ms);
    }

    /**
     * 查询资金信息
     * @param account 账户信息
     * @param market 市场代码
     * @param currency 货币种类（可选）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t query_funds(const AccountInfo *account, MarketType market, const char *currency = nullptr, const QueryRequestParams *params = nullptr) override final;

    /**
     * 同步查询资金信息
     */
    SyncResult<std::vector<AccountData>> query_funds_sync(const AccountInfo *account,
                                                         MarketType market,
                                                         const char *currency = nullptr,
                                                         const QueryRequestParams *params = nullptr,
                                                         int timeout_ms = 30000) override final
    {
        return call_sync_with_array([this, account, market, currency, params]()
                                    { return this->query_funds(account, market, currency, params); },
                                    _account_query_sync_mgr, timeout_ms);
    }

    /**
     * 查询成交信息
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码（可选）
     * @param order_id 订单编号（可选）
     * @param params 查询参数
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t query_trades(const AccountInfo *account, MarketType market, const char *security_code = nullptr,
                         const int64_t order_id = 0, const QueryRequestParams *params = nullptr) override final;

    /**
     * 同步查询成交信息
     */
    SyncResult<std::vector<TradeFillData>> query_trades_sync(const AccountInfo *account,
                                                             MarketType market,
                                                             const char *security_code = nullptr,
                                                             const int64_t order_id = 0,
                                                             const QueryRequestParams *params = nullptr,
                                                             int timeout_ms = 30000) override final
    {
        return call_sync_with_array([this, account, market, security_code, order_id, params]()
                                    { return this->query_trades(account, market, security_code, order_id, params); },
                                    _trade_query_sync_mgr, timeout_ms);
    }
    // ===== 银证转账和资金划拨接口 =====

    /**
     * 银证转账操作
     * @param account 账户信息
     * @param direction 转账方向（银行转证券或证券转银行）
     * @param market 市场类型
     * @param transfer_amount 转账金额
     * @param currency 货币种类（可选）
     * @param bank_password 银行密码（可选）
     * @param fund_password 资金密码（可选）
     * @param remark 备注信息（可选）
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t bank_securities_transfer(const AccountInfo *account,
                                     BankSecuritiesTransferDirection direction,
                                     MarketType market,
                                     double transfer_amount,
                                     const char *currency = nullptr,
                                     const char *bank_password = nullptr,
                                     const char *fund_password = nullptr,
                                     const char *remark = nullptr) override final;

    SyncResult<BankSecuritiesTransferData> bank_securities_transfer_sync(const AccountInfo *account,
                                                                         BankSecuritiesTransferDirection direction,
                                                                         MarketType market,
                                                                         double transfer_amount,
                                                                         const char *currency = nullptr,
                                                                         const char *bank_password = nullptr,
                                                                         const char *fund_password = nullptr,
                                                                         const char *remark = nullptr,
                                                                         int timeout_ms = 30000)
    {
        return call_sync([this, account, direction, market, transfer_amount, currency, bank_password, fund_password, remark]()
                         { return this->bank_securities_transfer(account, direction, market, transfer_amount, currency, bank_password, fund_password, remark); }, _bank_transfer_sync_mgr, timeout_ms);
    }

    /**
     * 沪深资金划拨操作
     * @param account 账户信息
     * @param fund_out_account_id 资金转出账户ID
     * @param fund_out_market 资金转出市场
     * @param fund_in_account_id 资金转入账户ID
     * @param fund_in_market 资金转入市场
     * @param transfer_amount 划拨金额
     * @return 正数为生成的request_id（用于跟踪响应），0或负数表示失败
     * @note 异步操作，结果通过回调或轮询获取
     */
    int64_t fund_transfer(const AccountInfo *account,
                          const char *fund_out_account_id,
                          MarketType fund_out_market,
                          const char *fund_in_account_id,
                          MarketType fund_in_market,
                          double transfer_amount) override final;

    SyncResult<FundTransferData> fund_transfer_sync(const AccountInfo *account,
                                                    const char *fund_out_account_id,
                                                    MarketType fund_out_market,
                                                    const char *fund_in_account_id,
                                                    MarketType fund_in_market,
                                                    double transfer_amount,
                                                    int timeout_ms = 30000)
    {
        return call_sync([this, account, fund_out_account_id, fund_out_market, fund_in_account_id, fund_in_market, transfer_amount]()
                         { return this->fund_transfer(account, fund_out_account_id, fund_out_market, fund_in_account_id, fund_in_market, transfer_amount); }, _fund_transfer_sync_mgr, timeout_ms);
    }

    // ===== 数据库服务注入 =====
    
    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     * @note 在初始化后调用，用于交易数据持久化
     */
    void set_db_manager(ft::DatabaseManager* db_manager) override final;
    
    /**
     * 获取数据库管理器
     * @return 数据库管理器指针，未设置时返回nullptr
     */
    ft::DatabaseManager* get_db_manager() const override final;

    /**
     * 获取登陆方式
     * @return 登陆方式
     */
    LoginMode get_login_mode() const override final;

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器接口
     */
    void set_quote_parser(IQuoteParser *quote_parser) override final;

    /**
     * 获取行情解析器
     * @return 行情解析器接口指针
     */
    IQuoteParser *get_quote_parser() const override final;

public:
    // 通用同步调用模板函数
    template <typename AsyncFunc, typename ResponseType>
    SyncResult<ResponseType> call_sync(AsyncFunc async_func,
                                       SyncCallManager<ResponseType> &manager,
                                       int timeout_ms = 30000)
    {
        SyncResult<ResponseType> result;

        try
        {
            // 调用异步函数获取request_id
            int64_t request_id = async_func();
            if (request_id <= 0)
            {
                result.set_error(-1, "Failed to send request");
                return result;
            }

            // 创建future
            auto future = manager.create_future(request_id);

            // 等待响应
            auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

            // 清理资源
            manager.cleanup(request_id);

            if (status == std::future_status::timeout)
            {
                result.set_error(-2, "Request timeout");
                return result;
            }

            // 获取完整响应
            auto complete_response = future.get();

            if (complete_response.error_code == 0)
            {
                result.set_success(complete_response.data);
            }
            else
            {
                result.set_error(complete_response.error_code, complete_response.error_msg.c_str());
            }
        }
        catch (const std::exception &e)
        {
            result.set_error(-3, e.what());
        }

        return result;
    }

    template <typename AsyncFunc, typename ResponseType>
    SyncResult<std::vector<ResponseType>> call_sync_with_array(AsyncFunc async_func,
                                                               SyncCallManager<std::vector<ResponseType>> &sync_mgr,
                                                               int timeout_ms = 30000)
    {
        SyncResult<std::vector<ResponseType>> result;

        try
        {
            int64_t request_id = async_func();
            if (request_id <= 0)
            {
                result.set_error(-1, "Failed to send request");
                return result;
            }

            auto future = sync_mgr.create_future(request_id);
            auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

            sync_mgr.cleanup(request_id);

            if (status == std::future_status::timeout)
            {
                result.set_error(-2, "Request timeout");
                return result;
            }

            auto complete_response = future.get();
            if (complete_response.error_code == 0)
            {
                result.set_success(complete_response.data);
            }
            else
            {
                result.set_error(complete_response.error_code, complete_response.error_msg.c_str());
            }
        }
        catch (const std::exception &e)
        {
            result.set_error(-3, e.what());
        }

        return result;
    }

protected:
    // ===== 子类必须实现的核心接口 =====

    /**
     * 子类特定的初始化实现
     * @param config 交易配置
     * @return 0成功，负数失败
     * @note 在基类init()中调用，用于子类特定的初始化逻辑
     */
    virtual int init_impl(const TradeConfig *config) = 0;

    /**
     * 子类特定的启动实现
     * @return 0成功，负数失败
     * @note 在基类start()中调用，用于启动子类特定的处理线程
     */
    virtual int start_impl() = 0;

    /**
     * 子类特定的连接实现
     * @return 0成功，负数失败
     * @note 在基类connect()中调用，用于建立到交易系统的连接
     */
    virtual int connect_impl() = 0;

    /**
     * 子类特定的断开实现
     * @return 0成功，负数失败
     * @note 在基类disconnect()中调用，用于断开交易系统连接
     */
    virtual int disconnect_impl() = 0;

    /**
     * 子类特定的停止实现
     * @return 0成功，负数失败
     * @note 在基类stop()中调用，用于停止处理线程
     */
    virtual int stop_impl() = 0;

    /**
     * 子类特定的清理实现
     * @note 在基类release()中调用，用于清理子类特定的资源
     */
    virtual void release_impl() = 0;

    /**
     * 子类特定的下单实现
     * @param order 下单请求
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的下单逻辑
     */
    virtual int send_order_impl(const TradeOrderRequest *order, int64_t request_id) = 0;

    /**
     * 子类特定的撤单实现
     * @param account 账户信息
     * @param orig_order_id 原订单编号
     * @param orig_batch_id 原批次编号
     * @param market 市场代码
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的撤单逻辑
     */
    virtual int cancel_order_impl(const AccountInfo *account, const int64_t orig_order_id,
                                  uint64_t orig_batch_id, MarketType market, int64_t request_id) = 0;

    /**
     * 子类特定的订单查询实现
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码
     * @param order_id 订单编号
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的订单查询逻辑
     */
    virtual int query_orders_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                  const int64_t order_id, const QueryRequestParams *params, int64_t request_id) = 0;

    /**
     * 子类特定的持仓查询实现
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的持仓查询逻辑
     */
    virtual int query_positions_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                    const QueryRequestParams *params, int64_t request_id) = 0;

    /**
     * 子类特定的资金查询实现
     * @param account 账户信息
     * @param market 市场代码
     * @param currency 货币种类
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的资金查询逻辑
     */
    virtual int query_funds_impl(const AccountInfo *account, MarketType market, const char *currency,
                                 const QueryRequestParams *params, int64_t request_id) = 0;

    /**
     * 子类特定的成交查询实现
     * @param account 账户信息
     * @param market 市场代码
     * @param security_code 证券代码
     * @param order_id 订单编号
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的成交查询逻辑
     */
    virtual int query_trades_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                  const int64_t order_id, const QueryRequestParams *params, int64_t request_id) = 0;

    /**
     * 子类特定的银证转账实现
     * @param account 账户信息
     * @param direction 转账方向
     * @param transfer_amount 转账金额
     * @param currency 货币种类
     * @param bank_password 银行密码
     * @param fund_password 资金密码
     * @param remark 备注信息
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的银证转账逻辑
     */
    virtual int bank_securities_transfer_impl(const AccountInfo *account,
                                              BankSecuritiesTransferDirection direction,
                                              MarketType market,
                                              double transfer_amount,
                                              const char *currency,
                                              const char *bank_password,
                                              const char *fund_password,
                                              const char *remark,
                                              int64_t request_id) = 0;

    /**
     * 子类特定的资金划拨实现
     * @param account 账户信息
     * @param fund_out_account_id 转出账户ID
     * @param fund_out_market 转出市场
     * @param fund_in_account_id 转入账户ID
     * @param fund_in_market 转入市场
     * @param transfer_amount 划拨金额
     * @param request_id 请求ID
     * @return 0成功，负数失败
     * @note 实现具体的资金划拨逻辑
     */
    virtual int fund_transfer_impl(const AccountInfo *account,
                                   const char *fund_out_account_id,
                                   MarketType fund_out_market,
                                   const char *fund_in_account_id,
                                   MarketType fund_in_market,
                                   double transfer_amount,
                                   int64_t request_id) = 0;

    // ===== 基类提供的辅助方法 =====

    /**
     * 生成唯一的request_id
     * @return 正数为生成的request_id，0表示生成失败
     * @note 线程安全，生成的ID全局唯一
     */
    int64_t generate_request_id();

    /**
     * 设置错误信息
     * @param error 错误描述字符串
     * @note 线程安全，用于子类设置错误状态
     */
    void set_error(const std::string &error);

    /**
     * 配置性能统计参数
     * @param config 性能配置信息
     * @note 在init阶段调用，配置延迟统计、吞吐量统计等
     */
    void configure_performance_stats(const PerformanceConfig &config);

    // ===== 多账户管理辅助方法 =====

    /**
     * 根据登陆账号获取账户信息（内部方法）
     * @param login_id 登陆账号
     * @return 账户信息指针，未找到时返回nullptr
     * @note 内部使用，不加锁
     */
    AccountInfo *get_account_by_login_id_internal(const char *login_id);

    /**
     * 更新账户数据
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param account_id 证券账户ID
     * @param data 新的账户数据
     * @note 线程安全，更新内存缓存中的账户数据
     */
    void update_account_data(const char *login_id, const char *fund_account_id, const char *account_id, const AccountData &data);

    // ===== 数据推送回调接口（子类调用）=====

    /**
     * 订单状态更新回调
     * @param data 订单数据
     * @note 子类在接收到订单状态变化时调用
     */
    void on_order_update(const TradeOrderData &data) override final;

    /**
     * 订单查询结果回调
     * @param data 订单数据
     * @note 子类在接收到订单查询结果时调用，用于数据同步
     */
    void on_order_query_result(const TradeOrderData &data) override final;

    /**
     * 成交查询结果回调
     * @param data 成交数据
     * @note 子类在接收到成交查询结果时调用
     */
    void on_fill_query_result(const TradeFillData &data) override final;

    /**
     * 持仓查询结果回调
     * @param data 持仓数据
     * @note 子类在接收到持仓查询结果时调用
     */
    void on_position_query_result(const PositionData &data) override final;

    /**
     * 资金查询结果回调
     * @param data 资金数据
     * @param account_id 账户标识
     * @note 子类在接收到资金查询结果时调用
     */
    void on_account_query_result(const AccountData &data, const char *account_id) override final;

    /**
     * 错误信息回调
     * @param error_code 错误代码
     * @param error_msg 错误消息
     * @param request_id 关联的请求ID
     * @note 子类在发生错误时调用
     */
    void on_error_received(int32_t error_code, const char *error_msg, int64_t request_id) override final;

    /**
     * 状态变化回调
     * @param status 新状态
     * @param message 状态消息
     * @note 子类在状态发生变化时调用
     */
    void on_status_changed(Status status, const char *message) override final;

    /**
     * 银证转账结果回调
     * @param data 转账结果数据
     * @note 子类在接收到银证转账结果时调用
     */
    void on_bank_securities_transfer_update(const BankSecuritiesTransferData &data) override final;

    /**
     * 资金划拨结果回调
     * @param data 划拨结果数据
     * @note 子类在接收到资金划拨结果时调用
     */
    void on_fund_transfer_update(const FundTransferData &data) override final;

    /**
     * 委托响应回调
     * @param data 委托响应数据
     * @note 子类在接收到委托响应时调用
     */
    void on_order_rsp_update(const TradeOrderRspData &data) override final;

    /**
     * 撤单响应回调
     * @param data 撤单响应数据
     * @note 子类在接收到撤单响应时调用
     */
    void on_cancel_rsp_update(const TradeOrderCancelRsqData &data) override final;

    // ===== 数据落地方法 =====

    /**
     * 写入数据到数据管理器
     * @param data_type 数据类型
     * @param data 数据指针
     * @param size 数据大小
     * @return true成功，false失败
     * @note 用于交易数据的持久化存储
     */
    bool write_to_data_manager(FTDataType data_type, const void *data, size_t size);

    /**
     * 设置账户登录状态
     * @param login_id 登陆账号
     * @param login_status 登录状态
     * @note 线程安全，供子类调用
     */
    void set_account_login_status(const char *login_id, bool login_status);

    /**
     * 清除所有账户登录状态
     * @note 线程安全，供子类调用
     */
    void clear_all_login_status();

    // ===== 受保护的成员变量 =====
    std::atomic<LoginMode> _login_mode; ///< 登陆方式
    mutable std::mutex _mutex; ///< 保护共享数据的互斥锁
    std::string _last_error;   ///< 最后的错误信息


    // request_id生成器
    std::atomic<int64_t> _next_request_id;                        ///< 下一个request_id（线程安全）
    std::chrono::steady_clock::time_point _request_id_start_time; ///< request_id生成器启动时间

    // 状态管理
    std::atomic<bool> _running;          ///< 运行状态标志
    std::atomic<bool> _initialized;      ///< 初始化状态标志
    std::atomic<bool> _connected;        ///< 连接状态标志
    std::atomic<Status> _current_status; ///< 当前状态
    std::atomic<bool> _released;         ///< 资源释放标志

    // 多账户信息管理
    std::unordered_map<std::string, AccountInfo> _accounts;            ///< 登陆账号到账户信息的映射
    std::unordered_map<std::string, AccountData> _latest_account_data; ///< 账户数据缓存
    std::unordered_set<std::string> _logged_in_accounts;               ///< 已登录账户集合（登陆账号）

    // 回调模式
    std::atomic<bool> _callback_mode_enabled; ///< 回调模式启用标志

    // 待处理请求时间记录（使用request_id作为键）
    std::unordered_map<int64_t, std::chrono::high_resolution_clock::time_point> _pending_order_times;  ///< 待处理订单时间记录（以request_id为键）
    std::unordered_map<int64_t, std::chrono::high_resolution_clock::time_point> _pending_cancel_times; ///< 待处理撤单时间记录（以request_id为键）

    // ===== 回调函数私有成员 =====
    TradeOrderCallbackList _order_rtn_callbacks;
    TradeOrderQueryCallbackList _order_query_callbacks;
    TradeFillCallbackList _fill_callbacks;
    PositionCallbackList _position_callbacks;
    AccountCallbackList _account_callbacks;
    TradeErrorCallbackList _error_callbacks;
    BankSecuritiesTransferCallbackList _bank_securities_transfer_callbacks;
    FundTransferCallbackList _fund_transfer_callbacks;
    TradeOrderRspCallbackList _order_rsp_callbacks;
    TradeOrderCancelRspCallbackList _cancel_rsp_callbacks;

    // ===== 最新数据缓存私有成员 =====
    TradeOrderData _latest_order_rtn_data;                            ///< 最新订单数据缓存
    TradeOrderData _latest_order_query_data;                          ///< 最新订单查询数据缓存
    TradeFillData _latest_fill_data;                                  ///< 最新成交数据缓存
    PositionData _latest_position_data;                               ///< 最新持仓数据缓存
    BankSecuritiesTransferData _latest_bank_securities_transfer_data; ///< 最新银证转账数据缓存
    FundTransferData _latest_fund_transfer_data;                      ///< 最新资金划拨数据缓存
    TradeOrderRspData _latest_order_rsp_data;                         ///< 最新委托响应数据缓存
    TradeOrderCancelRsqData _latest_cancel_rsp_data;                  ///< 最新撤单响应数据缓存

    // ===== 数据落地相关私有成员 =====
    std::unique_ptr<DataManager> _data_manager; ///< 数据管理器实例

    // ===== 性能统计相关 =====
    std::atomic<bool> _performance_enabled; ///< 性能统计开关
    bool _latency_stats_enabled;            ///< 延迟统计开关
    uint32_t _report_interval_ms;           ///< 性能报告间隔（毫秒）

    // 订单性能统计
    std::atomic<uint64_t> _total_orders_sent;       ///< 总发送订单数
    std::atomic<uint64_t> _total_orders_responded;  ///< 总响应订单数
    std::atomic<uint64_t> _total_cancels_sent;      ///< 总发送撤单数
    std::atomic<uint64_t> _total_cancels_responded; ///< 总响应撤单数

    // 延迟统计
    uint64_t _total_order_latency_us;  ///< 总订单延迟（微秒）
    uint64_t _total_cancel_latency_us; ///< 总撤单延迟（微秒）
    uint64_t _max_order_latency_us;    ///< 最大订单延迟（微秒）
    uint64_t _max_cancel_latency_us;   ///< 最大撤单延迟（微秒）
    uint64_t _min_order_latency_us;    ///< 最小订单延迟（微秒）
    uint64_t _min_cancel_latency_us;   ///< 最小撤单延迟（微秒）

    std::chrono::steady_clock::time_point _last_performance_report; ///< 上次性能报告时间
    std::thread _performance_report_thread;    
    
    std::unordered_map<int64_t, std::vector<TradeOrderData>> _order_query_accumulators;
    std::unordered_map<int64_t, std::vector<PositionData>> _position_query_accumulators;
    std::unordered_map<int64_t, std::vector<AccountData>> _account_query_accumulators;
    std::unordered_map<int64_t, std::vector<TradeFillData>> _trade_query_accumulators;                     ///< 性能报告线程

    // 保留非查询操作的同步管理器
    SyncCallManager<TradeOrderRspData> _order_rsp_sync_mgr;
    SyncCallManager<TradeOrderCancelRsqData> _cancel_rsp_sync_mgr;
    SyncCallManager<TradeOrderData> _order_rtn_sync_mgr; // 订单主推回报
    SyncCallManager<BankSecuritiesTransferData> _bank_transfer_sync_mgr;
    SyncCallManager<FundTransferData> _fund_transfer_sync_mgr;
    SyncCallManager<std::vector<TradeOrderData>> _order_query_sync_mgr;
    SyncCallManager<std::vector<PositionData>> _position_query_sync_mgr;
    SyncCallManager<std::vector<AccountData>> _account_query_sync_mgr;
    SyncCallManager<std::vector<TradeFillData>> _trade_query_sync_mgr;

    // ===== 数据库相关私有成员 =====
    DatabaseManager* _db_manager = nullptr;  ///< 数据库管理器指针

    // ===== 行情解析器相关私有成员 =====
    IQuoteParser* _quote_parser = nullptr;  ///< 行情解析器指针

    // ===== 私有方法 =====

    /**
     * 初始化数据管理器
     * @param config 数据配置
     * @return true成功，false失败
     */
    bool _initialize_data_manager(const DataConfig &config);

    /**
     * 启动性能报告线程
     */
    void _start_performance_reporting();

    /**
     * 性能统计报告
     */
    void _report_performance_stats();

    /**
     * 更新订单延迟统计
     * @param request_id 请求ID
     * @param order_id 订单ID
     */
    void _update_order_latency_stats(int64_t request_id);

    /**
     * 更新撤单延迟统计
     * @param request_id 请求ID
     * @param order_id 订单ID
     */
    void _update_cancel_latency_stats(int64_t request_id);

    /**
     * 清理过期的待处理时间记录
     */
    void _cleanup_expired_pending_times();

    /**
     * 从数据管理器获取数据
     * @param data_type 数据类型
     * @param data 输出数据指针
     * @param data_size 数据大小
     * @return 1成功获取，0无数据，-1失败
     */
    int _get_data_from_manager(FTDataType data_type, void *data, size_t data_size);

    /**
     * 从内存缓存获取数据
     * @param data_type 数据类型
     * @param data 输出数据指针
     * @param data_size 数据大小
     * @return 1成功获取，0无数据，-1失败
     */
    int _get_data_from_memory(FTDataType data_type, void *data, size_t data_size);

    /**
     * 添加账户信息
     * @param account 账户信息
     * @return 0成功，负数失败
     */
    int _add_account(const AccountInfo &account);

    /**
     * 从配置中加载账户信息
     * @param config 交易配置
     */
    void _load_accounts_from_config(const TradeConfig *config);

    /**
     * 生成账户数据缓存键
     * @param login_id 登陆账号
     * @param fund_account_id 资金账户ID
     * @param account_id 证券账户ID
     * @return 缓存键字符串
     * @note 格式：{login_id}_{fund_account_id}_{account_id}
     */
    std::string _generate_account_key(const char *login_id, const char *fund_account_id, const char *account_id) const;

    // ===== 数据库相关辅助方法 =====
    
    /**
     * 记录策略挂单到数据库
     * @param strategy_id 策略ID
     * @param security_code 股票代码
     * @param order_value 下单值
     * @param order_type 下单类型
     * @param side 下单方向
     * @param strategy_info 策略信息JSON
     * @return 挂单记录ID，失败返回-1
     */
    int64_t record_strategy_pending_order(const std::string& strategy_id,
                                         const std::string& security_code,
                                         double order_value,
                                         const std::string& order_type,
                                         OrderSide side,
                                         const std::string& strategy_info);

};

NS_FT_END

#endif // BASE_TRADE_HANDLER_H