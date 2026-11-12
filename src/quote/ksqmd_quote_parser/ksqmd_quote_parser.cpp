#include "ksqmd_quote_parser.h"
#include "KsHSInterfaceEx.h"
#include "SseExanicStruct.h"
#include "SzseExanicStruct.h"
#include "xml_config_ex.h"
#include "quote/quote_interface.h" // for get_current_timestamp
#include "utils/string.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include "logger/logger.hpp"
#include "quote/quote_parser.h"
#include "utils/time.h"

using namespace istone;
using namespace istone::base;
using namespace istone_hs;

NS_FT_BEGIN

// 静态实例指针
KsqmdQuoteParser *KsqmdQuoteParser::_instance = nullptr;

KsqmdQuoteParser::KsqmdQuoteParser()
    : BaseQuoteParser(), _xml_config(nullptr) // 先设为空，在需要时再初始化
      ,
      _nic_count(0), _queue_count(0)
{
    try
    {
        // 延迟初始化这些对象，避免构造函数中的异常
        _meta_info = std::make_unique<istone_hs::MetaInfo>();
        _auth_info = std::make_unique<istone_hs::AuthInfo>();
        _nic_list = std::make_unique<istone_hs::CacheAdvanceNicItem[]>(8);
        _queue_list = std::make_unique<istone_hs::CacheAdvanceQueueItem[]>(16);

        // 设置静态实例指针
        _instance = this;
    }
    catch (const std::exception &e)
    {
        QUOTE_LOG_ERROR("Exception in KsqmdQuoteParser constructor: {}", e.what());
        // 不要在构造函数中抛出异常，设置错误状态
        _instance = nullptr;
    }
}

KsqmdQuoteParser::~KsqmdQuoteParser()
{
    // 异步延迟清空，避免阻塞析构
    std::thread cleanup_thread([this]()
                               {
        // 延迟1秒，确保所有回调都已完成
        std::this_thread::sleep_for(std::chrono::seconds(1));
        _instance = nullptr;
        QUOTE_LOG_DEBUG("KsqmdQuoteParser instance cleared after delay"); });
    cleanup_thread.detach();
}

// ===== 实现基类的核心虚函数 =====
int KsqmdQuoteParser::init_impl(const QuoteConfig *config)
{
    if (is_mock_mode())
    {
        QUOTE_LOG_INFO("Running in Mock mode, skipping XML configuration");
        return 0;
    }

    if (!config->extended_config_file || strlen(config->extended_config_file) == 0)
    {
        set_error("Extended config file required for non-mock mode");
        return -1;
    }

    return _initialize_xml_config(config->extended_config_file);
}

int KsqmdQuoteParser::start_impl()
{
    return 0;
}

int KsqmdQuoteParser::connect_impl()
{
    // 启动KSQMD - 确保参数类型匹配API定义
    bool ret = istone_start_ex(
        _meta_info.get(),      // MetaInfo*
        _auth_info.get(),      // AuthInfo*
        _auth_status_callback, // istone::comm::STATUSCALLBACK
        _nic_list.get(),       // CacheAdvanceNicItem*
        _nic_count,            // uint8_t
        _queue_list.get(),     // CacheAdvanceQueueItem*
        _queue_count           // uint8_t
    );

    if (ret)
    {
        return 0;
    }
    else
    {
        set_error("Failed to start KSQMD connection");
        return -1;
    }
}

int KsqmdQuoteParser::disconnect_impl()
{
    bool ret = istone_stop_ex(); // 无参数，返回bool
    if (ret)
    {
        return 0;
    }
    else
    {
        set_error("Failed to stop KSQMD connection");
        return -1;
    }
}

int KsqmdQuoteParser::stop_impl()
{
    QUOTE_LOG_INFO("KsqmdQuoteParser stopped");
    return 0;
}

void KsqmdQuoteParser::release_impl()
{
    if (_running.load())
    {
        stop();
    }
    _xml_config = nullptr;
    _config_file.clear();
}

// ===== KSQMD特定的回调函数 =====
void KsqmdQuoteParser::_data_receiver_callback(void *data, uint32_t queue_id)
{
    if (_instance)
    {
        // 如果收到数据，则认为连接成功
        _instance->_connected.store(true);  
        // 更新最近数据时间
        _instance->_process_data(data, queue_id);
    }
}

void KsqmdQuoteParser::_auth_status_callback(istone::comm::STATUS status, const char *msg)
{
    if (!_instance)
    {
        return;
    }

    // 映射 KSQMD 状态到系统状态
    Status system_status = _map_ksqmd_status_to_system_status(status);
    const char *status_str = _get_ksqmd_status_string(status);

    QUOTE_LOG_INFO("KSQMD status callback: {} - {}", status_str, msg ? msg : "");

    // 记录详细的认证状态日志和处理错误状态
    switch (status)
    {
    case istone::comm::STATUS_OK:
    case istone::comm::STATUS_CONNECTED:
    case istone::comm::STATUS_LOGIN_SUCC:
        _instance->_connected.store(true);
        break;
    case istone::comm::STATUS_RETRY_CONNECTING:
    case istone::comm::STATUS_LOGINING:
        // 登陆中，不需要特殊处理
        QUOTE_LOG_INFO("KSQMD logging in...");
        break;
    case istone::comm::STATUS_DISCONNECTED:
    case istone::comm::STATUS_LOGIN_FAIL:
    case istone::comm::STATUS_LOGOUT_FAIL:
    case istone::comm::STATUS_CFG_AUTH:
    case istone::comm::STATUS_NAMEORPWD_IS_NULL:
    case istone::comm::STATUS_OTHER:
        _instance->_connected.store(false);
        break;
    default:
        QUOTE_LOG_WARN("Unknown KSQMD status: {}, treating as error", (int)status);
        break;
    }

    // 更新系统状态
    _instance->_current_status.store(system_status);

    // 通知基类状态变化
    _instance->on_quote_status_received(system_status, msg);
}

void KsqmdQuoteParser::_process_data(void *data, uint32_t queue_id)
{
    (void)queue_id;
    if (!data)
    {
        return;
    }

    char msg_type = *(char *)data;

    // 根据消息类型判断是上交所还是深交所数据
    if (msg_type >= 'a' && msg_type <= 'z')
    {
        // 上交所数据 (小写字母)
        _parse_sse_data(msg_type, data);
    }
    else if (msg_type >= 'A' && msg_type <= 'Z')
    {
        // 深交所数据 (大写字母)
        _parse_szse_data(msg_type, data);
    }
}

void KsqmdQuoteParser::_parse_sse_data(char msg_type, void *data)
{
    MarketType market = MARKET_SSE;
    FTDataType data_type;
    const char *code = nullptr;

    switch (msg_type)
    {
    case 'a': // 逐笔委托
    {
        data_type = DATA_TYPE_ENTRUST;
        const SseEntrust *entrust = static_cast<const SseEntrust *>(data);
        code = entrust->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            EntrustData entrust_data = _convert_sse_entrust(data);
            on_entrust_data_received(entrust_data);
        }
    }
    break;
    case 'b': // 逐笔成交
    {
        data_type = DATA_TYPE_TRADE;
        const SseTrade *trade = static_cast<const SseTrade *>(data);
        code = trade->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            TradeData trade_data = _convert_sse_trade(data);
            on_trade_data_received(trade_data);
        }
    }
    break;
    case 'c': // 指数行情
    {
        data_type = DATA_TYPE_INDEX;
        const SseIndex *index = static_cast<const SseIndex *>(data);
        code = index->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            IndexData index_data = _convert_sse_index(data);
            on_index_data_received(index_data);
        }
    }
    break;
    case 'd': // 快照行情(含十档)
    case 'e': // 快照行情(不含十档)
    {
        data_type = DATA_TYPE_DEPTH;
        const SseDepth *depth = static_cast<const SseDepth *>(data);
        code = depth->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            DepthData depth_data = _convert_sse_depth(data, msg_type == 'd');
            on_depth_data_received(depth_data);
        }
    }
    break;
    case 'f': // 50档行情
    {
        data_type = DATA_TYPE_DEPTH_FIFTY;
        const SseDepthFifty *depth_fifty = static_cast<const SseDepthFifty *>(data);
        code = depth_fifty->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            DepthFiftyData depth_fifty_data = _convert_sse_depth_fifty(data);
            on_depth_fifty_data_received(depth_fifty_data);
        }
    }
    break;
    default:
        break;
    }
}

void KsqmdQuoteParser::_parse_szse_data(char msg_type, void *data)
{
    MarketType market = MARKET_SZSE;
    FTDataType data_type;
    const char *code = nullptr;

    switch (msg_type)
    {
    case 'B': // 指数行情
    {
        data_type = DATA_TYPE_INDEX;
        const SzseIndex *index = static_cast<const SzseIndex *>(data);
        code = index->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            IndexData index_data = _convert_szse_index(data);
            on_index_data_received(index_data);
        }
    }
    break;
    case 'C': // 逐笔委托
    {
        data_type = DATA_TYPE_ENTRUST;
        const SzseEntrust *entrust = static_cast<const SzseEntrust *>(data);
        code = entrust->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            EntrustData entrust_data = _convert_szse_entrust(data);
            on_entrust_data_received(entrust_data);
        }
    }
    break;
    case 'D': // 逐笔成交
    {
        data_type = DATA_TYPE_TRADE;
        const SzseTrade *trade = static_cast<const SzseTrade *>(data);
        code = trade->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            TradeData trade_data = _convert_szse_trade(data);
            on_trade_data_received(trade_data);
        }
    }
    break;
    case 'E': // 快照行情
    {
        data_type = DATA_TYPE_DEPTH;
        const SzseDepth *depth = static_cast<const SzseDepth *>(data);
        code = depth->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            DepthData depth_data = _convert_szse_depth(data);
            on_depth_data_received(depth_data);
        }
    }
    break;
    case 'F': // 50档行情
    {
        data_type = DATA_TYPE_DEPTH_FIFTY;
        const SzseDepthFifty *depth_fifty = static_cast<const SzseDepthFifty *>(data);
        code = depth_fifty->SecurityID;
        if (should_process_data(market, data_type, code))
        {
            DepthFiftyData depth_fifty_data = _convert_szse_depth_fifty(data);
            on_depth_fifty_data_received(depth_fifty_data);
        }
    }
    break;
    default:
        break;
    }
}

// ===== 数据转换函数实现 =====
IndexData KsqmdQuoteParser::_convert_sse_index(const void *sse_index)
{
    const SseIndex *index = static_cast<const SseIndex *>(sse_index);
    IndexData data = {};

    data.security_info.market = MARKET_SSE;
    data.timestamp = get_current_timestamp();
    data.update_time = index->UpdateTime; // 添加缺失的更新时间

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   index->SecurityID, sizeof(index->SecurityID));

    data.last_index = static_cast<double>(index->LastIndex) / 100000.0;
    data.open_index = static_cast<double>(index->OpenIndex) / 100000.0;
    data.high_index = static_cast<double>(index->HighIndex) / 100000.0;
    data.low_index = static_cast<double>(index->LowIndex) / 100000.0;
    data.pre_close_index = static_cast<double>(index->PreCloseIndex) / 100000.0;
    data.close_index = static_cast<double>(index->CloseIndex) / 100000.0; // 添加收盘指数

    data.total_volume = transformSseQty(index->TotalVolumeTrade);
    data.total_value = transformSseIndexMoney(index->Turnover);

    // 添加缺失的序列号（可以用TradeTime作为序列标识）
    data.sequence = index->TradeTime;

    return data;
}

IndexData KsqmdQuoteParser::_convert_szse_index(const void *szse_index)
{
    const SzseIndex *index = static_cast<const SzseIndex *>(szse_index);
    IndexData data = {};

    data.security_info.market = MARKET_SZSE;
    data.timestamp = get_current_timestamp();
    data.update_time = static_cast<uint32_t>(index->OrigTime); // 使用OrigTime作为更新时间

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   index->SecurityID, sizeof(index->SecurityID));

    // 解析交易阶段
    std::string trading_phase(index->TradingPhaseCode, sizeof(index->TradingPhaseCode));
    data.trading_phase = parse_szse_trading_phase(trading_phase.c_str());

    data.last_index = transformSzseIndex_Nx_6(index->LastIndex);
    data.open_index = transformSzseIndex_Nx_6(index->OpenIndex);
    data.high_index = transformSzseIndex_Nx_6(index->HighIndex);
    data.low_index = transformSzseIndex_Nx_6(index->LowIndex);
    data.pre_close_index = transformSzseIndex_Nx_6(index->PreCloseIndex);
    data.close_index = transformSzseIndex_Nx_6(index->CloseIndex);

    data.total_volume = transformSzseQty(index->TotalVolumeTrade);
    data.total_value = static_cast<double>(index->TotalValueTrade) / SZSE_MONEY_DECIMAL;
    data.num_trades = transformSzseQty(index->NumTrades);

    // 使用OrigTime作为序列号
    data.sequence = static_cast<uint32_t>(index->OrigTime);

    return data;
}

DepthData KsqmdQuoteParser::_convert_sse_depth(const void *sse_depth, bool has_price)
{
    const SseDepth *depth = static_cast<const SseDepth *>(sse_depth);
    DepthData data = {};

    // 基本信息
    data.security_info.market = MARKET_SSE;
    data.timestamp = get_current_timestamp();
    data.update_time = depth->UpdateTime;

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   depth->SecurityID, sizeof(depth->SecurityID));

    // 解析交易阶段
    std::string trading_phase(depth->TradingPhaseCode, sizeof(depth->TradingPhaseCode));
    data.trading_phase = parse_sse_trading_phase(trading_phase.c_str());

    // 合约状态
    safe_copy_code(data.instrument_status, sizeof(data.instrument_status),
                   depth->InstruStatus, sizeof(depth->InstruStatus));

    // 价格信息
    data.last_price = transformSsePrice(depth->LastPx);
    data.pre_close_price = transformSsePrice(depth->PreClosePx);
    data.open_price = transformSsePrice(depth->OpenPx);
    data.high_price = transformSsePrice(depth->HighPx);
    data.low_price = transformSsePrice(depth->LowPx);
    data.close_price = transformSsePrice(depth->ClosePx);
    data.upper_limit_price = transformSsePrice(depth->UpperLimitPrice);
    data.lower_limit_price = transformSsePrice(depth->LowerLimitPrice);
    data.avg_price = transformSsePrice(depth->WeightedAvgPx); // 债券加权平均价格

    // 成交信息
    data.total_volume = transformSseQty(depth->TotalVolumeTrade);
    data.total_value = transformSseMoney(depth->TotalValueTrade);
    data.num_trades = depth->NumTrades;

    // 委托信息
    data.total_bid_qty = transformSseQty(depth->TotalBidQty);
    data.total_ask_qty = transformSseQty(depth->TotalOfferQty);
    data.weighted_avg_bid_price = transformSsePrice(depth->WeightedAvgBidPx);
    data.weighted_avg_ask_price = transformSsePrice(depth->WeightedAvgOfferPx);
    data.total_bid_number = depth->TotalBidNumber;
    data.total_ask_number = depth->TotalOfferNumber;

    // 十档买卖盘信息
    if (has_price)
    {
        for (int i = 0; i < 10; ++i)
        {
            data.bid_price[i] = transformSsePrice(depth->BidPrice[i]);
            data.bid_qty[i] = transformSseQty(depth->BidQty[i]);
            data.ask_price[i] = transformSsePrice(depth->AskPrice[i]);
            data.ask_qty[i] = transformSseQty(depth->AskQty[i]);
        }
    }

    // 期权特有字段 (上交所目前没有这些字段，设为0)
    data.total_position = 0;
    data.reference_price = 0.0;

    // 基金特有字段 (上交所目前没有IOPV字段，设为0)
    data.iopv = 0.0;
    data.pre_iopv = 0.0;

    // 频道号和序列号
    data.channel_no = 0;               // 上交所没有频道号字段
    data.sequence = depth->UpdateTime; // 使用更新时间作为序列号

    return data;
}

DepthData KsqmdQuoteParser::_convert_szse_depth(const void *szse_depth)
{
    const SzseDepth *depth = static_cast<const SzseDepth *>(szse_depth);
    DepthData data = {};

    data.security_info.market = MARKET_SZSE;
    data.timestamp = get_current_timestamp();
    data.update_time = static_cast<uint32_t>(depth->OrigTime);

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   depth->SecurityID, sizeof(depth->SecurityID));

    // 解析交易阶段
    std::string trading_phase(depth->TradingPhaseCode, sizeof(depth->TradingPhaseCode));
    data.trading_phase = parse_szse_trading_phase(trading_phase.c_str());

    // 解析行情类别到证券类型
    std::string md_stream(depth->MDStreamID, sizeof(depth->MDStreamID));
    safe_copy_code(data.security_info.security_type, sizeof(data.security_info.security_type),
                   md_stream.c_str(), md_stream.length());

    // 价格信息
    data.last_price = transformSzse_Nx_6(depth->LastPrice);
    data.open_price = transformSzse_Nx_6(depth->OpenPrice);
    data.high_price = transformSzse_Nx_6(depth->HighPrice);
    data.low_price = transformSzse_Nx_6(depth->LowPrice);
    data.pre_close_price = transformSzse_Nx_4(depth->PrevClosePx);
    data.upper_limit_price = transformSzse_Nx_6(depth->UpperLimitPrice);
    data.lower_limit_price = transformSzse_Nx_6(depth->LowerLimitPrice);
    data.avg_price = transformSzse_Nx_6(depth->AvgPrice);

    // 成交信息
    data.total_volume = transformSzseQty(depth->TotalVolumeTrade);
    data.total_value = static_cast<double>(depth->TotalValueTrade) / SZSE_MONEY_DECIMAL;
    data.num_trades = depth->NumTrades;

    // 委托信息
    data.total_bid_qty = transformSzseQty(depth->TotalBidQty);
    data.total_ask_qty = transformSzseQty(depth->TotalAskQty);
    data.weighted_avg_bid_price = transformSzse_Nx_6(depth->MaBidPrice);
    data.weighted_avg_ask_price = transformSzse_Nx_6(depth->MaAskPrice);

    // 十档买卖盘
    for (int i = 0; i < 10; ++i)
    {
        data.bid_price[i] = transformSzse_Nx_6(depth->BidPrice[i]);
        data.bid_qty[i] = transformSzseQty(depth->BidQty[i]);
        data.ask_price[i] = transformSzse_Nx_6(depth->AskPrice[i]);
        data.ask_qty[i] = transformSzseQty(depth->AskQty[i]);
    }

    // 期权特有字段
    data.total_position = transformSzseQty(depth->InstrQtyNum);
    data.reference_price = transformSzse_Nx_6(depth->BrkRefPrice);

    // 基金特有字段
    data.iopv = transformSzse_Nx_6(depth->Iopv);
    data.pre_iopv = transformSzse_Nx_6(depth->PreIopv);

    data.channel_no = depth->ChannelNo;
    data.sequence = static_cast<uint32_t>(depth->OrigTime);

    return data;
}

DepthFiftyData KsqmdQuoteParser::_convert_sse_depth_fifty(const void *sse_depth_fifty)
{
    const SseDepthFifty *depth_fifty = static_cast<const SseDepthFifty *>(sse_depth_fifty);
    DepthFiftyData data = {};

    data.security_info.market = MARKET_SSE;
    data.timestamp = get_current_timestamp();
    data.update_time = depth_fifty->UpdateTime; // 添加更新时间

    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   depth_fifty->SecurityID, sizeof(depth_fifty->SecurityID));

    data.bid_price = transformSsePrice(depth_fifty->BidOrderPrice);
    data.bid_qty = transformSseQty(depth_fifty->BidOrderQty);
    data.ask_price = transformSsePrice(depth_fifty->AskOrderPrice);
    data.ask_qty = transformSseQty(depth_fifty->AskOrderQty);

    data.bid_num_orders = depth_fifty->BidNumOfOrders;
    data.ask_num_orders = depth_fifty->AskNumOfOrders;
    data.bid_show_orders = depth_fifty->BidNoOrders;
    data.ask_show_orders = depth_fifty->AskNoOrders;

    // 复制委托队列数据
    for (int i = 0; i < 100; ++i)
    {
        data.orders[i] = depth_fifty->Orders[i];
    }

    // 生成序列号（用更新时间作为序列标识）
    data.sequence = depth_fifty->UpdateTime;

    return data;
}

DepthFiftyData KsqmdQuoteParser::_convert_szse_depth_fifty(const void *szse_depth_fifty)
{
    const SzseDepthFifty *depth_fifty = static_cast<const SzseDepthFifty *>(szse_depth_fifty);
    DepthFiftyData data = {};

    data.security_info.market = MARKET_SZSE;
    data.timestamp = get_current_timestamp();
    data.update_time = static_cast<uint32_t>(depth_fifty->OrigTime);

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   depth_fifty->SecurityID, sizeof(depth_fifty->SecurityID));

    // 解析行情类别到证券类型
    std::string md_stream(depth_fifty->MDStreamID, sizeof(depth_fifty->MDStreamID));
    safe_copy_code(data.security_info.security_type, sizeof(data.security_info.security_type),
                   md_stream.c_str(), md_stream.length());

    data.bid_price = transformSzse_Nx_6(depth_fifty->BidOrderPrice);
    data.bid_qty = transformSzseQty(depth_fifty->BidOrderQty);
    data.ask_price = transformSzse_Nx_6(depth_fifty->AskOrderPrice);
    data.ask_qty = transformSzseQty(depth_fifty->AskOrderQty);

    data.bid_num_orders = depth_fifty->BidNumOfOrders;
    data.ask_num_orders = depth_fifty->AskNumOfOrders;
    data.bid_show_orders = depth_fifty->BidNoOrders;
    data.ask_show_orders = depth_fifty->AskNoOrders;

    // 复制委托队列数据
    for (int i = 0; i < 100; ++i)
    {
        data.orders[i] = depth_fifty->Orders[i];
    }

    data.sequence = static_cast<uint32_t>(depth_fifty->OrigTime);

    return data;
}

EntrustData KsqmdQuoteParser::_convert_sse_entrust(const void *sse_entrust)
{
    const SseEntrust *entrust = static_cast<const SseEntrust *>(sse_entrust);
    EntrustData data = {};

    data.security_info.market = MARKET_SSE;
    data.timestamp = get_current_timestamp();
    data.transact_time = entrust->OrderTime; // 委托时间

    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   entrust->SecurityID, sizeof(entrust->SecurityID));

    data.order_type = entrust->OrderType;
    data.side = entrust->OrderBSFlag;
    data.bs_flag = entrust->OrderBSFlag; // 兼容字段

    data.order_no = entrust->OrderNO;
    data.order_index = entrust->OrderIndex;
    data.price = transformSsePrice(entrust->OrderPrice);
    data.qty = transformSseQty(entrust->Balance); // 这里应该是委托数量，不是剩余数量
    data.balance = transformSseQty(entrust->Balance);

    data.channel_no = static_cast<uint16_t>(entrust->OrderChannel);
    data.biz_index = entrust->BizIndex;
    data.sequence = entrust->OrderIndex; // 使用委托序号作为序列号

    return data;
}

EntrustData KsqmdQuoteParser::_convert_szse_entrust(const void *szse_entrust)
{
    const SzseEntrust *entrust = static_cast<const SzseEntrust *>(szse_entrust);
    EntrustData data = {};

    data.security_info.market = MARKET_SZSE;
    data.timestamp = get_current_timestamp();
    data.transact_time = entrust->TransactTime;

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   entrust->SecurityID, sizeof(entrust->SecurityID));

    // 解析行情类别到证券类型
    std::string md_stream(entrust->MDStreamID, sizeof(entrust->MDStreamID));
    safe_copy_code(data.security_info.security_type, sizeof(data.security_info.security_type),
                   md_stream.c_str(), md_stream.length());

    data.order_type = entrust->OrdType;
    data.side = entrust->Side;
    data.bs_flag = entrust->Side; // 兼容字段

    data.order_no = 0;    // SZSE没有直接的OrderNO
    data.order_index = 0; // SZSE没有直接的OrderIndex
    data.price = static_cast<double>(entrust->Price) / SZSE_PRICE_DECIMAL_4;
    data.qty = transformSzseQty(entrust->OrderQty);
    data.balance = transformSzseQty(entrust->OrderQty); // 深交所委托数据中没有剩余量，用委托量代替

    data.channel_no = entrust->ChannelNo;
    data.biz_index = 0; // SZSE没有BizIndex字段
    data.appl_seq_num = entrust->Applseqnum;
    data.sequence = static_cast<uint32_t>(entrust->Applseqnum);

    return data;
}

TradeData KsqmdQuoteParser::_convert_sse_trade(const void *sse_trade)
{
    const SseTrade *trade = static_cast<const SseTrade *>(sse_trade);
    TradeData data = {};

    data.security_info.market = MARKET_SSE;
    data.timestamp = get_current_timestamp();
    data.transact_time = trade->TradeTime;

    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   trade->SecurityID, sizeof(trade->SecurityID));

    data.trade_id = 0; // SSE没有直接的TradeID，可以用TradeIndex
    data.trade_index = trade->TradeIndex;
    data.price = transformSsePrice(trade->TradePrice);
    data.qty = transformSseQty(trade->TradeQty);
    data.value = transformSseMoney(trade->TradeMoney);

    data.bid_order_no = trade->TradeBuyNo;
    data.ask_order_no = trade->TradeSellNo;
    data.exec_type = 'F'; // 成交
    data.trade_flag = trade->TradeBSFlag;

    data.channel_no = static_cast<uint16_t>(trade->TradeChannel);
    data.biz_index = trade->BizIndex;
    data.sequence = trade->TradeIndex; // 使用成交序号作为序列号

    return data;
}

TradeData KsqmdQuoteParser::_convert_szse_trade(const void *szse_trade)
{
    const SzseTrade *trade = static_cast<const SzseTrade *>(szse_trade);
    TradeData data = {};

    data.security_info.market = MARKET_SZSE;
    data.timestamp = get_current_timestamp();
    data.transact_time = trade->TransactTime;

    // 安全拷贝证券代码
    safe_copy_code(data.security_info.code, sizeof(data.security_info.code),
                   trade->SecurityID, sizeof(trade->SecurityID));

    // 解析行情类别到证券类型
    std::string md_stream(trade->MDStreamID, sizeof(trade->MDStreamID));
    safe_copy_code(data.security_info.security_type, sizeof(data.security_info.security_type),
                   md_stream.c_str(), md_stream.length());

    data.trade_id = 0;    // SZSE没有直接的TradeID
    data.trade_index = 0; // SZSE没有TradeIndex
    data.price = static_cast<double>(trade->TradePrice) / SZSE_PRICE_DECIMAL_4;
    data.qty = transformSzseQty(trade->TradeQty);
    data.value = (data.price * data.qty); // 计算成交金额

    data.bid_order_no = 0; // SZSE没有直接的订单号
    data.ask_order_no = 0;
    data.bid_appl_seq_num = trade->BidApplSeqNum;
    data.ask_appl_seq_num = trade->OfferApplSeqNum;

    data.exec_type = trade->ExecType;
    data.trade_flag = 0; // SZSE没有交易标志

    data.channel_no = trade->ChannelNo;
    data.biz_index = 0; // SZSE没有BizIndex字段
    data.appl_seq_num = trade->Applseqnum;
    data.sequence = static_cast<uint32_t>(trade->Applseqnum);

    return data;
}

// ===== 配置和辅助方法 =====
int KsqmdQuoteParser::_initialize_xml_config(const char *config_file)
{
    _config_file = config_file ? config_file : "api_config_ex.xml";
    QUOTE_LOG_INFO("Loading KSQMD configuration from: {}", config_file);

    if (!_xml_config)
    {
        _xml_config = XMLConfigEx::instance();
    }

    if (!_xml_config)
    {
        set_error("Failed to get XMLConfigEx instance");
        return -1;
    }

    if (!_xml_config->LoadXML(_config_file.c_str()))
    {
        set_error("Failed to load XML configuration file: " + _config_file);
        return -1;
    }

    // 解析meta信息
    if (!_xml_config->meta_node_)
    {
        set_error("Meta node not found in configuration");
        return -1;
    }

    _meta_info->version = const_cast<char *>(_xml_config->meta_node_->Attribute("version"));

    auto auth_elem = _xml_config->meta_node_->FirstChildElement("Auth");
    if (!auth_elem)
    {
        set_error("Auth element not found in meta node");
        return -1;
    }
    _meta_info->authMetaInfo = const_cast<char *>(auth_elem->GetText());

    auto multicast_elem = _xml_config->meta_node_->FirstChildElement("Multicast");
    if (!multicast_elem)
    {
        set_error("Multicast element not found in meta node");
        return -1;
    }
    _meta_info->multiMetaInfo = const_cast<char *>(multicast_elem->GetText());

    // 解析鉴权信息
    if (!_xml_config->auth_node_)
    {
        set_error("AuthInfo node not found in configuration");
        return -1;
    }

    _auth_info->user = const_cast<char *>(_xml_config->auth_node_->Attribute("user"));
    _auth_info->passwd = const_cast<char *>(_xml_config->auth_node_->Attribute("passwd"));
    _auth_info->nic = const_cast<char *>(_xml_config->auth_node_->Attribute("nic"));

    QUOTE_LOG_INFO("KSQMD Auth config - user: {}, nic: {}",
                   _auth_info->user ? _auth_info->user : "null",
                   _auth_info->nic ? _auth_info->nic : "null");

    // 解析网卡配置
    _nic_count = 0;
    if (_xml_config->nic_node_)
    {
        auto item = _xml_config->nic_node_->FirstChildElement();
        while (item && _nic_count < 8)
        {
            _nic_list[_nic_count].nicName = const_cast<char *>(item->Attribute("onNic"));
            _nic_list[_nic_count].cpuId = std::stoi(item->Attribute("cpuId"));
            _nic_list[_nic_count].msgTypes = const_cast<char *>(item->Attribute("types"));

            item = item->NextSiblingElement();
            _nic_count++;
        }
    }

    // 解析队列配置
    _queue_count = 0;
    if (_xml_config->queue_node_)
    {
        auto item = _xml_config->queue_node_->FirstChildElement();
        while (item && _queue_count < 16)
        {
            _queue_list[_queue_count].queueId = std::stoi(item->Attribute("id"));
            _queue_list[_queue_count].msgTypes = const_cast<char *>(item->Attribute("types"));
            _queue_list[_queue_count].cpuId = std::stoi(item->Attribute("cpuId"));
            _queue_list[_queue_count].blockSize = std::stoi(item->Attribute("blockSize"));
            _queue_list[_queue_count].dataCallback = _data_receiver_callback;

            item = item->NextSiblingElement();
            _queue_count++;
        }
    }

    QUOTE_LOG_INFO("KSQMD configuration loaded successfully");
    return 0;
}

// ===== 状态映射辅助函数 =====
Status KsqmdQuoteParser::_map_ksqmd_status_to_system_status(istone::comm::STATUS ksqmd_status)
{
    switch (ksqmd_status)
    {
    case istone::comm::STATUS_OK:
    case istone::comm::STATUS_CONNECTED:
    case istone::comm::STATUS_LOGIN_SUCC:
        return STATUS_CONNECTED;

    case istone::comm::STATUS_RETRY_CONNECTING:
    case istone::comm::STATUS_LOGINING:
        return STATUS_CONNECTING;

    case istone::comm::STATUS_DISCONNECTED:
    case istone::comm::STATUS_LOGOUT_SUCC:
        return STATUS_DISCONNECTED;

    default:
        return STATUS_ERROR;
    }
}

const char *KsqmdQuoteParser::_get_ksqmd_status_string(istone::comm::STATUS status)
{
    switch (status)
    {
    case istone::comm::STATUS_OK:
        return "OK";
    case istone::comm::STATUS_CONNECTED:
        return "CONNECTED";
    case istone::comm::STATUS_LOGINING:
        return "LOGINING";
    case istone::comm::STATUS_RETRY_CONNECTING:
        return "RETRY_CONNECTING";
    case istone::comm::STATUS_DISCONNECTED:
        return "DISCONNECTED";
    case istone::comm::STATUS_LOGIN_SUCC:
        return "LOGIN_SUCC";
    case istone::comm::STATUS_LOGIN_FAIL:
        return "LOGIN_FAIL";
    case istone::comm::STATUS_LOGOUT_FAIL:
        return "LOGOUT_FAIL";
    case istone::comm::STATUS_CFG_AUTH:
        return "CFG_AUTH";
    case istone::comm::STATUS_NAMEORPWD_IS_NULL:
        return "NAMEORPWD_IS_NULL";
    case istone::comm::STATUS_OTHER:
        return "OTHER";
    default:
        return "UNKNOWN";
    }
}
NS_FT_END

// C接口工厂函数实现
extern "C"
{
    EXPORT_FLAG IQuoteParser *create_quote_parser(void)
    {
        return reinterpret_cast<IQuoteParser *>(new ft::KsqmdQuoteParser());
    }

    EXPORT_FLAG void delete_quote_parser(IQuoteParser *&parser)
    {
        if (parser != nullptr)
        {
            delete parser;
            parser = nullptr;
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
            // 将void*转换为LoggerState*
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