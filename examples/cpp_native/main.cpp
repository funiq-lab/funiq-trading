#include "quote/quote_service.h"
#include "trade/trade_service.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: cpp_native_example /path/to/examples/mock_core");
        auto config = std::filesystem::absolute(argv[1]);
        // This example only submits orders to the supplied mock plugins.
        for (auto item : {std::make_pair("quote_config.json", "ft_mock_quote_parser"),
                          std::make_pair("trade_config.json", "ft_mock_trade_handler")}) {
            nlohmann::json value; std::ifstream(config / item.first) >> value;
            int active = 0;
            for (const auto& entry : value.at("parsers")) {
                if (entry.value("active", false)) {
                    ++active;
                    check(entry.at("module") == item.second, "Expected mock plugin configuration");
                }
            }
            check(active == 1, "Expected one active mock plugin");
        }
        setenv("QUOTE_CONFIG_FILE", (config / "quote_config.json").c_str(), 1);
        setenv("TRADE_CONFIG_FILE", (config / "trade_config.json").c_str(), 1);
        setenv("LOG_ENABLED", "false", 1);
        // Direct C++ objects: no quote_init()/trade_init() wrappers or HTTP server.
        ft::QuoteService quotes;
        ft::TradeService trades;
        check(quotes.init() == 0 && quotes.start() == 0, quotes.get_last_error());
        check(trades.init() == 0 && trades.start() == 0, trades.get_last_error());
        auto* parser = quotes.get_parser();
        auto* handler = trades.get_handler();
        check(parser && handler, "Missing service objects");
        std::cout << "Funiq Trading | Direct C++ calls\n"
                  << "Objects: ft::QuoteService + ft::TradeService\n"
                  << "Process-local mock account.\n"
                  << "Quote plugin: " << quotes.get_parser_id() << " | running=" << quotes.is_running() << "\n"
                  << "Trade plugin: " << trades.get_handler_id() << " | running=" << trades.is_running() << "\n\n";
        DepthData depth{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            parser->get_depth_data(&depth);
            if (depth.last_price > 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(depth.last_price > 0, "No generated quote received");
        std::cout << std::fixed << std::setprecision(2);
        for (int i = 0; i < 3; ++i) {
            parser->get_depth_data(&depth);
            std::cout << "C++ quote: symbol=" << depth.security_info.code << " last=" << depth.last_price
                      << " sequence=" << depth.sequence << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        AccountInfo account{};
        check(handler->get_account_info_by_id("MOCK_DEMO", &account) == 0, "Mock account missing");
        TradeOrderRequest order{};
        order.account = account; std::strcpy(order.security_code, "000001");
        order.market = MARKET_SZSE; order.side = SIDE_BUY; order.order_type = ORDER_TYPE_LIMIT;
        order.price = 20; order.qty = 1000;
        auto buy = handler->send_order_sync(&order, 1000);
        check(buy.is_success(), buy.error_msg);
        std::cout << "\nC++ buy: order=" << buy.data.order_id << " shares=" << buy.data.qty << "\n";
        order.price = 1; order.qty = 100;
        auto resting = handler->send_order_sync(&order, 1000);
        check(resting.is_success(), resting.error_msg);
        auto cancelled = handler->cancel_order_sync(&account, resting.data.order_id, 0, MARKET_SZSE, 1000);
        check(cancelled.is_success(), cancelled.error_msg);
        std::cout << "C++ cancel: order=" << resting.data.order_id << " released 100.00 CNY\n";
        auto funds = handler->query_funds_sync(&account, MARKET_SZSE, "CNY", nullptr, 1000);
        check(funds.is_success() && funds.data.size() == 1, "Fund query failed");
        check(funds.data[0].cash_balance == 985000 && funds.data[0].frozen_cash == 0, "Cash reconciliation failed");
        std::cout << "Cash=" << funds.data[0].cash_balance << " CNY, position value=" << funds.data[0].market_value << " CNY\n";
        check(trades.stop() == 0 && quotes.stop() == 0, "Service shutdown failed");
        std::cout << "PASS: direct C++ quotes, orders, cancellation and clean shutdown\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Example error: " << e.what() << "\n";
        return 1;
    }
}
