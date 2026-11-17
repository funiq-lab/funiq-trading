#include "trade/base_trade_handler.h"
#include "logger/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace ft {
// A deterministic in-memory example, not an exchange simulator. No network I/O.
class MockTradeHandler final : public BaseTradeHandler {
    struct Wallet {
        AccountInfo account{};
        double cash = 1000000.0;
        double reserved = 0;
        std::map<std::string, PositionData> positions;
        std::map<int64_t, TradeOrderData> orders;
        std::vector<TradeFillData> fills;
    };
    std::map<std::string, Wallet> wallets_;
    std::mutex state_mutex_;
    int64_t next_order_ = 100001;
    static uint64_t now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    static double reference(const char* code) { return std::string(code) == "600000" ? 10.0 : 15.0; }
    static bool supported(const char* code, MarketType market) {
        return (std::string(code) == "600000" && market == MARKET_SSE) ||
               (std::string(code) == "000001" && market == MARKET_SZSE);
    }
    static const char* account_id(const AccountInfo& a, MarketType m) {
        return m == MARKET_SSE ? a.account_id_sh : a.account_id_sz;
    }
    template<class T> static void error(T& data, int code, const char* msg) {
        data.error_code = code;
        std::snprintf(data.error_msg, sizeof(data.error_msg), "%s", msg);
    }
    template<class T> static void identify(T& d, const AccountInfo& a, MarketType market, int64_t rid) {
        d.account = a;
        d.timestamp = now();
        d.request_id = rid;
        d.current_market = market;
        std::snprintf(d.current_account_id, sizeof(d.current_account_id), "%s", account_id(a, market));
    }
    template<class T, class Emit> static void emit_rows(std::vector<T>& rows, T empty, int code, Emit emit) {
        if (rows.empty()) {
            error(empty, code, "No matching mock data");
            empty.is_last = true;
            emit(empty);
        } else {
            for (size_t i = 0; i < rows.size(); ++i) {
                rows[i].is_last = i + 1 == rows.size();
                emit(rows[i]);
            }
        }
    }
public:
    ~MockTradeHandler() override { release(); }
protected:
    int init_impl(const TradeConfig* config) override {
        // BaseTradeHandler holds its own mutex during init. Use the supplied config.
        for (int i = 0; i < config->account_list.account_count; ++i) {
            Wallet wallet;
            wallet.account = config->account_list.accounts[i];
            wallets_.emplace(wallet.account.login_id, std::move(wallet));
        }
        TRADE_LOG_INFO("MOCK trade plugin ready: {} in-memory account(s), 1000000 CNY initial cash, fixed-price fills", wallets_.size());
        return wallets_.empty() ? -1 : 0;
    }
    int start_impl() override { return 0; }
    int connect_impl() override {
        for (const auto& item : wallets_) set_account_login_status(item.first.c_str(), true);
        return 0;
    }
    int disconnect_impl() override { clear_all_login_status(); return 0; }
    int stop_impl() override { return disconnect_impl(); }
    void release_impl() override { wallets_.clear(); }

    int send_order_impl(const TradeOrderRequest* req, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto& w = wallets_.at(req->account.login_id);
        TradeOrderRspData response{};
        response.account = w.account;
        response.request_id = rid;
        response.timestamp = now();
        response.response_time = response.timestamp;
        response.price = req->price;
        response.qty = req->qty;
        response.side = req->side;
        response.order_type = req->order_type;
        response.batch_id = req->batch_id;
        response.security_info.market = req->market;
        std::snprintf(response.security_info.code, sizeof(response.security_info.code), "%s", req->security_code);
        std::snprintf(response.current_account_id, sizeof(response.current_account_id), "%s", account_id(w.account, req->market));
        const double px = reference(req->security_code);
        if (!supported(req->security_code, req->market) || req->qty == 0 ||
            (req->side != SIDE_BUY && req->side != SIDE_SELL) ||
            (req->order_type != ORDER_TYPE_LIMIT && req->order_type != ORDER_TYPE_MARKET) ||
            (req->order_type == ORDER_TYPE_LIMIT && (!std::isfinite(req->price) || req->price <= 0))) {
            error(response, -100, "Mock supports 600000/SSE and 000001/SZSE, positive limit or market orders");
            on_order_rsp_update(response);
            return 0;
        }
        const bool fill = req->order_type == ORDER_TYPE_MARKET ||
            (req->side == SIDE_BUY ? req->price >= px : req->price <= px);
        const double reserve = (fill ? px : req->price) * req->qty;
        auto& position = w.positions[req->security_code];
        if ((req->side == SIDE_BUY && reserve > w.cash - w.reserved) ||
            (req->side == SIDE_SELL && req->qty > position.available_qty)) {
            error(response, -101, "Insufficient mock cash or available shares");
            on_order_rsp_update(response);
            return 0;
        }
        TradeOrderData order{};
        identify(order, w.account, req->market, 0);
        order.order_id = next_order_++;
        order.security_info = response.security_info;
        order.side = req->side;
        order.order_type = req->order_type;
        order.batch_id = req->batch_id;
        order.price = req->price;
        order.qty = req->qty;
        order.leaves_qty = req->qty;
        order.order_time = order.update_time = now();
        order.status = ORDER_STATUS_NEW;
        std::snprintf(order.status_msg, sizeof(order.status_msg), "MOCK accepted");
        response.order_id = order.order_id;
        if (fill) {
            identify(position, w.account, req->market, 0);
            position.security_info = response.security_info;
            if (req->side == SIDE_BUY) {
                const double cost = position.cost_price * position.total_qty + px * req->qty;
                position.total_qty += req->qty;
                position.available_qty += req->qty;
                position.today_buy_qty += req->qty;
                position.cost_price = cost / position.total_qty;
                w.cash -= px * req->qty;
            } else {
                position.total_qty -= req->qty;
                position.available_qty -= req->qty;
                position.today_sell_qty += req->qty;
                w.cash += px * req->qty;
            }
            position.market_price = px;
            position.market_value = position.total_qty * px;
            position.update_time = now();
            order.status = ORDER_STATUS_FILLED;
            order.leaves_qty = 0;
            order.cum_qty = req->qty;
            order.avg_price = px;
            order.cum_amt = px * req->qty;
            std::snprintf(order.status_msg, sizeof(order.status_msg), "MOCK filled at fixed reference price");
            TradeFillData trade{};
            identify(trade, w.account, req->market, 0);
            trade.security_info = order.security_info;
            trade.order_id = order.order_id;
            trade.batch_id = req->batch_id;
            trade.side = req->side;
            trade.order_type = req->order_type;
            trade.price = px;
            trade.qty = req->qty;
            trade.value = px * req->qty;
            trade.trade_time = now();
            std::snprintf(trade.trade_id, sizeof(trade.trade_id), "MOCK-%lld", static_cast<long long>(order.order_id));
            std::snprintf(trade.currency, sizeof(trade.currency), "CNY");
            w.fills.push_back(trade);
            on_fill_query_result(trade);
        } else if (req->side == SIDE_BUY) {
            w.reserved += reserve;
        } else {
            position.available_qty -= req->qty;
            position.frozen_qty += req->qty;
        }
        w.orders.emplace(order.order_id, order);
        on_order_update(order);
        on_order_rsp_update(response);
        TRADE_LOG_INFO("MOCK order {}: {} {} x {}, status {}", order.order_id,
                       req->side == SIDE_BUY ? "BUY" : "SELL", req->security_code, req->qty, static_cast<int>(order.status));
        return 0;
    }
    int cancel_order_impl(const AccountInfo* a, int64_t oid, uint64_t, MarketType market, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto& w = wallets_.at(a->login_id);
        TradeOrderCancelRsqData result{};
        result.account = *a;
        result.request_id = rid;
        result.order_id = oid;
        result.timestamp = now();
        auto it = w.orders.find(oid);
        if (it == w.orders.end() || it->second.status != ORDER_STATUS_NEW || it->second.current_market != market) {
            error(result, -102, "Mock order not found or no longer cancellable");
        } else {
            auto& order = it->second;
            if (order.side == SIDE_BUY) w.reserved -= order.price * order.leaves_qty;
            else {
                auto& p = w.positions.at(order.security_info.code);
                p.available_qty += order.leaves_qty;
                p.frozen_qty -= order.leaves_qty;
            }
            order.status = ORDER_STATUS_CANCELLED;
            order.leaves_qty = 0;
            order.update_time = now();
            std::snprintf(order.status_msg, sizeof(order.status_msg), "MOCK cancelled");
            on_order_update(order);
            TRADE_LOG_INFO("MOCK cancelled order {}: released remaining cash / shares", oid);
        }
        on_cancel_rsp_update(result);
        return 0;
    }
    int query_orders_impl(const AccountInfo* a, MarketType market, const char* code,
                          int64_t oid, const QueryRequestParams*, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::vector<TradeOrderData> rows;
        for (auto item : wallets_.at(a->login_id).orders) {
            auto d = item.second;
            if (d.current_market != market || (code && *code && std::string(code) != d.security_info.code) || (oid && oid != d.order_id)) continue;
            d.request_id = rid; rows.push_back(d);
        }
        TradeOrderData empty{}; empty.request_id = rid;
        emit_rows(rows, empty, 100008, [this](const auto& d) { on_order_query_result(d); });
        return 0;
    }
    int query_trades_impl(const AccountInfo* a, MarketType market, const char* code,
                          int64_t oid, const QueryRequestParams*, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::vector<TradeFillData> rows;
        for (auto d : wallets_.at(a->login_id).fills) {
            if (d.current_market != market || (code && *code && std::string(code) != d.security_info.code) || (oid && oid != d.order_id)) continue;
            d.request_id = rid; rows.push_back(d);
        }
        TradeFillData empty{}; empty.request_id = rid;
        emit_rows(rows, empty, 100009, [this](const auto& d) { on_fill_query_result(d); });
        return 0;
    }
    int query_positions_impl(const AccountInfo* a, MarketType market, const char* code,
                             const QueryRequestParams*, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::vector<PositionData> rows;
        for (auto item : wallets_.at(a->login_id).positions) {
            auto d = item.second;
            if (!d.total_qty || d.current_market != market || (code && *code && std::string(code) != item.first)) continue;
            d.request_id = rid; rows.push_back(d);
        }
        PositionData empty{}; empty.request_id = rid;
        emit_rows(rows, empty, 100010, [this](const auto& d) { on_position_query_result(d); });
        return 0;
    }
    int query_funds_impl(const AccountInfo* a, MarketType market, const char* currency,
                         const QueryRequestParams*, int64_t rid) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto& w = wallets_.at(a->login_id);
        AccountData data{};
        identify(data, *a, market, rid);
        std::snprintf(data.currency, sizeof(data.currency), "CNY");
        if (currency && *currency && std::string(currency) != "CNY") error(data, -103, "Mock supports CNY only");
        data.cash_balance = w.cash;
        data.available_cash = w.cash - w.reserved;
        data.frozen_cash = w.reserved;
        for (const auto& p : w.positions) data.market_value += p.second.market_value;
        data.total_asset = data.cash_balance + data.market_value;
        data.buying_power = data.available_cash;
        data.update_time = now();
        data.is_last = true;
        on_account_query_result(data, data.current_account_id);
        return 0;
    }
    int bank_securities_transfer_impl(const AccountInfo*, BankSecuritiesTransferDirection, MarketType, double,
                                      const char*, const char*, const char*, const char*, int64_t) override {
        set_error("Bank transfers are not supported by the mock plugin"); return -1;
    }
    int fund_transfer_impl(const AccountInfo*, const char*, MarketType, const char*, MarketType, double, int64_t) override {
        set_error("Fund transfers are not supported by the mock plugin"); return -1;
    }
};
}
extern "C" {
EXPORT_FLAG ITradeHandler* create_trade_handler() { return reinterpret_cast<ITradeHandler*>(new ft::MockTradeHandler); }
EXPORT_FLAG void delete_trade_handler(ITradeHandler*& handler) { delete reinterpret_cast<ft::ITradeHandler*>(handler); handler = nullptr; }
EXPORT_FLAG void set_logger_state(void* state) {
    if (state) ft::Logger::set_logger_state(static_cast<ft::LoggerState*>(state));
}
EXPORT_FLAG void reset_logger_state() { ft::Logger::reset_logger_state(); }
}
