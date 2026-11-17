#include "quote/quote_parser.h"
#include "trade/trade_handler.h"
#include "utils/sync_call_framework.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

extern "C" IQuoteParser* create_quote_parser();
extern "C" ITradeHandler* create_trade_handler();
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main() {
    // A plugin may respond synchronously, before create_future() is called.
    ft::SyncCallManager<int> manager;
    manager.set_success(1, 42);
    auto future = manager.create_future(1);
    CHECK(future.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready);
    CHECK(future.get().data == 42);
    manager.cleanup(1);
    manager.set_error(2, -7, "early error");
    auto failed = manager.create_future(2);
    CHECK(failed.get().error_code == -7);
    manager.cleanup(2);
    auto late = manager.create_future(3);
    manager.set_success(3, 99);
    CHECK(late.get().data == 99);
    manager.cleanup(3);

    std::unique_ptr<ft::IQuoteParser> parser(reinterpret_cast<ft::IQuoteParser*>(create_quote_parser()));
    QuoteConfig quote{};
    MarketType markets[] = {MARKET_SSE, MARKET_SZSE};
    FTDataType types[] = {DATA_TYPE_DEPTH};
    quote.subscription.markets = markets;
    quote.subscription.market_count = 2;
    quote.subscription.data_types = types;
    quote.subscription.data_type_count = 1;
    quote.mock.enabled = true;
    quote.mock.loop_replay = true;
    std::strcpy(quote.mock.mock_data_file, "/tmp/funiq-mock-no-input");
    CHECK(parser->init(&quote) == 0);
    CHECK(parser->start() == 0);
    CHECK(parser->connect() == 0);
    DepthData depth{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        parser->get_depth_data(&depth);
        if (depth.last_price > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(depth.last_price > 0);
    CHECK(parser->stop() == 0);
    parser->release();

    std::unique_ptr<ft::ITradeHandler> handler(reinterpret_cast<ft::ITradeHandler*>(create_trade_handler()));
    AccountInfo account{};
    std::strcpy(account.login_id, "MOCK_DEMO");
    std::strcpy(account.fund_account_id, "MOCK_FUND");
    std::strcpy(account.account_id_sh, "MOCK_SH");
    std::strcpy(account.account_id_sz, "MOCK_SZ");
    TradeConfig config{};
    config.login_mode = LOGIN_MODE_FUND_ACCOUNT_ID;
    config.account_list.accounts = &account;
    config.account_list.account_count = 1;
    CHECK(handler->init(&config) == 0);
    CHECK(handler->start() == 0);
    CHECK(handler->connect() == 0);
    CHECK(handler->is_account_login(account.login_id));
    auto cash = [&]() {
        auto r = handler->query_funds_sync(&account, MARKET_SZSE, "CNY", nullptr, 1000);
        CHECK(r.is_success()); CHECK(r.data.size() == 1); return r.data.front();
    };
    CHECK(cash().cash_balance == 1000000);
    auto empty = handler->query_orders_sync(&account, MARKET_SZSE, nullptr, 0, nullptr, 1000);
    CHECK(empty.error_code == 100008);
    TradeOrderRequest order{};
    order.account = account;
    std::strcpy(order.security_code, "000001");
    order.market = MARKET_SZSE;
    order.side = SIDE_BUY;
    order.order_type = ORDER_TYPE_LIMIT;
    order.price = 20;
    order.qty = 1000;
    auto buy = handler->send_order_sync(&order, 1000);
    CHECK(buy.is_success());
    CHECK(cash().cash_balance == 985000);
    auto positions = handler->query_positions_sync(&account, MARKET_SZSE, nullptr, nullptr, 1000);
    CHECK(positions.is_success()); CHECK(positions.data.size() == 1);
    CHECK(positions.data[0].total_qty == 1000);
    // Resting limit orders reserve funds, then cancellation releases them.
    order.price = 1; order.qty = 100;
    auto resting = handler->send_order_sync(&order, 1000);
    CHECK(resting.is_success()); CHECK(cash().frozen_cash == 100);
    auto cancelled = handler->cancel_order_sync(&account, resting.data.order_id, 0, MARKET_SZSE, 1000);
    CHECK(cancelled.is_success()); CHECK(cash().frozen_cash == 0);
    // Insufficient funds and filled-order cancellation must fail.
    order.price = 20; order.qty = 1000000;
    CHECK(handler->send_order_sync(&order, 1000).error_code == -101);
    CHECK(handler->cancel_order_sync(&account, buy.data.order_id, 0, MARKET_SZSE, 1000).error_code == -102);
    // Selling updates inventory, fills and cash consistently.
    order.side = SIDE_SELL; order.price = 1; order.qty = 400;
    CHECK(handler->send_order_sync(&order, 1000).is_success());
    CHECK(cash().cash_balance == 991000);
    positions = handler->query_positions_sync(&account, MARKET_SZSE, nullptr, nullptr, 1000);
    CHECK(positions.data[0].total_qty == 600);
    auto trades = handler->query_trades_sync(&account, MARKET_SZSE, nullptr, 0, nullptr, 1000);
    CHECK(trades.is_success()); CHECK(trades.data.size() == 2);
    CHECK(cash().total_asset == 1000000);
    CHECK(handler->stop() == 0);
    CHECK(!handler->is_account_login(account.login_id));
    handler->release();
    std::cout << "PASS: synthetic quotes, immediate callbacks, orders, fills, reservations, cancellation, rejection, positions and funds\n";
}
