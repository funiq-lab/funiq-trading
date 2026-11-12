#ifndef ATP_TRADE_HANDLER_H
#define ATP_TRADE_HANDLER_H

#include "trade/base_trade_handler.h"
#include "types/trade_types.h"
#include "atp_quant_api.h"
#include "atp_quant_msg.h"
#include "atp_quant_constant.h"
#include "tinyxml2.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <mutex>

NS_FT_BEGIN

/**
 * ATP交易处理器实现类
 * 
 * 基于华锐ATP（Application Trading Platform）量化交易接口实现的交易处理器。
 * 继承自BaseTradeHandler，实现ATP特定的连接、认证和交易功能。
 * 
 * 主要功能：
 * - 连接管理：建立和维护与ATP服务器的连接
 * - 账户管理：支持多账户登录和状态管理
 * - 交易功能：委托下单、撤单、查询等核心交易功能
 * - 数据转换：ATP消息格式与系统内部数据格式的转换
 * - 回调处理：处理ATP服务器推送的各种消息和事件
 * 
 * 线程安全：类内部使用互斥锁保护共享数据，支持多线程环境
 */
class AtpTradeHandler : public BaseTradeHandler, public atp::quant_api::ATPQuantHandler {
public:
    /**
     * 构造函数
     * 初始化ATP交易处理器，设置默认参数
     */
    AtpTradeHandler();
    
    /**
     * 析构函数
     * 清理ATP相关资源，断开连接
     */
    virtual ~AtpTradeHandler();

    // 禁用拷贝构造和赋值操作
    AtpTradeHandler(const AtpTradeHandler&) = delete;
    AtpTradeHandler& operator=(const AtpTradeHandler&) = delete;

protected:
    // ===== BaseTradeHandler接口实现 =====
    
    /**
     * 初始化实现
     * @param config 交易配置参数
     * @return 0表示成功，负数表示失败
     */
    int init_impl(const TradeConfig* config) override;
    
    /**
     * 启动实现
     * @return 0表示成功，负数表示失败
     */
    int start_impl() override;
    
    /**
     * 连接实现
     * @return 0表示成功，负数表示失败
     */
    int connect_impl() override;
    
    /**
     * 断开连接实现
     * @return 0表示成功，负数表示失败
     */
    int disconnect_impl() override;
    
    /**
     * 停止实现
     * @return 0表示成功，负数表示失败
     */
    int stop_impl() override;
    
    /**
     * 释放资源实现
     */
    void release_impl() override;
    
    /**
     * 发送订单实现
     * @param order 订单数据
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int send_order_impl(const TradeOrderRequest* order, int64_t request_id) override;
    
    /**
     * 撤单实现
     * @param account 账户信息
     * @param orig_order_id 原订单ID
     * @param orig_batch_id 原批次ID
     * @param market 市场类型
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int cancel_order_impl(const AccountInfo* account, const int64_t orig_order_id, 
                         uint64_t orig_batch_id, MarketType market, int64_t request_id) override;

    /**
     * 查询订单实现
     * @param account 账户信息
     * @param market 市场类型
     * @param security 证券信息（可选）
     * @param order_id 订单ID（可选）
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int query_orders_impl(const AccountInfo* account, MarketType market, const char* security_code, 
                         const int64_t order_id, const QueryRequestParams *params, int64_t request_id) override;
    
    /**
     * 查询持仓实现
     * @param account 账户信息
     * @param market 市场类型
     * @param security 证券信息（可选）
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int query_positions_impl(const AccountInfo* account, MarketType market, const char* security_code, 
                            const QueryRequestParams *params, int64_t request_id) override;
    
    /**
     * 查询资金实现
     * @param account 账户信息
     * @param market 市场类型
     * @param currency 币种（可选）
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int query_funds_impl(const AccountInfo* account, MarketType market, const char* currency, 
                        const QueryRequestParams *params, int64_t request_id) override;
    
    /**
     * 查询成交实现
     * @param account 账户信息
     * @param market 市场类型
     * @param security 证券信息（可选）
     * @param order_id 订单ID（可选）
     * @param params 查询参数
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int query_trades_impl(const AccountInfo* account, MarketType market, const char* security_code, 
                         const int64_t order_id, const QueryRequestParams *params, int64_t request_id) override;
    
    /**
     * 银证转账实现
     * @param account 账户信息
     * @param direction 转账方向
     * @param transfer_amount 转账金额
     * @param currency 币种
     * @param bank_password 银行密码
     * @param fund_password 资金密码
     * @param remark 备注
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int bank_securities_transfer_impl(const AccountInfo* account, 
                                      BankSecuritiesTransferDirection direction,
                                      MarketType market,
                                      double transfer_amount,
                                      const char* currency,
                                      const char* bank_password,
                                      const char* fund_password,
                                      const char* remark,
                                      int64_t request_id) override;

    /**
     * 资金划拨实现
     * @param account 账户信息
     * @param fund_out_account_id 转出资金账户ID
     * @param fund_out_market 转出市场
     * @param fund_in_account_id 转入资金账户ID
     * @param fund_in_market 转入市场
     * @param transfer_amount 转账金额
     * @param request_id 请求ID
     * @return 0表示成功，负数表示失败
     */
    int fund_transfer_impl(const AccountInfo* account,
                          const char* fund_out_account_id,
                          MarketType fund_out_market,
                          const char* fund_in_account_id,
                          MarketType fund_in_market,
                          double transfer_amount,
                          int64_t request_id) override;

public:
    // ===== ATP基础回调接口 =====
    
    /**
     * 登录成功回调
     * @param msg 客户信息消息
     */
    void OnLogin(const atp::quant_api::ATPCustomerInfo& msg) override;
    
    /**
     * 登出回调
     * @param desc 描述信息
     */
    void OnLogout(const char* desc) override;
    
    /**
     * 自动重连回调
     * @param desc 描述信息
     */
    void OnRecovering(const char* desc) override;
    
    /**
     * 订阅平台状态信息结果回调
     * @param error_info 错误信息
     * @param request_id 请求ID
     */
    void OnRspSubsPlatFormStateResult(const atp::quant_api::ATPRspErrorInfo& error_info, const int64_t request_id) override;
    
    /**
     * 平台状态推送回调
     * @param msg 平台状态推送消息
     */
    void OnRtnPlatFormStatePushResult(const atp::quant_api::ATPRtnPlatFormStatePushResultMsg& msg) override;
   
    /**
     * 现货委托响应回调
     * @param msg 委托响应消息
     * @param error_info 错误信息
     * @param request_id 请求ID
     */
    void OnRspCashAuctionOrder(
        const atp::quant_api::ATPRspCashAuctionOrderMsg& msg, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const int64_t request_id
    ) override;
    
    /**
     * 现货委托回报回调
     * @param msg 委托回报消息
     */
    void OnRtnCashAuctionOrder(
        const atp::quant_api::ATPRtnCashAuctionOrderMsg& msg
    ) override;
    
    /**
     * 现货撤单响应回调
     * @param msg 撤单响应消息
     * @param error_info 错误信息
     * @param request_id 请求ID
     */
    void OnRspCashCancelOrder(
        const atp::quant_api::ATPRspCashCancelOrderMsg& msg, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const int64_t request_id
    ) override;
    
    /**
     * 现货订单查询结果回调
     * @param msg 订单查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashOrderQueryResult(
        const atp::quant_api::ATPRspCashOrderQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货股份查询结果回调
     * @param msg 股份查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashShareQueryResult(
        const atp::quant_api::ATPRspCashShareQueryResultMsg& msg,
        const int64_t request_id,
        const atp::quant_api::ATPRspErrorInfo& error_info,
        const bool isLast
    ) override;
    
    /**
     * 现货资金查询结果回调
     * @param msg 资金查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashFundQueryResult(
        const atp::quant_api::ATPRspCashFundQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货成交查询结果回调
     * @param msg 成交查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashTradeOrderQueryResult(
        const atp::quant_api::ATPRspCashTradeOrderQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货按证券账户划转资金响应回调
     * @param msg 划转资金响应消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     */
    void OnRspCashExternalInsTETransFundResp(
        const atp::quant_api::ATPRspCashExternalInsTETransFundRespMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info
    ) override;
    
    /**
     * 现货资金划拨(转入、转出)应答回调
     * @param msg 资金划拨结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     */
    void OnRspCashExtFundTransferResult(
        const atp::quant_api::ATPRspCashExtFundTransferResultOtherMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info
    ) override;
    
    /**
     * 现货证券信息查询结果消息回调
     * @param msg 证券信息查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashExtQueryResultSecurityInfo(
        const atp::quant_api::ATPRspCashExtQueryResultSecurityInfoMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货资产查询结果回调
     * @param msg 资产查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashAssetQueryResult(
        const atp::quant_api::ATPRspCashAssetQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货成交汇总查询结果回调
     * @param msg 成交汇总查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashTradeCollectQueryResult(
        const atp::quant_api::ATPRspCashCollectQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 现货最大可委托数查询结果回调
     * @param msg 最大可委托数查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspCashMaxOrderQueryResult(
        const atp::quant_api::ATPRspCashMaxOrderQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * ETF信息查询结果回调
     * @param msg ETF信息查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspExtQueryResultETFInfo(
        const atp::quant_api::ATPRspExtQueryResultETFInfoMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * ETF成分股信息查询结果回调
     * @param msg ETF成分股信息查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspExtQueryResultETFComponentInfo(
        const atp::quant_api::ATPRspExtQueryResultETFComponentInfoMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 客户报盘通道ID查询结果回调
     * @param msg 报盘通道ID查询结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     * @param isLast 是否最后一条
     */
    void OnRspOpuQueryResult(
        const atp::quant_api::ATPRspOpuQueryResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info, 
        const bool isLast
    ) override;
    
    /**
     * 一步式银证转账响应回调
     * @param msg 银证转账结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     */
    void OnRspCashBankSecuritiesFundTransferResult(
        const atp::quant_api::ATPRspCashBankSecuritiesFundTransferResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info
    ) override;
    
    /**
     * 外部持仓划拨响应回调
     * @param msg 持仓划拨结果消息
     * @param request_id 请求ID
     * @param error_info 错误信息
     */
    void OnRspExtShareTransferResult(
        const atp::quant_api::ATPRspExtShareTransferResultMsg& msg, 
        const int64_t request_id, 
        const atp::quant_api::ATPRspErrorInfo& error_info
    ) override;
    
private:
    // ===== ATP特定的私有成员 =====
    std::unique_ptr<atp::quant_api::ATPQuantAPI> _atp_api;        ///< ATP量化API实例
    std::unique_ptr<atp::quant_api::ATPProperties> _atp_properties; ///< ATP属性配置
    
    // ATP配置信息 - 从XML解析的所有配置项
    int _log_level;                    ///< 日志级别
    std::string _common_log_path;      ///< 通用日志路径
    std::string _indicator_log_path;   ///< 指标日志路径
    std::string _io_log_path;          ///< IO日志路径
    int _encrypt_schema;               ///< 加密模式
    bool _is_enable_latency;           ///< 是否启用延迟统计
    int _callback_resource_mode;       ///< 回调资源模式
    int _receive_thread_cpu;           ///< 接收线程CPU绑定
    int _send_thread_cpu;              ///< 发送线程CPU绑定
    bool _is_tcp_direct;               ///< 是否TCP直连
    std::string _bind_ip_address;      ///< 绑定IP地址
    int _retransmit_mode;              ///< 重传模式
    int _multi_channel_flag;           ///< 多通道标志
    int _connection_protocol;          ///< 连接协议
    std::string _locations;            ///< 服务器地址列表
    std::string _order_way;            ///< 委托方式
    std::string _client_feature_code;  ///< 客户端特征码
    std::string _agw_user;             ///< AGW用户名
    std::string _agw_password;         ///< AGW密码
    
    // 委托序号管理
    std::atomic<int64_t> _next_cl_ord_no;                    ///< 下一个委托序号

    // ===== ATP特定的私有方法 =====
    
    /**
     * 初始化ATP API
     * @param config 交易配置
     * @return 0表示成功，负数表示失败
     */
    int _initialize_atp_api(const TradeConfig* config);
    
    /**
     * 创建ATP属性配置
     * @param config 交易配置
     * @return 0表示成功，负数表示失败
     */
    int _create_atp_properties(const TradeConfig* config);
    
    /**
     * 解析ATP XML配置文件
     * @param xml_file_path XML文件路径
     * @return 0表示成功，负数表示失败
     */
    int _parse_atp_xml_config(const char* xml_file_path);
    
    // ATP登录相关
    
    /**
     * 登录所有账户
     * @return 0表示成功，负数表示失败
     */
    int _login_all_accounts();
    
    /**
     * 登录单个账户
     * @param account 账户信息
     * @return 0表示成功，负数表示失败
     */
    int _login_single_account(const AccountInfo& account);
    
    /**
     * 登出所有账户
     * @return 0表示成功，负数表示失败
     */
    int _logout_all_accounts();
    
    /**
     * 从ATP客户信息更新账户列表
     * @param msg ATP客户信息消息
     */
    void _update_accounts_from_atp_info(const atp::quant_api::ATPCustomerInfo& msg);
    
    // ATP消息构建方法
    
    /**
     * 构建ATP委托请求消息
     * @param order 订单数据
     * @return ATP委托请求消息指针
     */
    std::unique_ptr<atp::quant_api::ATPReqCashAuctionOrderMsg> _build_atp_order_request(const TradeOrderRequest* order);
    
    /**
     * 构建ATP撤单请求消息
     * @param account 账户信息
     * @param orig_order_id 原订单ID
     * @param orig_batch_id 原批次ID
     * @param market 市场类型
     * @return ATP撤单请求消息指针
     */
    std::unique_ptr<atp::quant_api::ATPReqCashCancelOrderMsg> _build_atp_cancel_request(
        const AccountInfo* account, const int64_t orig_order_id, uint64_t orig_batch_id, MarketType market);
    
    /**
     * 构建ATP银证转账请求消息
     * @param account 账户信息
     * @param direction 转账方向
     * @param market 市场类型
     * @param transfer_amount 转账金额
     * @param currency 币种
     * @param bank_password 银行密码
     * @param fund_password 资金密码
     * @param remark 备注
     * @return ATP银证转账请求消息指针
     */
    std::unique_ptr<atp::quant_api::ATPReqCashBankSecuritiesFundTransferOtherMsg> 
        _build_atp_bank_securities_transfer_request(const AccountInfo* account, 
                                                   BankSecuritiesTransferDirection direction,
                                                   MarketType market,
                                                   double transfer_amount,
                                                   const char* currency,
                                                   const char* bank_password,
                                                   const char* fund_password,
                                                   const char* remark);

    /**
     * 构建ATP资金划拨请求消息
     * @param account 账户信息
     * @param fund_out_account_id 转出资金账户ID
     * @param fund_out_market 转出市场
     * @param fund_in_account_id 转入资金账户ID
     * @param fund_in_market 转入市场
     * @param transfer_amount 转账金额
     * @return ATP资金划拨请求消息指针
     */
    std::unique_ptr<atp::quant_api::ATPReqCashExternalInsTETransFundReqMsg> 
        _build_atp_fund_transfer_request(const AccountInfo* account,
                                       const char* fund_out_account_id,
                                       MarketType fund_out_market,
                                       const char* fund_in_account_id,
                                       MarketType fund_in_market,
                                       double transfer_amount);
    
    
    /**
     * 处理ATP错误信息
     * @param error_info ATP错误信息
     * @param request_id 请求ID
     */
    void _handle_atp_error(const atp::quant_api::ATPRspErrorInfo& error_info, int64_t request_id);
    
    // ===== 数据转换方法 =====
    
    /**
     * 将ATP委托响应转换为系统委托响应数据
     * @param msg ATP委托响应消息
     * @param error_info ATP错误信息
     * @return 系统委托响应数据
     */
    TradeOrderRspData _convert_atp_order_rsp_to_trade_order_rsp(const atp::quant_api::ATPRspCashAuctionOrderMsg& msg,
                                                               const atp::quant_api::ATPRspErrorInfo& error_info);
    
    /**
     * 将ATP撤单响应转换为系统撤单响应数据
     * @param msg ATP撤单响应消息
     * @param error_info ATP错误信息
     * @return 系统撤单响应数据
     */
    TradeOrderCancelRsqData _convert_atp_cancel_rsp_to_trade_cancel_rsp(const atp::quant_api::ATPRspCashCancelOrderMsg& msg,
                                                                       const atp::quant_api::ATPRspErrorInfo& error_info);
    
    /**
     * 将ATP委托回报转换为交易订单数据
     * @param msg ATP委托回报消息
     * @return 交易订单数据
     */
    TradeOrderData _convert_atp_order_to_trade_order_rtn(const atp::quant_api::ATPRtnCashAuctionOrderMsg& msg);
    
    /**
     * 将ATP订单查询结果转换为系统订单数据
     * @param msg ATP订单查询结果消息
     * @param error_info ATP错误信息
     * @return 系统订单数据
     */
    TradeOrderData _convert_atp_order_query_to_trade_order(const atp::quant_api::ATPRspCashOrderQueryResultMsg& msg, 
                                                          const atp::quant_api::ATPRspErrorInfo& error_info,
                                                          const bool is_last);
    
    /**
     * 将ATP成交查询结果转换为系统成交数据
     * @param msg ATP成交查询结果消息
     * @param error_info ATP错误信息
     * @return 系统成交数据
     */
    TradeFillData _convert_atp_trade_query_to_trade_fill(const atp::quant_api::ATPRspCashTradeOrderQueryResultMsg& msg,
                                                        const atp::quant_api::ATPRspErrorInfo& error_info,
                                                        const bool is_last);
    
    /**
     * 将ATP持仓查询结果转换为系统持仓数据
     * @param msg ATP持仓查询结果消息
     * @param error_info ATP错误信息
     * @return 系统持仓数据
     */
    PositionData _convert_atp_position_to_position_data(const atp::quant_api::ATPRspCashShareQueryResultMsg& msg,
                                                       const atp::quant_api::ATPRspErrorInfo& error_info,
                                                       const bool is_last);
    
    /**
     * 将ATP银证转账结果转换为银证转账数据
     * @param msg ATP银证转账结果消息
     * @param error_info ATP错误信息
     * @return 银证转账数据
     */
    BankSecuritiesTransferData _convert_atp_bank_securities_transfer_to_data(
        const atp::quant_api::ATPRspCashBankSecuritiesFundTransferResultMsg& msg,
        const atp::quant_api::ATPRspErrorInfo& error_info);
    
    /**
     * 将ATP资金划拨结果转换为资金划拨数据
     * @param msg ATP资金划拨结果消息
     * @return 资金划拨数据
     */
    FundTransferData _convert_atp_fund_transfer_to_data(
        const atp::quant_api::ATPRspCashExternalInsTETransFundRespMsg& msg,
        const atp::quant_api::ATPRspErrorInfo& error_info
    );    
    /**
     * 将ATP资金查询结果转换为系统账户数据
     * @param msg ATP资金查询结果消息
     * @param error_info ATP错误信息
     * @return 系统账户数据
     */
    AccountData _convert_atp_fund_to_account_data(const atp::quant_api::ATPRspCashFundQueryResultMsg& msg,
                                                 const atp::quant_api::ATPRspErrorInfo& error_info,
                                                 const bool is_last);
    
    // 状态映射

    /**
     * 将ATP订单标识映射为系统订单标识
     * @param ord_sign ATP订单标识
     * @return 系统订单标识
     */
    OrderSign _map_atp_order_sign_to_order_sign(uint8_t ord_sign);
    
    /**
     * 将ATP订单状态映射为系统订单状态
     * @param ord_status ATP订单状态
     * @return 系统订单状态
     */
    OrderStatus _map_atp_status_to_order_status(int ord_status);
    
    /**
     * 将ATP买卖方向映射为系统订单方向
     * @param side ATP买卖方向
     * @return 系统订单方向
     */
    OrderSide _map_atp_side_to_order_side(char side);
    
    /**
     * 将ATP订单类型映射为系统订单类型
     * @param order_type ATP订单类型
     * @return 系统订单类型
     */
    OrderType _map_atp_order_type_to_order_type(char order_type);
    
    /**
     * 将ATP市场ID映射为系统市场类型
     * @param market_id ATP市场ID
     * @return 系统市场类型
     */
    MarketType _map_atp_market_to_market_type(int market_id);

    /**
     * 根据市场类型获取对应的账户ID
     * @param account 账户信息
     * @param market 市场类型
     * @return 对应市场的账户ID，未找到返回nullptr
     */
    const char* _get_account_id_by_market(const AccountInfo* account, MarketType market) const;
    
    /**
     * 验证账户在指定市场是否有效
     * @param account 账户信息
     * @param market 市场类型
     * @return true表示有效，false表示无效
     */
    bool _is_market_account_valid(const AccountInfo* account, MarketType market) const;
    
    /**
     * 根据ATP消息填充账户信息
     * @param cust_id ATP返回的客户号
     * @param fund_account_id ATP返回的资金账户ID（可选，用于回退）
     * @param account 输出的账户信息
     * @return true表示找到完整账户信息，false表示使用了回退方案
     */
    bool _fill_account_info_from_atp(const char* cust_id, const char* fund_account_id, AccountInfo* account);

    // ===== 错误处理辅助方法 =====
    
    /**
     * 填充错误信息到数据结构
     * @param error_info ATP错误信息
     * @param error_code 目标错误代码字段引用
     * @param error_msg 目标错误消息字段指针
     * @param error_msg_size 错误消息字段大小
     */
    void _fill_error_info(const atp::quant_api::ATPRspErrorInfo& error_info,
                         int& error_code,
                         char* error_msg,
                         size_t error_msg_size);

    /**
     * 将FT订单查询条件映射为ATP订单查询条件
     * @param condition FT订单查询条件
    * @return ATP订单查询条件
    */
   int8_t _map_ft_order_query_condition_to_atp_order_query_condition(const OrderQueryCondition condition);

    /**
     * 将FT成交查询条件映射为ATP成交查询条件
     * @param condition FT查询条件
     * @return ATP成交查询条件
     */
    int8_t _map_ft_trade_query_condition_to_atp_trade_query_condition(const TradeQueryCondition condition);

    /**
     * 将FT返回顺序映射为ATP返回顺序
     * @param condition FT返回顺序
     * @return ATP返回顺序
     */
    int8_t _map_ft_return_seq_to_atp_return_seq(const ReturnSeqType condition);
};

NS_FT_END

#endif // ATP_TRADE_HANDLER_H