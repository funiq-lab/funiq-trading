#include "quote/base_quote_parser.h"
#include "logger/logger.hpp"
#include <condition_variable>

namespace ft {
// Reuse BaseQuoteParser's synthetic / CSV / binary replay implementation.
class MockQuoteParser final : public BaseQuoteParser {
    std::thread log_thread_;
    std::mutex log_mutex_;
    std::condition_variable log_wake_;
    bool stop_logging_ = false;

public:
    ~MockQuoteParser() override { release(); }
protected:
    int init_impl(const QuoteConfig* config) override {
        if (!config->mock.enabled) {
            _last_error = "MockQuoteParser requires mock.enabled=true";
            return -1;
        }
        QUOTE_LOG_INFO("MOCK quote plugin ready: BaseQuoteParser synthetic / replay data; no vendor connection");
        // Sample the real base-parser cache for readable example logs, not a benchmark.
        log_thread_ = std::thread([this] {
            std::unique_lock<std::mutex> lock(log_mutex_);
            while (!log_wake_.wait_for(lock, std::chrono::seconds(3), [this] { return stop_logging_; })) {
                lock.unlock();
                if (is_running()) {
                    for (const char* code : {"600000", "000001"}) {
                        DepthData data{};
                        if (get_depth_data_by_code(code, &data) == 0) {
                            QUOTE_LOG_INFO("MOCK quote {}: last={:.2f}, bid={:.2f}, ask={:.2f}, sequence={}",
                                code, data.last_price, data.bid_price[0], data.ask_price[0], data.sequence);
                        }
                    }
                }
                lock.lock();
            }
        });
        return 0;
    }
    int start_impl() override { return 0; }
    int connect_impl() override { return 0; }
    int disconnect_impl() override { return 0; }
    int stop_impl() override { stop_mock_replay(); return 0; }
    void release_impl() override {
        { std::lock_guard<std::mutex> lock(log_mutex_); stop_logging_ = true; }
        log_wake_.notify_all();
        if (log_thread_.joinable()) log_thread_.join();
    }
};
}
extern "C" {
EXPORT_FLAG IQuoteParser* create_quote_parser() { return reinterpret_cast<IQuoteParser*>(new ft::MockQuoteParser); }
EXPORT_FLAG void delete_quote_parser(IQuoteParser*& parser) { delete reinterpret_cast<ft::IQuoteParser*>(parser); parser = nullptr; }
EXPORT_FLAG void set_logger_state(void* state) {
    if (state) ft::Logger::set_logger_state(static_cast<ft::LoggerState*>(state));
}
EXPORT_FLAG void reset_logger_state() { ft::Logger::reset_logger_state(); }
}
