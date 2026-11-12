#pragma once

#include "strategy/base_strategy.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

NS_FT_BEGIN

// Teaching example: exits configured paper positions. Never sends broker orders.
class StopProfitLossStrategy final : public BaseStrategy {
public:
    int init(const char* config) override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_state == StrategyState::RUNNING) return -1;
        try {
            auto parsed = nlohmann::json::parse(config ? config : "{}");
            const double profit = parsed.value("profit_threshold", 3.0);
            const double loss = parsed.value("loss_threshold", 1.5);
            if (!std::isfinite(profit) || !std::isfinite(loss) || profit <= 0 || loss <= 0)
                throw std::invalid_argument("Thresholds must be positive finite percentages");
            if (!parsed.contains("positions") || !parsed["positions"].is_array() || parsed["positions"].empty())
                throw std::invalid_argument("Provide nonempty paper positions");
            std::map<std::string, Position> positions;
            for (const auto& item : parsed["positions"]) {
                const auto code = item.at("code").get<std::string>();
                const auto market = item.at("market").get<int>();
                const auto entry = item.at("entry_price").get<double>();
                if (!item.at("quantity").is_number_integer())
                    throw std::invalid_argument("Quantity must be an integer");
                const auto quantity = item.at("quantity").get<int64_t>();
                if (code.size() != 6 || code.find_first_not_of("0123456789") != std::string::npos ||
                    (market != MARKET_SSE && market != MARKET_SZSE) ||
                    !std::isfinite(entry) || entry <= 0 || quantity <= 0)
                    throw std::invalid_argument("Invalid paper position");
                if (!positions.emplace(key(code, market), Position{code, market, entry, quantity}).second)
                    throw std::invalid_argument("Duplicate position");
            }
            // Commit only after the entire configuration has validated.
            _positions = std::move(positions);
            _profit = profit;
            _loss = loss;
            _config = parsed.dump();
            _signals = nlohmann::json::array();
            _error.clear();
            _state = StrategyState::INITIALIZED;
            return 0;
        } catch (const std::exception& e) {
            _error = e.what();
            return -1;
        }
    }
    int start() override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_state != StrategyState::INITIALIZED && _state != StrategyState::STOPPED) return -1;
        _state = StrategyState::RUNNING;
        return 0;
    }
    int stop() override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_state != StrategyState::RUNNING) return -1;
        _state = StrategyState::STOPPED;
        return 0;
    }
    void release() override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _positions.clear();
        _signals = nlohmann::json::array();
        _config = "{}";
        _state = StrategyState::UNINITIALIZED;
    }
    const char* set_strategy_id(const char* id) override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _id = id ? id : "";
        return snapshot(_id);
    }
    const char* get_strategy_id() const override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return snapshot(_id);
    }
    StrategyState get_state() const override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _state;
    }
    const char* get_last_error() const override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return snapshot(_error);
    }
    int update_config(const char* config) override { return init(config); }
    const char* get_config() const override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return snapshot(_config);
    }
    const char* get_available_data_types() const override { return R"(["status","positions","signals"])"; }
    const char* get_strategy_data(const char* type) const override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!type) return nullptr;
        if (std::strcmp(type, "signals") == 0) return snapshot(_signals.dump());
        if (std::strcmp(type, "status") == 0)
            return snapshot(nlohmann::json({{"paper_only", true}, {"signal_count", _signals.size()},
                                           {"state", static_cast<int>(_state)}}).dump());
        if (std::strcmp(type, "positions") != 0) return nullptr;
        auto result = nlohmann::json::array();
        for (const auto& pair : _positions) {
            const auto& p = pair.second;
            result.push_back({{"code", p.code}, {"market", p.market}, {"entry_price", p.entry},
                              {"quantity", p.quantity}, {"closed", p.closed}});
        }
        return snapshot(result.dump());
    }
    void on_depth_data(const DepthData* data) override {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!data || _state != StrategyState::RUNNING ||
            !std::isfinite(data->last_price) || data->last_price <= 0) return;
        const auto* end = static_cast<const char*>(std::memchr(data->security_info.code, '\0', sizeof(data->security_info.code)));
        if (!end) return;
        auto it = _positions.find(key(std::string(data->security_info.code, end), data->security_info.market));
        if (it == _positions.end() || it->second.closed) return;
        auto& p = it->second;
        const double change = (data->last_price / p.entry - 1.0) * 100.0;
        if (change < _profit - 1e-9 && change > -_loss + 1e-9) return;
        p.closed = true;
        _signals.push_back({{"code", p.code}, {"market", p.market}, {"quantity", p.quantity},
                            {"price", data->last_price}, {"change_pct", change}, {"paper_only", true},
                            {"reason", change >= _profit - 1e-9 ? "take_profit" : "stop_loss"}});
    }
    void set_quote_parser(IQuoteParser* value) override { _quote = value; }
    void set_trade_handler(ITradeHandler* value) override { _trade = value; }
    void set_db_manager(DatabaseManager* value) override { _db = value; }
    IQuoteParser* get_quote_parser() const override { return _quote; }
    ITradeHandler* get_trade_handler() const override { return _trade; }
    DatabaseManager* get_db_manager() const override { return _db; }

private:
    struct Position { std::string code; int market; double entry; int64_t quantity; bool closed = false; };
    static std::string key(const std::string& code, int market) { return std::to_string(market) + ":" + code; }
    // Like other const char* APIs, returned text lasts until the next getter on this thread.
    static const char* snapshot(const std::string& value) { thread_local std::string text; text = value; return text.c_str(); }
    mutable std::recursive_mutex _mutex;
    StrategyState _state = StrategyState::UNINITIALIZED;
    std::string _id, _error, _config = "{}";
    double _profit = 3.0, _loss = 1.5;
    std::map<std::string, Position> _positions;
    nlohmann::json _signals = nlohmann::json::array();
    IQuoteParser* _quote = nullptr;
    ITradeHandler* _trade = nullptr;
    DatabaseManager* _db = nullptr;
};

NS_FT_END
