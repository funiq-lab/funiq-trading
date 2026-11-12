#include "atp_trade_handler.h"
#include "logger/logger.hpp"
#include "utils/string.hpp"
#include "utils/time.h"
#include <chrono>
#include <sstream>
#include <cstring>
#include <thread>

using namespace atp::quant_api;
using namespace tinyxml2;

NS_FT_BEGIN

AtpTradeHandler::AtpTradeHandler()
    : BaseTradeHandler(), _next_cl_ord_no(1)
{
    TRADE_LOG_INFO("AtpTradeHandler constructed");
}

AtpTradeHandler::~AtpTradeHandler()
{
    if (_running.load())
    {
        stop();
    }
    TRADE_LOG_INFO("AtpTradeHandler destructed");
}

// ===== 实现基类的核心虚函数 =====
int AtpTradeHandler::init_impl(const TradeConfig *config)
{
    // 初始化ATP API
    if (_initialize_atp_api(config) != 0)
    {
        return -1;
    }

    TRADE_LOG_INFO("AtpTradeHandler initialized successfully");
    return 0;
}

int AtpTradeHandler::start_impl()
{
    if (!_atp_api)
    {
        set_error("ATP API not initialized");
        return -1;
    }

    TRADE_LOG_INFO("AtpTradeHandler started");
    return 0;
}

int AtpTradeHandler::connect_impl()
{
    if (!_atp_api)
    {
        set_error("ATP API not initialized");
        return -1;
    }

    // ATP连接在构造时已建立，这里执行登录
    if (_login_all_accounts() != 0)
    {
        return -1;
    }

    TRADE_LOG_INFO("ATP connection established");
    return 0;
}

int AtpTradeHandler::disconnect_impl()
{
    if (get_logged_in_account_count() > 0)
    {
        _logout_all_accounts();
    }

    TRADE_LOG_INFO("ATP connection disconnected");
    return 0;
}

int AtpTradeHandler::stop_impl()
{
    if (disconnect_impl() != 0)
    {
        set_error("Failed to stop ATP");
        return -1;
    }
    TRADE_LOG_INFO("AtpTradeHandler stopped");
    return 0;
}

void AtpTradeHandler::release_impl()
{
    if (_atp_api)
    {
        _atp_api->Release();
        _atp_api.reset();
    }

    // 清理ATP相关资源
    if (ATPQuantAPI::Stop() != ATPErrorCode::kSuccess)
    {
        TRADE_LOG_WARN("Failed to stop ATP API");
    }

    _atp_properties.reset();

    TRADE_LOG_INFO("AtpTradeHandler released");
}

int AtpTradeHandler::send_order_impl(const TradeOrderRequest *order, int64_t request_id)
{
    if (!_atp_api || !is_account_login(order->account.login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 构建ATP订单请求
    auto atp_order = _build_atp_order_request(order);
    if (!atp_order)
    {
        set_error("Failed to build ATP order request");
        return -1;
    }

    // 发送订单
    ATPErrorCodeType ret = _atp_api->ReqCashAuctionOrder(atp_order.get(), request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP send order failed, error code: " + std::to_string(ret));
        return -1;
    }

    return 0;
}

int AtpTradeHandler::cancel_order_impl(const AccountInfo *account, const int64_t orig_order_id,
                                       uint64_t orig_batch_id, MarketType market, int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 构建ATP撤单请求
    auto atp_cancel = _build_atp_cancel_request(account, orig_order_id, orig_batch_id, market);
    if (!atp_cancel)
    {
        set_error("Failed to build ATP cancel request");
        return -1;
    }

    // 发送撤单
    ATPErrorCodeType ret = _atp_api->ReqCashCancelOrder(atp_cancel.get(), request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP cancel order failed, error code: " + std::to_string(ret));
        return -1;
    }

    return 0;
}

int AtpTradeHandler::query_orders_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                       const int64_t order_id, const QueryRequestParams *params, int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }
    // 验证市场账户有效性
    if (!_is_market_account_valid(account, market))
    {
        set_error("Invalid account for market " + std::to_string(static_cast<int>(market)));
        return -1;
    }

    // 构建ATP订单查询请求
    auto query_req = ATPReqCashOrderQueryMsg::NewMessage();

    // 设置查询参数
    query_req->SetCustId(account->cust_id);
    query_req->SetFundAccountId(account->fund_account_id);
    query_req->SetBranchId(account->branch_id);
    query_req->SetPassword(account->password);

    // 设置具体的证券账户ID和市场ID
    const char *account_id = _get_account_id_by_market(account, market);
    query_req->SetAccountId(account_id);

    if (market == MARKET_SSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShenZhen);
    }
    else
    {
        set_error("Unsupported market type: " + std::to_string(static_cast<int>(market)));
        ATPReqCashOrderQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    if (order_id)
    {
        query_req->SetClOrdNo(order_id);
    }

    if (security_code)
    {
        query_req->SetSecurityId(security_code);
    }

    if (params)
    {
        query_req->SetOrderQueryCondition(static_cast<uint8_t>(_map_ft_order_query_condition_to_atp_order_query_condition(params->condition.order_condition)));
        query_req->SetReturnSeq(static_cast<uint8_t>(_map_ft_return_seq_to_atp_return_seq(params->return_seq)));
        query_req->SetReturnNum(params->return_num);
    }

    // 发送查询请求
    ATPErrorCodeType ret = _atp_api->ReqCashOrderQuery(query_req, request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP query orders failed for market " + std::to_string(static_cast<int>(market)) +
                  ", error code: " + std::to_string(ret));
        ATPReqCashOrderQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    ATPReqCashOrderQueryMsg::DeleteMessage(query_req);
    return 0;
}

int AtpTradeHandler::query_positions_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                          const QueryRequestParams *params, int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 验证市场账户有效性
    if (!_is_market_account_valid(account, market))
    {
        set_error("Invalid account for market " + std::to_string(static_cast<int>(market)));
        return -1;
    }

    // 构建ATP股份查询请求
    auto query_req = ATPReqCashShareQueryMsg::NewMessage();

    query_req->SetCustId(account->cust_id);
    query_req->SetFundAccountId(account->fund_account_id);
    query_req->SetBranchId(account->branch_id);
    query_req->SetPassword(account->password);

    // 设置具体的证券账户ID和市场ID
    const char *account_id = _get_account_id_by_market(account, market);
    query_req->SetAccountId(account_id);

    if (market == MARKET_SSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShenZhen);
    }

    if (security_code)
    {
        query_req->SetSecurityId(security_code);
    }

    if (params)
    {
        query_req->SetReturnNum(params->return_num);
    }

    // 发送查询请求
    ATPErrorCodeType ret = _atp_api->ReqCashShareQuery(query_req, request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP query positions failed for market " + std::to_string(static_cast<int>(market)) +
                  ", error code: " + std::to_string(ret));
        ATPReqCashShareQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    ATPReqCashShareQueryMsg::DeleteMessage(query_req);
    return 0;
}

int AtpTradeHandler::query_funds_impl(const AccountInfo *account, MarketType market, const char *currency,
                                      const QueryRequestParams *params, int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 验证市场账户有效性
    if (!_is_market_account_valid(account, market))
    {
        set_error("Invalid account for market " + std::to_string(static_cast<int>(market)));
        return -1;
    }

    // 构建ATP资金查询请求
    auto query_req = ATPReqCashFundQueryMsg::NewMessage();

    query_req->SetCustId(account->cust_id);
    query_req->SetFundAccountId(account->fund_account_id);
    query_req->SetBranchId(account->branch_id);
    query_req->SetPassword(account->password);

    // 设置具体的证券账户ID和市场ID
    const char *account_id = _get_account_id_by_market(account, market);
    query_req->SetAccountId(account_id);

    if (market == MARKET_SSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShenZhen);
    }
    else
    {
        set_error("Unsupported market type: " + std::to_string(static_cast<int>(market)));
        ATPReqCashFundQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    if (currency && strlen(currency) > 0)
    {
        query_req->SetCurrency(currency);
    }

    // 发送查询请求
    ATPErrorCodeType ret = _atp_api->ReqCashFundQuery(query_req, request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP query funds failed for market " + std::to_string(static_cast<int>(market)) +
                  ", error code: " + std::to_string(ret));
        ATPReqCashFundQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    ATPReqCashFundQueryMsg::DeleteMessage(query_req);
    return 0;
}

int AtpTradeHandler::query_trades_impl(const AccountInfo *account, MarketType market, const char *security_code,
                                       const int64_t order_id, const QueryRequestParams *params, int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 验证市场账户有效性
    if (!_is_market_account_valid(account, market))
    {
        set_error("Invalid account for market " + std::to_string(static_cast<int>(market)));
        return -1;
    }

    // 构建ATP成交查询请求
    auto query_req = ATPReqCashTradeOrderQueryMsg::NewMessage();

    query_req->SetCustId(account->cust_id);
    query_req->SetFundAccountId(account->fund_account_id);
    query_req->SetPassword(account->password);

    // 设置具体的证券账户ID和市场ID
    const char *account_id = _get_account_id_by_market(account, market);
    query_req->SetAccountId(account_id);

    if (market == MARKET_SSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        query_req->SetMarketId(ATPMarketIDConst::kShenZhen);
    }

    if (security_code)
    {
        query_req->SetSecurityId(security_code);
    }

    if (order_id)
    {
        query_req->SetClOrdNo(order_id);
    }

    if (params)
    {
        query_req->SetTradeOrderQueryCondition(static_cast<uint8_t>(_map_ft_trade_query_condition_to_atp_trade_query_condition(params->condition.trade_condition)));
        query_req->SetReturnSeq(static_cast<uint8_t>(_map_ft_return_seq_to_atp_return_seq(params->return_seq)));
        query_req->SetReturnNum(params->return_num);
    }

    // 发送查询请求
    ATPErrorCodeType ret = _atp_api->ReqCashTradeOrderQuery(query_req, request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP query trades failed for market " + std::to_string(static_cast<int>(market)) +
                  ", error code: " + std::to_string(ret));
        ATPReqCashTradeOrderQueryMsg::DeleteMessage(query_req);
        return -1;
    }

    ATPReqCashTradeOrderQueryMsg::DeleteMessage(query_req);
    return 0;
}

int AtpTradeHandler::bank_securities_transfer_impl(const AccountInfo *account,
                                                   BankSecuritiesTransferDirection direction,
                                                   MarketType market,
                                                   double transfer_amount, const char *currency,
                                                   const char *bank_password,
                                                   const char *fund_password,
                                                   const char *remark,
                                                   int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 构建ATP银证转账请求
    auto atp_request = _build_atp_bank_securities_transfer_request(account, direction,
                                                                   market,
                                                                   transfer_amount, currency,
                                                                   bank_password, fund_password, remark);
    if (!atp_request)
    {
        set_error("Failed to build ATP bank securities transfer request");
        return -1;
    }

    // 发送银证转账请求
    ATPErrorCodeType ret = _atp_api->ReqCashBankSecuritiesFundTransferOther(atp_request.get(), request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP bank securities transfer failed, error code: " + std::to_string(ret));
        return -1;
    }

    return 0;
}

int AtpTradeHandler::fund_transfer_impl(const AccountInfo *account,
                                        const char *fund_out_account_id,
                                        MarketType fund_out_market,
                                        const char *fund_in_account_id,
                                        MarketType fund_in_market,
                                        double transfer_amount,
                                        int64_t request_id)
{
    if (!_atp_api || !is_account_login(account->login_id))
    {
        set_error("ATP not connected or not logged in");
        return -1;
    }

    // 构建ATP资金划拨请求
    auto atp_request = _build_atp_fund_transfer_request(account, fund_out_account_id, fund_out_market,
                                                        fund_in_account_id, fund_in_market, transfer_amount);
    if (!atp_request)
    {
        set_error("Failed to build ATP fund transfer request");
        return -1;
    }

    // 发送资金划拨请求
    ATPErrorCodeType ret = _atp_api->ReqCashExternalInsTETransFundReq(atp_request.get(), request_id);
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP fund transfer failed, error code: " + std::to_string(ret));
        return -1;
    }

    return 0;
}

// ===== 实现ATP回调接口 =====
void AtpTradeHandler::OnLogin(const ATPCustomerInfo &msg)
{
    const char *login_id = nullptr;
    if (get_login_mode() == LOGIN_MODE_CUST_ID)
    {
        login_id = msg.GetCustId();
        set_account_login_status(login_id, true);
    }
    else
    {
        const int fund_account_id_len = msg.FundAccountArraySize();
        for (int i = 0; i < fund_account_id_len; i++)
        {
            const char *login_id = msg.FundAccountArray_GetFundAccountId(i);
            set_account_login_status(login_id, true);
        }
    }

    // 根据ATP返回的账户信息更新系统账户信息
    _update_accounts_from_atp_info(msg);
}

void AtpTradeHandler::OnLogout(const char *desc)
{
    TRADE_LOG_INFO("ATP logout callback: {}", desc ? desc : "");

    clear_all_login_status();
    _current_status.store(STATUS_CONNECTED);

    on_status_changed(STATUS_LOGOUT_SUCCESS, "All accounts logged out");
}

void AtpTradeHandler::OnRecovering(const char *desc)
{
    _current_status.store(STATUS_CONNECTING); // 使用已有的状态
    TRADE_LOG_INFO("ATP recovering: {}", desc ? desc : "");
}

void AtpTradeHandler::OnRspSubsPlatFormStateResult(const ATPRspErrorInfo &error_info,
                                                   const int64_t request_id)
{
}

void AtpTradeHandler::OnRtnPlatFormStatePushResult(const ATPRtnPlatFormStatePushResultMsg &msg)
{
}

void AtpTradeHandler::OnRspCashAuctionOrder(const ATPRspCashAuctionOrderMsg &msg,
                                            const ATPRspErrorInfo &error_info,
                                            const int64_t request_id)
{
    // 转换ATP委托响应为系统数据
    TradeOrderRspData order_rsp_data = _convert_atp_order_rsp_to_trade_order_rsp(msg, error_info);
    order_rsp_data.request_id = request_id;
    on_order_rsp_update(order_rsp_data);

    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }
}

void AtpTradeHandler::OnRtnCashAuctionOrder(const ATPRtnCashAuctionOrderMsg &msg)
{
    // ATP订单主推消息 → TradeOrderData
    TradeOrderData order_data = _convert_atp_order_to_trade_order_rtn(msg);
    on_order_update(order_data);
}

void AtpTradeHandler::OnRspCashCancelOrder(const ATPRspCashCancelOrderMsg &msg,
                                           const ATPRspErrorInfo &error_info,
                                           const int64_t request_id)
{
    // 转换ATP撤单响应为系统数据
    TradeOrderCancelRsqData cancel_rsp_data = _convert_atp_cancel_rsp_to_trade_cancel_rsp(msg, error_info);

    cancel_rsp_data.request_id = request_id;
    on_cancel_rsp_update(cancel_rsp_data);

    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }
}

void AtpTradeHandler::OnRspCashShareQueryResult(const ATPRspCashShareQueryResultMsg &msg,
                                                const int64_t request_id,
                                                const ATPRspErrorInfo &error_info,
                                                const bool isLast)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送持仓数据
    PositionData position_data = _convert_atp_position_to_position_data(msg, error_info, isLast);
    position_data.request_id = request_id;
    on_position_query_result(position_data);
}

void AtpTradeHandler::OnRspCashFundQueryResult(const ATPRspCashFundQueryResultMsg &msg,
                                               const int64_t request_id,
                                               const ATPRspErrorInfo &error_info,
                                               const bool isLast)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送资金数据
    AccountData account_data = _convert_atp_fund_to_account_data(msg, error_info, isLast);
    account_data.request_id = request_id;

    on_account_query_result(account_data, account_data.current_account_id);
}

void AtpTradeHandler::OnRspCashExtFundTransferResult(const ATPRspCashExtFundTransferResultOtherMsg &msg,
                                                     const int64_t request_id,
                                                     const ATPRspErrorInfo &error_info)
{
}

void AtpTradeHandler::OnRspCashExtQueryResultSecurityInfo(const ATPRspCashExtQueryResultSecurityInfoMsg &msg,
                                                          const int64_t request_id,
                                                          const ATPRspErrorInfo &error_info,
                                                          const bool isLast)
{
}

void AtpTradeHandler::OnRspCashAssetQueryResult(const ATPRspCashAssetQueryResultMsg &msg,
                                                const int64_t request_id,
                                                const ATPRspErrorInfo &error_info,
                                                const bool isLast)
{
}

void AtpTradeHandler::OnRspCashTradeCollectQueryResult(const ATPRspCashCollectQueryResultMsg &msg,
                                                       const int64_t request_id,
                                                       const ATPRspErrorInfo &error_info,
                                                       const bool isLast)
{
}

void AtpTradeHandler::OnRspCashMaxOrderQueryResult(const ATPRspCashMaxOrderQueryResultMsg &msg,
                                                   const int64_t request_id,
                                                   const ATPRspErrorInfo &error_info,
                                                   const bool isLast)
{
}

void AtpTradeHandler::OnRspExtQueryResultETFInfo(const ATPRspExtQueryResultETFInfoMsg &msg,
                                                 const int64_t request_id,
                                                 const ATPRspErrorInfo &error_info,
                                                 const bool isLast)
{
}

void AtpTradeHandler::OnRspExtQueryResultETFComponentInfo(const ATPRspExtQueryResultETFComponentInfoMsg &msg,
                                                          const int64_t request_id,
                                                          const ATPRspErrorInfo &error_info,
                                                          const bool isLast)
{
}

void AtpTradeHandler::OnRspOpuQueryResult(const ATPRspOpuQueryResultMsg &msg,
                                          const int64_t request_id,
                                          const ATPRspErrorInfo &error_info,
                                          const bool isLast)
{
}

void AtpTradeHandler::OnRspExtShareTransferResult(const ATPRspExtShareTransferResultMsg &msg,
                                                  const int64_t request_id,
                                                  const ATPRspErrorInfo &error_info)
{
}

void AtpTradeHandler::OnRspCashBankSecuritiesFundTransferResult(
    const atp::quant_api::ATPRspCashBankSecuritiesFundTransferResultMsg &msg,
    const int64_t request_id,
    const atp::quant_api::ATPRspErrorInfo &error_info)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送银证转账数据
    BankSecuritiesTransferData transfer_data = _convert_atp_bank_securities_transfer_to_data(msg, error_info);
    transfer_data.request_id = request_id;
    on_bank_securities_transfer_update(transfer_data);
}

void AtpTradeHandler::OnRspCashExternalInsTETransFundResp(
    const atp::quant_api::ATPRspCashExternalInsTETransFundRespMsg &msg,
    const int64_t request_id,
    const atp::quant_api::ATPRspErrorInfo &error_info)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送资金划拨数据
    FundTransferData transfer_data = _convert_atp_fund_transfer_to_data(msg, error_info);
    transfer_data.request_id = request_id;
    on_fund_transfer_update(transfer_data);
}

void AtpTradeHandler::OnRspCashOrderQueryResult(const ATPRspCashOrderQueryResultMsg &msg,
                                                const int64_t request_id,
                                                const ATPRspErrorInfo &error_info,
                                                const bool isLast)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送订单查询结果数据
    TradeOrderData order_data = _convert_atp_order_query_to_trade_order(msg, error_info, isLast);
    order_data.request_id = request_id;
    on_order_query_result(order_data);
}

void AtpTradeHandler::OnRspCashTradeOrderQueryResult(const ATPRspCashTradeOrderQueryResultMsg &msg,
                                                     const int64_t request_id,
                                                     const ATPRspErrorInfo &error_info,
                                                     const bool isLast)
{
    if (error_info.error_id != 0)
    {
        _handle_atp_error(error_info, request_id);
        return;
    }

    // 转换并推送成交数据
    TradeFillData fill_data = _convert_atp_trade_query_to_trade_fill(msg, error_info, isLast);
    fill_data.request_id = request_id;
    on_fill_query_result(fill_data);
}

// ===== 私有辅助方法 =====
int AtpTradeHandler::_initialize_atp_api(const TradeConfig *config)
{
    TRADE_LOG_INFO("ATP API initialization starting...");

    // 先初始化ATP API
    _atp_properties = std::make_unique<ATPProperties>();

    // 创建ATP属性
    TRADE_LOG_INFO("Creating ATP properties...");
    if (_create_atp_properties(config) != 0)
    {
        return -1;
    }
    TRADE_LOG_INFO("ATP properties created successfully");

    TRADE_LOG_INFO("Calling ATPQuantAPI::Init()...");
    ATPErrorCodeType ret = ATPQuantAPI::Init(_atp_properties.get());
    if (ret != ATPErrorCode::kSuccess)
    {
        set_error("ATP API init failed, error code: " + std::to_string(ret));
        return -1;
    }

    // 创建ATP API实例
    TRADE_LOG_INFO("Creating ATP API instance...");
    _atp_api = std::make_unique<ATPQuantAPI>(this, _atp_properties.get());
    if (!_atp_api)
    {
        set_error("Failed to create ATP API instance");
        return -1;
    }

    TRADE_LOG_INFO("ATP API version: {}", _atp_api->GetVersion());
    TRADE_LOG_INFO("ATP API initialization completed successfully");
    return 0;
}

int AtpTradeHandler::_create_atp_properties(const TradeConfig *config)
{
    TRADE_LOG_INFO("Creating ATP properties configuration...");

    // 确定XML配置文件路径
    const char *xml_config_path = nullptr;
    if (config && strlen(config->extended_config_file) > 0)
    {
        xml_config_path = config->extended_config_file;
    }

    // 解析XML配置
    TRADE_LOG_INFO("Parsing ATP XML configuration...");
    if (_parse_atp_xml_config(xml_config_path) != 0)
    {
        TRADE_LOG_ERROR("Failed to parse ATP XML config");
        return -1;
    }

    // 设置所有ATP属性
    TRADE_LOG_INFO("Setting ATP properties from XML configuration...");

    // 设置日志相关属性
    _atp_properties->SetValue(prop::kLogLevel, _log_level);
    _atp_properties->SetValue(prop::kCommonLogPath, _common_log_path.c_str());
    _atp_properties->SetValue(prop::kIndicatorLogPath, _indicator_log_path.c_str());
    _atp_properties->SetValue(prop::kIoLogPath, _io_log_path.c_str());

    // 设置加密和延迟相关属性
    _atp_properties->SetValue(prop::kEncryptSchema, _encrypt_schema);
    _atp_properties->SetValue(prop::kIsEnableLatency, _is_enable_latency);

    // 设置线程相关属性
    _atp_properties->SetValue(prop::kCallbackResourceMode, _callback_resource_mode);
    _atp_properties->SetValue(prop::kRecevieThreadCpu, _receive_thread_cpu);
    _atp_properties->SetValue(prop::kSendThreadCpu, _send_thread_cpu);

    // 设置网络相关属性
    _atp_properties->SetValue(prop::kIsTcpDirect, _is_tcp_direct);
    _atp_properties->SetValue(prop::kBindIpAddress, _bind_ip_address.c_str());
    _atp_properties->SetValue(prop::kRetransmitMode, _retransmit_mode);
    _atp_properties->SetValue(prop::kMultiChannelFlag, _multi_channel_flag);
    _atp_properties->SetValue(prop::kConnectionProtocol, _connection_protocol);

    // 设置必要的连接属性
    _atp_properties->SetValue(prop::kLocations, _locations.c_str());
    _atp_properties->SetValue(prop::kOrderWay, _order_way.c_str());
    _atp_properties->SetValue(prop::kClientFeatureCode, _client_feature_code.c_str());

    // 设置网关用户名和密码（如果有）
    if (!_agw_user.empty())
    {
        _atp_properties->SetValue(prop::kAgwUser, _agw_user.c_str());
    }
    if (!_agw_password.empty())
    {
        _atp_properties->SetValue(prop::kAgwPassword, _agw_password.c_str());
    }

    TRADE_LOG_INFO("All ATP properties configured successfully");
    TRADE_LOG_INFO("  - Log Level: {}", _log_level);
    TRADE_LOG_INFO("  - Encrypt Schema: {}", _encrypt_schema);
    TRADE_LOG_INFO("  - Callback Resource Mode: {}", _callback_resource_mode);
    TRADE_LOG_INFO("  - Receive Thread CPU: {}", _receive_thread_cpu);
    TRADE_LOG_INFO("  - Send Thread CPU: {}", _send_thread_cpu);
    TRADE_LOG_INFO("  - TCP Direct: {}", _is_tcp_direct);
    TRADE_LOG_INFO("  - Retransmit Mode: {}", _retransmit_mode);
    TRADE_LOG_INFO("  - Multi Channel Flag: {}", _multi_channel_flag);
    TRADE_LOG_INFO("  - Connection Protocol: {}", _connection_protocol);
    TRADE_LOG_INFO("  - Locations: {}", _locations);
    TRADE_LOG_INFO("  - Order Way: {}", _order_way);
    TRADE_LOG_INFO("  - Client Feature Code: {}", _client_feature_code);

    return 0;
}

int AtpTradeHandler::_parse_atp_xml_config(const char *xml_file_path)
{
    std::string config_file = xml_file_path ? xml_file_path : "atp_quant_api.xml";
    TRADE_LOG_INFO("Loading ATP configuration from: {}", config_file);

    try
    {
        XMLDocument doc;
        XMLError result = doc.LoadFile(config_file.c_str());

        if (result != XML_SUCCESS)
        {
            TRADE_LOG_ERROR("Failed to load ATP XML file: {}, error: {}", config_file, doc.ErrorStr());
            return -1;
        }

        XMLElement *root = doc.FirstChildElement("quant_api");
        if (!root)
        {
            TRADE_LOG_ERROR("Invalid ATP XML format: missing root element 'quant_api'");
            return -1;
        }

        TRADE_LOG_INFO("Successfully loaded ATP XML config: {}", config_file);

        XMLElement *element = nullptr;

        // log_level (日志级别) - 0:kNone 1:kFatal 2:kError 3:kWarning 4:kInfo 5:kDebug 6:kVerbose
        element = root->FirstChildElement("log_level");
        if (element && element->GetText())
        {
            _log_level = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP log_level: {}", _log_level);
        }
        else
        {
            _log_level = 4; // 默认Info级别
            TRADE_LOG_WARN("log_level not found, using default: {}", _log_level);
        }

        // common_log_path (业务日志路径)
        element = root->FirstChildElement("common_log_path");
        if (element && element->GetText())
        {
            _common_log_path = element->GetText();
            TRADE_LOG_INFO("ATP common_log_path: {}", _common_log_path);
        }
        else
        {
            _common_log_path = "./logs";
            TRADE_LOG_INFO("ATP common_log_path: ./logs (current directory)");
        }

        // indicator_log_path (指标日志路径)
        element = root->FirstChildElement("indicator_log_path");
        if (element && element->GetText())
        {
            _indicator_log_path = element->GetText();
            TRADE_LOG_INFO("ATP indicator_log_path: {}", _indicator_log_path);
        }
        else
        {
            _indicator_log_path = "./logs";
            TRADE_LOG_INFO("ATP indicator_log_path: ./logs (current directory)");
        }

        // io_log_path (IO日志路径)
        element = root->FirstChildElement("io_log_path");
        if (element && element->GetText())
        {
            _io_log_path = element->GetText();
            TRADE_LOG_INFO("ATP io_log_path: {}", _io_log_path);
        }
        else
        {
            _io_log_path = "./logs";
            TRADE_LOG_INFO("ATP io_log_path: ./logs (current directory)");
        }

        // encrypt_schema (加密模式) - 0:kTransMode 1:kEncryptMode
        element = root->FirstChildElement("encrypt_schema");
        if (element && element->GetText())
        {
            _encrypt_schema = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP encrypt_schema: {}", _encrypt_schema);
        }
        else
        {
            _encrypt_schema = 1; // 默认加密模式
            TRADE_LOG_WARN("encrypt_schema not found, using default: {}", _encrypt_schema);
        }

        // is_enable_latency (是否启用时延统计)
        element = root->FirstChildElement("is_enable_latency");
        if (element && element->GetText())
        {
            _is_enable_latency = std::stoi(element->GetText()) != 0;
            TRADE_LOG_INFO("ATP is_enable_latency: {}", _is_enable_latency);
        }
        else
        {
            _is_enable_latency = false;
            TRADE_LOG_INFO("ATP is_enable_latency: false (default)");
        }

        // callback_resource_mode (回调线程资源模式) - 0:kLowResourceMode 1:kLowLatencyMode
        element = root->FirstChildElement("callback_resource_mode");
        if (element && element->GetText())
        {
            _callback_resource_mode = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP callback_resource_mode: {}", _callback_resource_mode);
        }
        else
        {
            _callback_resource_mode = 1; // 默认低延迟模式
            TRADE_LOG_INFO("ATP callback_resource_mode: 1 (default - low latency)");
        }

        // recevie_thread_cpu (回调线程绑定CPU) - 255表示不绑定
        element = root->FirstChildElement("recevie_thread_cpu");
        if (element && element->GetText())
        {
            _receive_thread_cpu = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP recevie_thread_cpu: {}", _receive_thread_cpu);
        }
        else
        {
            _receive_thread_cpu = 255; // 默认不绑定
            TRADE_LOG_INFO("ATP recevie_thread_cpu: 255 (default - no binding)");
        }

        // send_thread_cpu (发送线程绑定CPU) - 255表示不绑定
        element = root->FirstChildElement("send_thread_cpu");
        if (element && element->GetText())
        {
            _send_thread_cpu = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP send_thread_cpu: {}", _send_thread_cpu);
        }
        else
        {
            _send_thread_cpu = 255; // 默认不绑定
            TRADE_LOG_INFO("ATP send_thread_cpu: 255 (default - no binding)");
        }

        // is_tcp_direct (是否启用TCPDirect)
        element = root->FirstChildElement("is_tcp_direct");
        if (element && element->GetText())
        {
            _is_tcp_direct = std::stoi(element->GetText()) != 0;
            TRADE_LOG_INFO("ATP is_tcp_direct: {}", _is_tcp_direct);
        }
        else
        {
            _is_tcp_direct = false;
            TRADE_LOG_INFO("ATP is_tcp_direct: false (default)");
        }

        // bind_ip_address (绑定本地网卡地址)
        element = root->FirstChildElement("bind_ip_address");
        if (element && element->GetText())
        {
            _bind_ip_address = element->GetText();
            TRADE_LOG_INFO("ATP bind_ip_address: {}", _bind_ip_address);
        }
        else
        {
            _bind_ip_address = "";
            TRADE_LOG_INFO("ATP bind_ip_address: empty (no binding)");
        }

        // retransmit_mode (重传模式) - 0:kQuickMode 1:kRestartMode
        element = root->FirstChildElement("retransmit_mode");
        if (element && element->GetText())
        {
            _retransmit_mode = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP retransmit_mode: {}", _retransmit_mode);
        }
        else
        {
            _retransmit_mode = 0; // 默认快速模式
            TRADE_LOG_INFO("ATP retransmit_mode: 0 (default - quick mode)");
        }

        // multi_channel_flag (多通道推送自主订阅标志) - 255:kDefault 0:kActiveNoSubscribe 1:kActiveSubscribe
        element = root->FirstChildElement("multi_channel_flag");
        if (element && element->GetText())
        {
            _multi_channel_flag = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP multi_channel_flag: {}", _multi_channel_flag);
        }
        else
        {
            _multi_channel_flag = 255; // 默认值
            TRADE_LOG_INFO("ATP multi_channel_flag: 255 (default)");
        }

        // connection_protocol (协议类型) - 0:kTCPProtocol 1:kRUDPProtocol
        element = root->FirstChildElement("connection_protocol");
        if (element && element->GetText())
        {
            _connection_protocol = std::stoi(element->GetText());
            TRADE_LOG_INFO("ATP connection_protocol: {}", _connection_protocol);
        }
        else
        {
            _connection_protocol = 0; // 默认TCP协议
            TRADE_LOG_INFO("ATP connection_protocol: 0 (default - TCP)");
        }

        // locations (网关地址，必填)
        element = root->FirstChildElement("locations");
        if (element && element->GetText())
        {
            _locations = element->GetText();
            TRADE_LOG_INFO("ATP locations: {}", _locations);
        }
        else
        {
            TRADE_LOG_ERROR("locations is required in ATP XML config");
            return -1;
        }

        // order_way (委托方式，必填)
        element = root->FirstChildElement("order_way");
        if (element && element->GetText())
        {
            _order_way = element->GetText();
            TRADE_LOG_INFO("ATP order_way: {}", _order_way);
        }
        else
        {
            TRADE_LOG_ERROR("order_way is required in ATP XML config");
            return -1;
        }

        // client_feature_code (终端识别码，必填)
        element = root->FirstChildElement("client_feature_code");
        if (element && element->GetText())
        {
            _client_feature_code = element->GetText();
            TRADE_LOG_INFO("ATP client_feature_code: {}", _client_feature_code);
        }
        else
        {
            TRADE_LOG_ERROR("client_feature_code is required in ATP XML config");
            return -1;
        }

        // agw_user (网关用户名，可选)
        element = root->FirstChildElement("agw_user");
        if (element && element->GetText() && strlen(element->GetText()) > 0)
        {
            _agw_user = element->GetText();
            TRADE_LOG_INFO("ATP agw_user: {}", _agw_user);
        }
        else
        {
            _agw_user.clear();
            TRADE_LOG_INFO("ATP agw_user: not configured (anonymous login)");
        }

        // agw_password (网关密码，可选)
        element = root->FirstChildElement("agw_password");
        if (element && element->GetText() && strlen(element->GetText()) > 0)
        {
            _agw_password = element->GetText();
            TRADE_LOG_INFO("ATP agw_password: configured");
        }
        else
        {
            _agw_password.clear();
            TRADE_LOG_INFO("ATP agw_password: not configured");
        }

        TRADE_LOG_INFO("ATP XML configuration parsed successfully");
        return 0;
    }
    catch (const std::exception &e)
    {
        TRADE_LOG_ERROR("Exception parsing ATP XML config: {}", e.what());
        return -1;
    }
}

// ===== ATP消息构建方法 =====
std::unique_ptr<ATPReqCashAuctionOrderMsg> AtpTradeHandler::_build_atp_order_request(const TradeOrderRequest *order)
{
    auto atp_order = std::unique_ptr<ATPReqCashAuctionOrderMsg>(
        ATPReqCashAuctionOrderMsg::NewMessage(ATPBusinessTypeConst::kCashAuction));

    // 设置账户信息
    atp_order->SetCustId(order->account.cust_id);
    atp_order->SetFundAccountId(order->account.fund_account_id);
    atp_order->SetBranchId(order->account.branch_id);
    atp_order->SetPassword(order->account.password);

    // 根据市场选择证券账户
    if (order->market == MARKET_SSE)
    {
        atp_order->SetAccountId(order->account.account_id_sh);
        atp_order->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (order->market == MARKET_SZSE)
    {
        atp_order->SetAccountId(order->account.account_id_sz);
        atp_order->SetMarketId(ATPMarketIDConst::kShenZhen);
    }
    else
    {
        TRADE_LOG_ERROR("Unknown market type: {}", static_cast<int>(order->market));
        return nullptr;
    }

    // 设置证券信息
    atp_order->SetSecurityId(order->security_code);

    // 设置订单信息 - 使用正确的ATP常量
    atp_order->SetSide(order->side == SIDE_BUY ? ATPSideConst::kBuy : ATPSideConst::kSell);
    atp_order->SetOrderQty(order->qty);
    atp_order->SetPrice(order->price);

    // 根据订单类型设置ATP订单类型
    switch (order->order_type)
    {
    case ORDER_TYPE_LIMIT:
        atp_order->SetOrderType(ATPOrderTypeConst::kFixedNew);
        break;
    case ORDER_TYPE_MARKET:
        atp_order->SetOrderType(ATPOrderTypeConst::kMarketTransferFixed);
        break;
    case ORDER_TYPE_BEST_PRICE:
        atp_order->SetOrderType(ATPOrderTypeConst::kLocalOptimalNew);
        break;
    default:
        atp_order->SetOrderType(ATPOrderTypeConst::kFixedNew);
        break;
    }

    atp_order->SetBatchClOrdNo(order->batch_id);

    return atp_order;
}

std::unique_ptr<ATPReqCashCancelOrderMsg> AtpTradeHandler::_build_atp_cancel_request(
    const AccountInfo *account, const int64_t orig_order_id, uint64_t orig_batch_id, MarketType market)
{

    auto atp_cancel = std::unique_ptr<ATPReqCashCancelOrderMsg>(
        ATPReqCashCancelOrderMsg::NewMessage());

    // 设置账户信息
    atp_cancel->SetCustId(account->cust_id);
    atp_cancel->SetFundAccountId(account->fund_account_id);
    atp_cancel->SetPassword(account->password);

    // 设置原始委托信息
    atp_cancel->SetOrigClOrdNo(orig_order_id);
    atp_cancel->SetBatchClOrdNo(orig_batch_id);

    // 设置市场 - 使用正确的ATP常量
    if (market == MARKET_SSE)
    {
        atp_cancel->SetMarketId(ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        atp_cancel->SetMarketId(ATPMarketIDConst::kShenZhen);
    }

    return atp_cancel;
}

// ===== 数据转换方法 - 只使用ATP实际存在的字段 =====
TradeOrderRspData AtpTradeHandler::_convert_atp_order_rsp_to_trade_order_rsp(const ATPRspCashAuctionOrderMsg &msg,
                                                                             const ATPRspErrorInfo &error_info)
{
    TradeOrderRspData order_rsp_data = {};

    order_rsp_data.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
    order_rsp_data.order_id = msg.GetClOrdNo();
    order_rsp_data.batch_id = msg.GetTransactTime();

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &order_rsp_data.account);

    // 填充ATP返回的具体信息
    strncpy(order_rsp_data.current_account_id, msg.GetAccountId(), sizeof(order_rsp_data.current_account_id) - 1);
    order_rsp_data.price = msg.GetPrice();
    order_rsp_data.qty = msg.GetOrderQty();
    order_rsp_data.side = _map_atp_side_to_order_side(msg.GetSide());
    order_rsp_data.order_type = _map_atp_order_type_to_order_type(msg.GetOrderType());
    order_rsp_data.business_type = msg.GetBusinessType();

    // 填充证券信息
    strncpy(order_rsp_data.security_info.code, msg.GetSecurityId(), sizeof(order_rsp_data.security_info.code) - 1);
    if (msg.GetSecuritySymbol() && strlen(msg.GetSecuritySymbol()) > 0)
    {
        strncpy(order_rsp_data.security_info.name, msg.GetSecuritySymbol(), sizeof(order_rsp_data.security_info.name) - 1);
    }
    order_rsp_data.security_info.market = _map_atp_market_to_market_type(msg.GetMarketId());

    // 使用统一的错误处理函数
    _fill_error_info(error_info, order_rsp_data.error_code, order_rsp_data.error_msg, sizeof(order_rsp_data.error_msg));

    return order_rsp_data;
}

TradeOrderCancelRsqData AtpTradeHandler::_convert_atp_cancel_rsp_to_trade_cancel_rsp(const ATPRspCashCancelOrderMsg &msg,
                                                                                     const ATPRspErrorInfo &error_info)
{
    TradeOrderCancelRsqData cancel_rsp_data = {};

    cancel_rsp_data.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    cancel_rsp_data.order_id = msg.GetClOrdNo();
    cancel_rsp_data.response_time = msg.GetTransactTime();
    cancel_rsp_data.batch_id = msg.GetBatchClOrdNo();

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &cancel_rsp_data.account);

    strncpy(cancel_rsp_data.current_account_id, msg.GetAccountId(), sizeof(cancel_rsp_data.current_account_id) - 1);

    // 使用统一的错误处理函数
    _fill_error_info(error_info, cancel_rsp_data.error_code, cancel_rsp_data.error_msg, sizeof(cancel_rsp_data.error_msg));

    return cancel_rsp_data;
}

TradeOrderData AtpTradeHandler::_convert_atp_order_to_trade_order_rtn(const ATPRtnCashAuctionOrderMsg &msg)
{
    TradeOrderData data = {};

    // 填充基本信息
    data.timestamp = get_current_timestamp();
    data.order_id = msg.GetClOrdNo();
    data.batch_id = msg.GetBatchClOrdNo();
    strncpy(data.current_account_id, msg.GetAccountId(), sizeof(data.current_account_id) - 1);
    data.current_market = _map_atp_market_to_market_type(msg.GetMarketId());

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &data.account);

    // 填充证券信息
    strncpy(data.security_info.code, msg.GetSecurityId(), sizeof(data.security_info.code) - 1);
    data.security_info.market = _map_atp_market_to_market_type(msg.GetMarketId());

    // 填充订单信息
    data.side = _map_atp_side_to_order_side(msg.GetSide());
    data.order_type = ORDER_TYPE_UNKNOWN;
    data.sign = _map_atp_order_sign_to_order_sign(msg.GetOrdSign());
    data.price = msg.GetPrice();
    data.qty = msg.GetOrderQty();
    data.leaves_qty = msg.GetLeavesQty();
    data.cum_qty = msg.GetCumQty();

    if (msg.GetCumQty() > 0)
    {
        data.avg_price = msg.GetTotalValueTraded() / msg.GetCumQty();
    }
    else
    {
        data.avg_price = 0.0;
    }
    data.cum_amt = msg.GetTotalValueTraded();

    data.status = _map_atp_status_to_order_status(msg.GetOrdStatus());
    data.order_time = msg.GetTransactTime();
    data.update_time = get_current_timestamp();

    data.error_code = msg.GetRejectReasonCode();

    // 正确复制错误消息
    const char *reject_reason = msg.GetOrdRejReason();
    if (reject_reason && strlen(reject_reason) > 0)
    {
        strncpy(data.error_msg, reject_reason, sizeof(data.error_msg) - 1);
        data.error_msg[sizeof(data.error_msg) - 1] = '\0'; // 确保字符串结束
    }
    else
    {
        data.error_msg[0] = '\0'; // 清空错误消息
    }

    if (msg.GetOrderId() && strlen(msg.GetOrderId()) > 0)
    {
        strncpy(data.exchange_order_id, msg.GetOrderId(), sizeof(data.exchange_order_id) - 1);
    }

    data.business_type = msg.GetBusinessType();
    data.opu_id = msg.GetOpuId();

    return data;
}

PositionData AtpTradeHandler::_convert_atp_position_to_position_data(const ATPRspCashShareQueryResultMsg &msg,
                                                                     const ATPRspErrorInfo &error_info,
                                                                     const bool is_last)
{
    PositionData data = {};

    data.timestamp = get_current_timestamp();
    data.is_last = is_last;
    strncpy(data.current_account_id, msg.GetAccountId(), sizeof(data.current_account_id) - 1);
    data.current_market = _map_atp_market_to_market_type(msg.GetMarketId());

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &data.account);

    // 如果系统中没有找到账户信息，补充ATP返回的营业部信息
    if (strlen(data.account.branch_id) == 0 && msg.GetBranchId() && strlen(msg.GetBranchId()) > 0)
    {
        strncpy(data.account.branch_id, msg.GetBranchId(), sizeof(data.account.branch_id) - 1);
    }

    // 填充证券信息
    strncpy(data.security_info.code, msg.GetSecurityId(), sizeof(data.security_info.code) - 1);
    strncpy(data.security_info.name, msg.GetSecuritySymbol(), sizeof(data.security_info.name) - 1);
    data.security_info.market = _map_atp_market_to_market_type(msg.GetMarketId());

    IQuoteParser *quote_parser = get_quote_parser();
    DepthData depth_data = {};
    bool has_depth_data = false;

    if (quote_parser)
    {
        if (quote_parser->get_depth_data_by_code(data.security_info.code, &depth_data) == 0)
        {
            has_depth_data = true;
        }
    }

    // 填充持仓信息
    data.total_qty = msg.GetLeavesQty();
    data.available_qty = msg.GetAvailableQty();
    data.frozen_qty = msg.GetLeavesQty() - msg.GetAvailableQty();
    data.cost_price = msg.GetCostPrice();
    data.market_price = has_depth_data && depth_data.last_price > 0 ? depth_data.last_price : msg.GetLastPrice();
    data.market_value = has_depth_data && depth_data.last_price > 0 ? depth_data.last_price * data.total_qty : msg.GetMarketValue();
    data.profit_loss = has_depth_data && depth_data.last_price > 0 ? (depth_data.last_price - data.cost_price) * data.total_qty : msg.GetProfitLoss();
    data.today_buy_qty = data.total_qty - msg.GetInitQty();
    data.today_sell_qty = msg.GetStockSale();

    if (data.market_value > 0 && data.cost_price > 0)
    {
        double cost_value = data.total_qty * data.cost_price;
        if (cost_value > 0)
        {
            data.profit_loss_ratio = data.profit_loss / cost_value;
        }
    }

    data.update_time = get_current_timestamp();
    data.security_type = msg.GetSecurityType();

    // 使用ATPRspErrorInfo处理错误信息
    data.error_code = error_info.error_id;
    if (error_info.GetErrorMsg() && strlen(error_info.GetErrorMsg()) > 0)
    {
        strncpy(data.error_msg, error_info.GetErrorMsg(), sizeof(data.error_msg) - 1);
        data.error_msg[sizeof(data.error_msg) - 1] = '\0';
    }
    else
    {
        data.error_msg[0] = '\0';
    }

    return data;
}

AccountData AtpTradeHandler::_convert_atp_fund_to_account_data(const ATPRspCashFundQueryResultMsg &msg,
                                                               const ATPRspErrorInfo &error_info,
                                                               const bool is_last)
{
    AccountData data = {};

    data.timestamp = get_current_timestamp();
    data.is_last = is_last;

    // 使用辅助函数填充账户信息
    bool found_complete_info = _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &data.account);

    // 设置当前数据对应的具体证券账户ID
    const char *account_id = msg.GetAccountId();
    if (account_id && strlen(account_id) > 0)
    {
        strncpy(data.current_account_id, account_id, sizeof(data.current_account_id) - 1);

        if (found_complete_info)
        {
            if (strcmp(data.account.account_id_sh, account_id) == 0)
            {
                data.current_market = MARKET_SSE;
            }
            else if (strcmp(data.account.account_id_sz, account_id) == 0)
            {
                data.current_market = MARKET_SZSE;
            }
            else
            {
                data.current_market = MARKET_UNKNOWN;
            }
        }
        else
        {
            data.current_market = MARKET_UNKNOWN;
        }
    }

    // 填充资金信息
    data.available_cash = msg.GetAvailableT1();
    data.cash_balance = msg.GetLeavesValue();
    data.frozen_cash = msg.GetFrozenAll();
    data.withdrawal_cash = msg.GetAvailableT0();

    if (msg.GetCurrency() && strlen(msg.GetCurrency()) > 0)
    {
        strncpy(data.currency, msg.GetCurrency(), sizeof(data.currency) - 1);
    }
    else
    {
        strncpy(data.currency, "CNY", sizeof(data.currency) - 1);
    }

    data.total_asset = data.cash_balance;
    data.update_time = get_current_timestamp();

    // 使用ATPRspErrorInfo处理错误信息
    data.error_code = error_info.error_id;
    if (error_info.GetErrorMsg() && strlen(error_info.GetErrorMsg()) > 0)
    {
        strncpy(data.error_msg, error_info.GetErrorMsg(), sizeof(data.error_msg) - 1);
        data.error_msg[sizeof(data.error_msg) - 1] = '\0';
    }
    else
    {
        data.error_msg[0] = '\0';
    }

    return data;
}

int8_t AtpTradeHandler::_map_ft_order_query_condition_to_atp_order_query_condition(const OrderQueryCondition condition)
{
    switch (condition)
    {
    case ORDER_QUERY_CONDITION_ALL:
        return ATPOrderQueryConditionConst::kAll;
    case ORDER_QUERY_CONDITION_CANCEL_AVAILABLE:
        return ATPOrderQueryConditionConst::kCancelAvailable;
    case ORDER_QUERY_CONDITION_TRADE_EXEC_OR_CANCELLED:
        return ATPOrderQueryConditionConst::kTradeExecOrCanceled;
    case ORDER_QUERY_CONDITION_UNAVAILABLE_CANCEL:
        return ATPOrderQueryConditionConst::kUnavailableCancel;
    case ORDER_QUERY_CONDITION_ALL_WITH_CANCEL:
        return ATPOrderQueryConditionConst::kAllWithCancleOrd;
    case ORDER_QUERY_CONDITION_ALL_EXCEPT_INTERNAL_REJ:
        return ATPOrderQueryConditionConst::kAllExceptInRejOrd;
    }

    return ATPOrderQueryConditionConst::kAll;
}

int8_t AtpTradeHandler::_map_ft_trade_query_condition_to_atp_trade_query_condition(const TradeQueryCondition condition)
{
    switch (condition)
    {
    case TRADE_QUERY_CONDITION_GLOBAL_PARAMS:
        return ATPTradeOrderQueryConditionConst::kGlobalParams;
    case TRADE_QUERY_CONDITION_TRADE_REPORT:
        return ATPTradeOrderQueryConditionConst::kTradeReport;
    case TRADE_QUERY_CONDITION_TRADE_AND_CANCEL:
        return ATPTradeOrderQueryConditionConst::kTradeReportAndCancleSuccess;
    case TRADE_QUERY_CONDITION_ALL_AVAILABLE:
        return ATPTradeOrderQueryConditionConst::kAllAvailable;
    }
    return ATPTradeOrderQueryConditionConst::kGlobalParams;
}

int8_t AtpTradeHandler::_map_ft_return_seq_to_atp_return_seq(const ReturnSeqType condition)
{
    switch (condition)
    {
    case RETURN_SEQ_DEFAULT:
        return ATPReturnSeqConst::kDefault;
    case RETURN_SEQ_TIME_ASC:
        return ATPReturnSeqConst::kTimeOrder;
    case RETURN_SEQ_TIME_DESC:
        return ATPReturnSeqConst::kTimeOrderRe;
    }
    return ATPReturnSeqConst::kDefault;
}

// ===== 状态映射 - 使用正确的ATP常量 =====
OrderStatus AtpTradeHandler::_map_atp_status_to_order_status(int ord_status)
{
    // 根据ATP的订单状态映射到系统状态
    switch (ord_status)
    {
    case ATPOrdStatusConst::kNew:
        return ORDER_STATUS_NEW;
    case ATPOrdStatusConst::kPartiallyFilled:
        return ORDER_STATUS_PARTIAL_FILLED;
    case ATPOrdStatusConst::kFilled:
        return ORDER_STATUS_FILLED;
    case ATPOrdStatusConst::kCancelled:
        return ORDER_STATUS_CANCELLED;
    case ATPOrdStatusConst::kReject:
        return ORDER_STATUS_REJECTED;
    case ATPOrdStatusConst::kPartiallyFilledPartiallyCancelled:
        return ORDER_STATUS_PARTIAL_FILLED;
    case ATPOrdStatusConst::kWaitCancelled:
        return ORDER_STATUS_PENDING_CANCEL;
    case ATPOrdStatusConst::kPartiallyFilledWaitCancelled:
        return ORDER_STATUS_PENDING_CANCEL;
    case ATPOrdStatusConst::kUnSend:
        return ORDER_STATUS_PENDING_NEW;
    default:
        return ORDER_STATUS_UNKNOWN;
    }
}

OrderSide AtpTradeHandler::_map_atp_side_to_order_side(char side)
{
    // ATP使用字符 '1' 表示买, '2' 表示卖
    return (side == ATPSideConst::kBuy) ? SIDE_BUY : SIDE_SELL;
}

OrderType AtpTradeHandler::_map_atp_order_type_to_order_type(char order_type)
{
    // 根据ATP的订单类型映射
    switch (order_type)
    {
    case ATPOrderTypeConst::kFixedNew:
    case ATPOrderTypeConst::kFixed:
        return ORDER_TYPE_LIMIT;
    case ATPOrderTypeConst::kMarket:
    case ATPOrderTypeConst::kMarketTransferFixed:
        return ORDER_TYPE_MARKET;
    case ATPOrderTypeConst::kLocalOptimalNew:
    case ATPOrderTypeConst::kLocalOptimal:
        return ORDER_TYPE_BEST_PRICE;
    default:
        return ORDER_TYPE_LIMIT; // 默认限价
    }
}

OrderSign AtpTradeHandler::_map_atp_order_sign_to_order_sign(uint8_t ord_sign)
{
    switch (ord_sign)
    {
    case ATPOrderSignConst::kOrder:
        return ORDER_SIGN_ORDER;
    case ATPOrderSignConst::kCancel:
        return ORDER_SIGN_CANCEL;
    default:
        return ORDER_SIGN_UNKNOWN;
    }
}

MarketType AtpTradeHandler::_map_atp_market_to_market_type(int market_id)
{
    // ATP使用 101 表示上海, 102 表示深圳
    switch (market_id)
    {
    case ATPMarketIDConst::kShangHai:
        return MARKET_SSE;
    case ATPMarketIDConst::kShenZhen:
        return MARKET_SZSE;
    default:
        return MARKET_UNKNOWN;
    }
}

int AtpTradeHandler::_login_all_accounts()
{
    // 清空所有登录状态
    clear_all_login_status();

    // 先获取账户列表的副本，然后释放锁
    std::vector<AccountInfo> accounts_to_login;
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_accounts.empty())
        {
            set_error("No accounts configured for login");
            return -1;
        }

        TRADE_LOG_INFO("Starting ATP login for {} accounts", _accounts.size());

        // 复制账户列表
        accounts_to_login.reserve(_accounts.size());
        for (const auto &account_pair : _accounts)
        {
            accounts_to_login.push_back(account_pair.second);
        }
    } // 锁在这里释放

    // 在没有锁的情况下执行登录
    int success_count = 0;
    int total_count = 0;

    for (const auto &account : accounts_to_login)
    {
        total_count++;

        if (_login_single_account(account) == 0)
        {
            success_count++;
            TRADE_LOG_INFO("Account {} login initiated successfully", account.login_id);
        }
        else
        {
            TRADE_LOG_WARN("Account {} login failed, but continuing with other accounts", account.login_id);
        }
    }

    TRADE_LOG_INFO("ATP login initiated: {}/{} accounts", success_count, total_count);

    // 只要有账户登录成功就认为是成功的
    if (success_count > 0)
    {
        return 0;
    }
    else
    {
        set_error("All accounts failed to login");
        return -1;
    }
}

int AtpTradeHandler::_login_single_account(const AccountInfo &account)
{
    // 创建登录属性
    auto login_property = std::unique_ptr<atp::quant_api::ATPLoginProperty>(
        atp::quant_api::ATPLoginProperty::NewMessage());

    login_property->SetUserId(account.login_id);
    login_property->SetPassword(account.password);
    if (get_login_mode() == LOGIN_MODE_CUST_ID)
    {
        login_property->SetLoginMode(atp::quant_api::ATPLoginModeConst::kCustIDMode);
    }
    else
    {
        login_property->SetLoginMode(atp::quant_api::ATPLoginModeConst::kFundAccountIDMode);
    }

    if (strlen(account.branch_id) > 0)
    {
        login_property->SetBranchId(account.branch_id);
    }

    // 调用ATP登录接口
    atp::quant_api::ATPErrorCodeType ret = _atp_api->Login(login_property.get());
    if (ret != atp::quant_api::ATPErrorCode::kSuccess)
    {
        TRADE_LOG_ERROR("ATP login failed for account {}, error code: {}",
                        account.login_id, ret);
        return -1;
    }

    TRADE_LOG_INFO("ATP login request sent for account: {}", account.login_id);
    return 0;
}

int AtpTradeHandler::_logout_all_accounts()
{
    if (!_atp_api)
    {
        return 0;
    }

    // 使用基类方法检查是否有已登录账户
    int logged_in_count = get_logged_in_account_count();

    if (logged_in_count <= 0)
    {
        TRADE_LOG_INFO("No accounts to logout");
        return 0;
    }

    TRADE_LOG_INFO("Logging out {} accounts", logged_in_count);

    // 调用ATP API进行登出
    atp::quant_api::ATPErrorCodeType ret = _atp_api->Logout();
    if (ret != atp::quant_api::ATPErrorCode::kSuccess)
    {
        TRADE_LOG_ERROR("ATP logout failed, error code: {}", ret);

        // 即使失败也要清理本地状态 - 使用基类方法
        clear_all_login_status();
        _current_status.store(STATUS_CONNECTED);

        return -1;
    }

    // 注意：实际的状态清理会在OnLogout回调中完成
    TRADE_LOG_INFO("ATP logout initiated");
    return 0;
}

void AtpTradeHandler::_update_accounts_from_atp_info(const atp::quant_api::ATPCustomerInfo &msg)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 遍历ATP返回的资金账户信息
    for (uint32_t i = 0; i < msg.FundAccountArraySize(); i++)
    {
        const char *fund_account_id = msg.FundAccountArray_GetFundAccountId(i);
        const char *branch_id = msg.FundAccountArray_GetBranchId(i);
        const char *login_id = nullptr;
        if (get_login_mode() == LOGIN_MODE_CUST_ID)
        {
            login_id = msg.GetCustId();
        }
        else
        {
            login_id = fund_account_id;
        }

        // 查找对应的账户信息并更新
        std::string account_key = std::string(login_id) + "|" + fund_account_id;
        auto it = _accounts.find(account_key);

        if (it != _accounts.end())
        {
            AccountInfo &account = it->second;

            // 更新营业部ID
            if (strlen(branch_id) > 0)
            {
                strncpy(account.branch_id, branch_id, sizeof(account.branch_id) - 1);
            }

            // 遍历证券账户信息
            for (uint32_t j = 0; j < msg.FundAccountArray_AccountArraySize(i); j++)
            {
                const char *account_id = msg.FundAccountArray_AccountArray_GetAccountId(i, j);
                uint16_t market_id = msg.FundAccountArray_AccountArray_GetMarketId(i, j);

                // 根据市场ID设置对应的证券账户
                if (market_id == atp::quant_api::ATPMarketIDConst::kShangHai)
                {
                    strncpy(account.account_id_sh, account_id, sizeof(account.account_id_sh) - 1);
                }
                else if (market_id == atp::quant_api::ATPMarketIDConst::kShenZhen)
                {
                    strncpy(account.account_id_sz, account_id, sizeof(account.account_id_sz) - 1);
                }
            }

            TRADE_LOG_INFO("Updated account info for: {}", account_key);
        }
    }
}

// ===== ATP消息构建方法 =====
std::unique_ptr<atp::quant_api::ATPReqCashBankSecuritiesFundTransferOtherMsg>
AtpTradeHandler::_build_atp_bank_securities_transfer_request(const AccountInfo *account,
                                                             BankSecuritiesTransferDirection direction,
                                                             MarketType market,
                                                             double transfer_amount,
                                                             const char *currency,
                                                             const char *bank_password,
                                                             const char *fund_password,
                                                             const char *remark)
{

    auto atp_request = std::unique_ptr<atp::quant_api::ATPReqCashBankSecuritiesFundTransferOtherMsg>(
        atp::quant_api::ATPReqCashBankSecuritiesFundTransferOtherMsg::NewMessage());

    // 设置账户信息
    atp_request->SetCustId(account->cust_id);
    atp_request->SetFundAccountId(account->fund_account_id);
    if (market == MARKET_SSE)
    {
        atp_request->SetAccountId(account->account_id_sh);
        atp_request->SetMarketId(atp::quant_api::ATPMarketIDConst::kShangHai);
    }
    else if (market == MARKET_SZSE)
    {
        atp_request->SetAccountId(account->account_id_sz);
        atp_request->SetMarketId(atp::quant_api::ATPMarketIDConst::kShenZhen);
    }
    atp_request->SetBranchId(account->branch_id);
    atp_request->SetPassword(account->password);
    atp_request->SetTransferWay(static_cast<uint8_t>(direction));

    // 设置转账金额
    atp_request->SetTransferValue(transfer_amount);

    // 设置货币种类（如果提供）
    if (currency && strlen(currency) > 0)
    {
        atp_request->SetCurrency(currency);
    }
    else
    {
        atp_request->SetCurrency("CNY"); // 默认人民币
    }

    // 设置银行密码（如果提供）
    if (bank_password && strlen(bank_password) > 0)
    {
        atp_request->SetBankPassword(bank_password);
    }

    // 设置资金密码（如果提供）
    if (fund_password && strlen(fund_password) > 0)
    {
        atp_request->SetFundPassword(fund_password);
    }

    // 设置备注（如果提供）
    if (remark && strlen(remark) > 0)
    {
        atp_request->SetRemark(remark);
    }

    return atp_request;
}

std::unique_ptr<atp::quant_api::ATPReqCashExternalInsTETransFundReqMsg>
AtpTradeHandler::_build_atp_fund_transfer_request(const AccountInfo *account,
                                                  const char *fund_out_account_id,
                                                  MarketType fund_out_market,
                                                  const char *fund_in_account_id,
                                                  MarketType fund_in_market,
                                                  double transfer_amount)
{

    auto atp_request = std::unique_ptr<atp::quant_api::ATPReqCashExternalInsTETransFundReqMsg>(
        atp::quant_api::ATPReqCashExternalInsTETransFundReqMsg::NewMessage());

    // 设置账户信息
    atp_request->SetCustId(account->cust_id);
    atp_request->SetFundAccountId(account->fund_account_id);
    atp_request->SetBranchId(account->branch_id);
    atp_request->SetPassword(account->password);

    // 设置转出账户信息
    atp_request->SetFundOutAccountId(fund_out_account_id);
    if (fund_out_market == MARKET_SSE)
    {
        atp_request->SetFundOutMarketId(atp::quant_api::ATPMarketIDConst::kShangHai);
    }
    else if (fund_out_market == MARKET_SZSE)
    {
        atp_request->SetFundOutMarketId(atp::quant_api::ATPMarketIDConst::kShenZhen);
    }

    // 设置转入账户信息
    atp_request->SetFundInAccountId(fund_in_account_id);
    if (fund_in_market == MARKET_SSE)
    {
        atp_request->SetFundInMarketId(atp::quant_api::ATPMarketIDConst::kShangHai);
    }
    else if (fund_in_market == MARKET_SZSE)
    {
        atp_request->SetFundInMarketId(atp::quant_api::ATPMarketIDConst::kShenZhen);
    }

    // 设置划拨金额
    atp_request->SetValue(transfer_amount);

    return atp_request;
}

// ===== 数据转换方法 =====
BankSecuritiesTransferData AtpTradeHandler::_convert_atp_bank_securities_transfer_to_data(
    const atp::quant_api::ATPRspCashBankSecuritiesFundTransferResultMsg &msg,
    const atp::quant_api::ATPRspErrorInfo &error_info)
{

    BankSecuritiesTransferData data = {};

    // 填充基本信息
    data.timestamp = get_current_timestamp();

    // 填充账户信息
    if (get_login_mode() == LOGIN_MODE_CUST_ID)
    {
        strncpy(data.login_id, msg.GetCustId(), sizeof(data.login_id) - 1);
    }
    else
    {
        strncpy(data.login_id, msg.GetFundAccountId(), sizeof(data.login_id) - 1);
    }

    strncpy(data.cust_id, msg.GetCustId(), sizeof(data.cust_id) - 1);
    strncpy(data.fund_account_id, msg.GetFundAccountId(), sizeof(data.fund_account_id) - 1);
    strncpy(data.account_id, msg.GetAccountId(), sizeof(data.account_id) - 1);
    strncpy(data.branch_id, msg.GetBranchId(), sizeof(data.branch_id) - 1);

    // 填充转账信息
    // 根据ATP的转账方向进行映射
    uint8_t transfer_way = msg.GetTransferWay();
    if (transfer_way == 1)
    { // 1-银证转入
        data.direction = BANK_SECURITIES_TRANSFER_IN;
    }
    else if (transfer_way == 2)
    { // 2-银证转出
        data.direction = BANK_SECURITIES_TRANSFER_OUT;
    }

    // 填充响应信息
    data.serial_no = msg.GetSerialNo();
    data.transfer_serial_no = msg.GetTransferSerialNo();
    data.bank_transfer_serial_no = msg.GetBankTransferSerialNo();
    data.business_status = msg.GetBusinessStatus();
    data.transact_time = msg.GetTransactTime();

    data.error_code = 0;      // ATP成功回调默认为0
    data.error_msg[0] = '\0'; // 清空错误消息

    return data;
}

const char *AtpTradeHandler::_get_account_id_by_market(const AccountInfo *account, MarketType market) const
{
    switch (market)
    {
    case MARKET_SSE:
        return account->account_id_sh;
    case MARKET_SZSE:
        return account->account_id_sz;
    default:
        return nullptr;
    }
}

bool AtpTradeHandler::_is_market_account_valid(const AccountInfo *account, MarketType market) const
{
    const char *account_id = _get_account_id_by_market(account, market);
    return account_id && strlen(account_id) > 0;
}

bool AtpTradeHandler::_fill_account_info_from_atp(const char *cust_id, const char *fund_account_id, AccountInfo *account)
{
    const LoginMode login_mode = get_login_mode();
    if (!account)
    {
        return false;
    }

    const char *login_id = nullptr;
    if (login_mode == LOGIN_MODE_CUST_ID)
    {
        if (!cust_id)
        {
            return false;
        }
        login_id = cust_id;
        strncpy(account->login_id, cust_id, sizeof(account->login_id) - 1);
    }
    else
    {
        if (!fund_account_id)
        {
            return false;
        }
        login_id = fund_account_id;
        strncpy(account->login_id, fund_account_id, sizeof(account->login_id) - 1);
    }

    // 根据ATP返回的login_id查找完整的账户信息
    AccountInfo *account_info = get_account_by_login_id_internal(account->login_id);
    if (account_info)
    {
        *account = *account_info; // 使用系统配置的完整账户信息
        return true;
    }
    else
    {
        TRADE_LOG_WARN("Failed to find account info for login_id: {}", account->login_id);

        // 回退方案：使用ATP返回的部分信息
        memset(account, 0, sizeof(AccountInfo));
        strncpy(account->login_id, login_id, sizeof(account->login_id) - 1);
        strncpy(account->cust_id, cust_id, sizeof(account->cust_id) - 1);
        if (fund_account_id && strlen(fund_account_id) > 0)
        {
            strncpy(account->fund_account_id, fund_account_id, sizeof(account->fund_account_id) - 1);
        }
        return false;
    }
}

TradeOrderData AtpTradeHandler::_convert_atp_order_query_to_trade_order(const ATPRspCashOrderQueryResultMsg &msg,
                                                                        const ATPRspErrorInfo &error_info,
                                                                        const bool is_last)
{
    TradeOrderData data = {};

    // 填充基本信息
    data.timestamp = get_current_timestamp();
    data.order_id = msg.GetClOrdNo();
    data.batch_id = msg.GetBatchClOrdNo();
    strncpy(data.current_account_id, msg.GetAccountId(), sizeof(data.current_account_id) - 1);
    data.current_market = _map_atp_market_to_market_type(msg.GetMarketId());
    data.is_last = is_last;

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &data.account);

    // 填充证券信息
    strncpy(data.security_info.code, msg.GetSecurityId(), sizeof(data.security_info.code) - 1);
    if (msg.GetSecuritySymbol() && strlen(msg.GetSecuritySymbol()) > 0)
    {
        strncpy(data.security_info.name, msg.GetSecuritySymbol(), sizeof(data.security_info.name) - 1);
    }
    data.security_info.market = _map_atp_market_to_market_type(msg.GetMarketId());

    // 填充订单信息
    data.side = _map_atp_side_to_order_side(msg.GetSide());
    data.sign = _map_atp_order_sign_to_order_sign(msg.GetOrdSign());
    data.order_type = _map_atp_order_type_to_order_type(msg.GetOrdType());
    data.price = msg.GetOrderPrice();
    data.qty = msg.GetOrderQty();
    data.leaves_qty = msg.GetLeavesQty();
    data.cum_qty = msg.GetCumQty();
    data.avg_price = msg.GetExecPrice();
    data.cum_amt = msg.GetOrderCumTransactionAmt();
    data.status = _map_atp_status_to_order_status(msg.GetOrdStatus());

    if (msg.GetOrderStatusInfo() && strlen(msg.GetOrderStatusInfo()) > 0)
    {
        strncpy(data.status_msg, msg.GetOrderStatusInfo(), sizeof(data.status_msg) - 1);
    }
    data.order_time = msg.GetTransactTime();
    data.update_time = get_current_timestamp();

    // 使用统一的错误处理函数
    _fill_error_info(error_info, data.error_code, data.error_msg, sizeof(data.error_msg));

    if (msg.GetOrderId() && strlen(msg.GetOrderId()) > 0)
    {
        strncpy(data.exchange_order_id, msg.GetOrderId(), sizeof(data.exchange_order_id) - 1);
    }

    data.business_type = msg.GetBusinessType();
    data.opu_id = msg.GetOpuId();

    return data;
}

TradeFillData AtpTradeHandler::_convert_atp_trade_query_to_trade_fill(const ATPRspCashTradeOrderQueryResultMsg &msg,
                                                                      const ATPRspErrorInfo &error_info,
                                                                      const bool is_last)
{
    TradeFillData data = {};

    data.timestamp = get_current_timestamp();
    data.order_id = msg.GetClOrdNo();
    data.batch_id = msg.GetBatchClOrdNo();
    strncpy(data.current_account_id, msg.GetAccountId(), sizeof(data.current_account_id) - 1);
    data.current_market = _map_atp_market_to_market_type(msg.GetMarketId());
    data.is_last = is_last;

    if (msg.GetExecId() && strlen(msg.GetExecId()) > 0)
    {
        strncpy(data.trade_id, msg.GetExecId(), sizeof(data.trade_id) - 1);
    }

    if (msg.GetOrderId() && strlen(msg.GetOrderId()) > 0)
    {
        strncpy(data.exchange_order_id, msg.GetOrderId(), sizeof(data.exchange_order_id) - 1);
    }

    // 使用辅助函数填充账户信息
    _fill_account_info_from_atp(msg.GetCustId(), msg.GetFundAccountId(), &data.account);

    // 填充证券信息
    strncpy(data.security_info.code, msg.GetSecurityId(), sizeof(data.security_info.code) - 1);
    if (msg.GetSecuritySymbol() && strlen(msg.GetSecuritySymbol()) > 0)
    {
        strncpy(data.security_info.name, msg.GetSecuritySymbol(), sizeof(data.security_info.name) - 1);
    }
    data.security_info.market = _map_atp_market_to_market_type(msg.GetMarketId());

    data.side = _map_atp_side_to_order_side(msg.GetSide());
    data.order_type = _map_atp_order_type_to_order_type(msg.GetOrdType());
    data.price = msg.GetLastPx();
    data.qty = msg.GetLastQty();
    data.value = msg.GetTotalValueTraded();
    data.trade_time = msg.GetTransactTime();
    data.fee = msg.GetFee();

    if (msg.GetCurrency() && strlen(msg.GetCurrency()) > 0)
    {
        strncpy(data.currency, msg.GetCurrency(), sizeof(data.currency) - 1);
    }
    else
    {
        strncpy(data.currency, "CNY", sizeof(data.currency) - 1);
    }

    data.business_type = msg.GetBusinessType();

    // 使用统一的错误处理函数
    _fill_error_info(error_info, data.error_code, data.error_msg, sizeof(data.error_msg));

    return data;
}

void AtpTradeHandler::_handle_atp_error(const ATPRspErrorInfo &error_info, int64_t request_id)
{
    std::string error_msg = error_info.GetErrorMsg() ? error_info.GetErrorMsg() : "Unknown error";

    TRADE_LOG_ERROR("ATP error: {}, request_id={}", error_msg, request_id);

    set_error(error_msg);
    on_error_received(error_info.error_id, error_msg.c_str(), request_id);
}

FundTransferData AtpTradeHandler::_convert_atp_fund_transfer_to_data(
    const atp::quant_api::ATPRspCashExternalInsTETransFundRespMsg &msg,
    const atp::quant_api::ATPRspErrorInfo &error_info)
{

    FundTransferData data = {};

    // 填充基本信息
    data.timestamp = get_current_timestamp();

    // 填充账户信息
    if (get_login_mode() == LOGIN_MODE_CUST_ID)
    {
        strncpy(data.login_id, msg.GetCustId(), sizeof(data.login_id) - 1);
    }
    else
    {
        strncpy(data.login_id, msg.GetFundAccountId(), sizeof(data.login_id) - 1);
    }
    strncpy(data.cust_id, msg.GetCustId(), sizeof(data.cust_id) - 1);
    strncpy(data.fund_account_id, msg.GetFundAccountId(), sizeof(data.fund_account_id) - 1);
    strncpy(data.branch_id, msg.GetBranchId(), sizeof(data.branch_id) - 1);

    // 使用统一的错误处理函数
    _fill_error_info(error_info, data.error_code, data.error_msg, sizeof(data.error_msg));

    return data;
}

// ===== 错误处理辅助方法实现 =====
void AtpTradeHandler::_fill_error_info(const atp::quant_api::ATPRspErrorInfo &error_info,
                                       int &error_code,
                                       char *error_msg,
                                       size_t error_msg_size)
{
    if (!error_msg || error_msg_size == 0)
    {
        return;
    }

    // 设置错误代码
    error_code = error_info.error_id;

    // 设置错误消息
    if (error_info.GetErrorMsg() && strlen(error_info.GetErrorMsg()) > 0)
    {
        strncpy(error_msg, error_info.GetErrorMsg(), error_msg_size - 1);
        error_msg[error_msg_size - 1] = '\0'; // 确保字符串结束
    }
    else
    {
        error_msg[0] = '\0'; // 清空错误消息
    }
}
NS_FT_END

extern "C"
{
    EXPORT_FLAG ITradeHandler *create_trade_handler(void)
    {
        return reinterpret_cast<ITradeHandler *>(new ft::AtpTradeHandler());
    }

    EXPORT_FLAG void delete_trade_handler(ITradeHandler *&handler)
    {
        if (handler != nullptr)
        {
            delete handler;
            handler = nullptr;
        }
    }

    EXPORT_FLAG void set_logger_state(void *logger_state)
    {
        if (!logger_state)
        {
            return;
        }

        try
        {
            auto *state_ptr = static_cast<ft::LoggerState *>(logger_state);
            ft::Logger::set_logger_state(state_ptr);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to set logger state: " << e.what() << std::endl;
        }
    }

    EXPORT_FLAG void reset_logger_state()
    {
        ft::Logger::reset_logger_state();
    }
}
