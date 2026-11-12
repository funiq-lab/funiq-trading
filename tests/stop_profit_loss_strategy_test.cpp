#include "../src/strategy/strategies/stop_profit_loss_strategy/stop_profit_loss_strategy.h"
#include <cassert>
#include <limits>
#include <iostream>

using nlohmann::json;

int main() {
    ft::StopProfitLossStrategy strategy;
    const auto config = R"({"profit_threshold":3,"loss_threshold":1.5,"positions":[
        {"code":"000001","market":2,"entry_price":10,"quantity":100},
        {"code":"600000","market":1,"entry_price":20,"quantity":200}]})";
    assert(strategy.start() == -1);
    assert(strategy.init(config) == 0);
    assert(strategy.get_trade_handler() == nullptr);
    assert(strategy.start() == 0);
    DepthData depth{};
    std::strcpy(depth.security_info.code, "000001");
    depth.security_info.market = MARKET_SZSE;
    auto signals = [&] { return json::parse(strategy.get_strategy_data("signals")); };
    strategy.on_depth_data(nullptr);
    depth.last_price = std::numeric_limits<double>::quiet_NaN();
    strategy.on_depth_data(&depth);
    assert(signals().empty());
    depth.last_price = 10.29;
    strategy.on_depth_data(&depth);
    assert(signals().empty());
    // Same code on another exchange must not close the monitored position.
    depth.security_info.market = MARKET_SSE;
    depth.last_price = 10.3;
    strategy.on_depth_data(&depth);
    assert(signals().empty());
    depth.security_info.market = MARKET_SZSE;
    strategy.on_depth_data(&depth);
    assert(signals().size() == 1);
    assert(signals()[0]["reason"] == "take_profit");
    strategy.on_depth_data(&depth);
    assert(signals().size() == 1); // one exit per configured paper position
    assert(strategy.update_config(config) == -1); // cannot reset while running
    assert(strategy.stop() == 0);
    std::strcpy(depth.security_info.code, "600000");
    depth.security_info.market = MARKET_SSE;
    depth.last_price = 19.7;
    strategy.on_depth_data(&depth);
    assert(signals().size() == 1); // stopped means no processing
    assert(strategy.start() == 0);
    strategy.on_depth_data(&depth);
    assert(signals().size() == 2);
    assert(signals()[1]["reason"] == "stop_loss");
    assert(signals()[1]["quantity"] == 200);
    assert(strategy.get_strategy_data("unknown") == nullptr);
    assert(strategy.stop() == 0);
    assert(strategy.init(R"({"positions":[]})") == -1);
    assert(signals().size() == 2); // invalid config leaves prior state intact
    assert(strategy.init(R"({"positions":[{"code":"000001","market":2,"entry_price":10,"quantity":1.5}]})") == -1);
    assert(strategy.init(config) == 0);
    assert(signals().empty());
    strategy.release();
    assert(strategy.get_state() == StrategyState::UNINITIALIZED);
    assert(strategy.start() == -1);
    std::cout << "Paper strategy lifecycle, thresholds, market isolation and config validation passed\n";
}
