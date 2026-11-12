#ifndef UTILS_MOCK_H
#define UTILS_MOCK_H

#include "types/quote_types.h"
#include "types/base_types.h"
#include "types/trade_types.h"
#include "quote/quote_interface.h"
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <cstring>

NS_FT_BEGIN

// Mock数据生成器类
class MockDataGenerator {
public:
    // 回调函数类型定义
    using IndexDataCallback = std::function<void(const IndexData&)>;
    using DepthDataCallback = std::function<void(const DepthData&)>;
    using DepthFiftyDataCallback = std::function<void(const DepthFiftyData&)>;
    using EntrustDataCallback = std::function<void(const EntrustData&)>;
    using TradeDataCallback = std::function<void(const TradeData&)>;

    // Mock代码配置结构
    struct MockCodeConfig {
        const char* sse_index_code;
        const char* sse_index_name;
        const char* sse_stock_code;
        const char* sse_stock_name;
        const char* szse_index_code;
        const char* szse_index_name;
        const char* szse_stock_code;
        const char* szse_stock_name;
    };
    
    // ===== CSV数据记录结构（完全对应quote_types.h） =====
    
    // CSV指数数据记录 - 对应IndexData
    struct CSVIndexRecord {
        std::string security_code;
        std::string security_name;
        MarketType market;
        uint64_t timestamp;
        uint32_t update_time;
        TradingPhase trading_phase;
        
        double last_index;
        double pre_close_index;
        double open_index;
        double high_index;
        double low_index;
        double close_index;
        
        uint64_t total_volume;
        uint64_t total_value;
        uint64_t num_trades;
        
        uint32_t sequence;
        
        CSVIndexRecord() : market(MARKET_SSE), timestamp(0), update_time(0),
                          trading_phase(TRADING_PHASE_CONTINUOUS),
                          last_index(0.0), pre_close_index(0.0), open_index(0.0),
                          high_index(0.0), low_index(0.0), close_index(0.0),
                          total_volume(0), total_value(0), num_trades(0), sequence(0) {}
    };
    
    // CSV深度数据记录 - 对应DepthData
    struct CSVDepthRecord {
        std::string security_code;
        std::string security_name;
        std::string security_type;
        MarketType market;
        uint64_t timestamp;
        uint32_t update_time;
        TradingPhase trading_phase;
        std::string instrument_status;
        
        // 价格信息
        double last_price;
        double pre_close_price;
        double open_price;
        double high_price;
        double low_price;
        double close_price;
        double upper_limit_price;
        double lower_limit_price;
        double avg_price;
        
        // 成交信息
        uint64_t total_volume;
        uint64_t total_value;
        uint64_t num_trades;
        uint64_t total_bid_qty;
        uint64_t total_ask_qty;
        double weighted_avg_bid_price;
        double weighted_avg_ask_price;
        uint32_t total_bid_number;
        uint32_t total_ask_number;
        
        // 十档行情
        double bid_prices[10];
        uint64_t bid_qtys[10];
        double ask_prices[10];
        uint64_t ask_qtys[10];
        
        // 其他字段
        uint64_t total_position;
        double reference_price;
        double iopv;
        double pre_iopv;
        uint16_t channel_no;
        uint32_t sequence;
        
        CSVDepthRecord() : market(MARKET_SSE), timestamp(0), update_time(0), 
                          trading_phase(TRADING_PHASE_CONTINUOUS), 
                          last_price(0.0), pre_close_price(0.0), open_price(0.0),
                          high_price(0.0), low_price(0.0), close_price(0.0),
                          upper_limit_price(0.0), lower_limit_price(0.0), avg_price(0.0),
                          total_volume(0), total_value(0), num_trades(0),
                          total_bid_qty(0), total_ask_qty(0), 
                          weighted_avg_bid_price(0.0), weighted_avg_ask_price(0.0),
                          total_bid_number(0), total_ask_number(0),
                          total_position(0), reference_price(0.0), 
                          iopv(0.0), pre_iopv(0.0), channel_no(0), sequence(0) {
            memset(bid_prices, 0, sizeof(bid_prices));
            memset(bid_qtys, 0, sizeof(bid_qtys));
            memset(ask_prices, 0, sizeof(ask_prices));
            memset(ask_qtys, 0, sizeof(ask_qtys));
        }
    };
    
    // CSV 50档数据记录 - 对应DepthFiftyData
    struct CSVDepthFiftyRecord {
        std::string security_code;
        std::string security_name;
        std::string security_type;
        MarketType market;
        uint64_t timestamp;
        uint32_t update_time;
        
        double bid_price;
        uint64_t bid_qty;
        uint32_t bid_num_orders;
        uint8_t bid_show_orders;
        
        double ask_price;
        uint64_t ask_qty;
        uint32_t ask_num_orders;
        uint8_t ask_show_orders;
        
        uint32_t orders[100];  // 50笔买+50笔卖
        
        uint16_t channel_no;
        uint32_t sequence;
        
        CSVDepthFiftyRecord() : market(MARKET_SSE), timestamp(0), update_time(0),
                               bid_price(0.0), bid_qty(0), bid_num_orders(0), bid_show_orders(0),
                               ask_price(0.0), ask_qty(0), ask_num_orders(0), ask_show_orders(0),
                               channel_no(0), sequence(0) {
            memset(orders, 0, sizeof(orders));
        }
    };
    
    // CSV委托数据记录 - 对应EntrustData
    struct CSVEntrustRecord {
        std::string security_code;
        std::string security_name;
        std::string security_type;
        MarketType market;
        uint64_t timestamp;
        uint64_t transact_time;
        
        char order_type;      // 'A'-新增, 'D'-删除, 'S'-状态
        char side;           // 'B'-买, 'S'-卖
        char bs_flag;
        
        uint64_t order_no;
        uint32_t order_index;
        double price;
        uint64_t qty;
        uint64_t balance;
        
        uint16_t channel_no;
        uint64_t biz_index;
        uint64_t appl_seq_num;
        uint32_t sequence;
        
        CSVEntrustRecord() : market(MARKET_SSE), timestamp(0), transact_time(0),
                            order_type('A'), side('B'), bs_flag('B'),
                            order_no(0), order_index(0), price(0.0), qty(0), balance(0),
                            channel_no(0), biz_index(0), appl_seq_num(0), sequence(0) {}
    };
    
    // CSV成交数据记录 - 对应TradeData
    struct CSVTradeRecord {
        std::string security_code;
        std::string security_name;
        std::string security_type;
        MarketType market;
        uint64_t timestamp;
        uint64_t transact_time;
        
        uint64_t trade_id;
        uint32_t trade_index;
        double price;
        uint64_t qty;
        uint64_t value;
        
        uint64_t bid_order_no;
        uint64_t ask_order_no;
        uint64_t bid_appl_seq_num;
        uint64_t ask_appl_seq_num;
        
        char exec_type;       // 'F'-成交, '4'-撤销
        char trade_flag;      // 内外盘标志
        
        uint16_t channel_no;
        uint64_t biz_index;
        uint64_t appl_seq_num;
        uint32_t sequence;
        
        CSVTradeRecord() : market(MARKET_SSE), timestamp(0), transact_time(0),
                          trade_id(0), trade_index(0), price(0.0), qty(0), value(0),
                          bid_order_no(0), ask_order_no(0), bid_appl_seq_num(0), ask_appl_seq_num(0),
                          exec_type('F'), trade_flag(0),
                          channel_no(0), biz_index(0), appl_seq_num(0), sequence(0) {}
    };

    // 构造函数
    MockDataGenerator();

    // 设置回调函数
    void set_index_callback(IndexDataCallback callback);
    void set_depth_callback(DepthDataCallback callback);
    void set_depth_fifty_callback(DepthFiftyDataCallback callback);
    void set_entrust_callback(EntrustDataCallback callback);
    void set_trade_callback(TradeDataCallback callback);

    // 设置mock代码配置
    void set_mock_codes(const MockCodeConfig& config);

    // ===== CSV数据支持 =====
    
    /**
     * 启用CSV数据模式
     * @param enabled 是否启用CSV模式
     */
    void enable_csv_mode(bool enabled);
    
    /**
     * 设置CSV文件路径
     * @param data_type 数据类型（"index", "depth", "depth_fifty", "entrust", "trade"）
     * @param csv_file_path CSV文件路径
     * @return true成功，false失败
     */
    bool set_csv_file(const std::string& data_type, const std::string& csv_file_path);
    
    /**
     * 加载所有CSV数据文件
     * @return true成功，false失败
     */
    bool load_csv_data();
    
    /**
     * 检查CSV模式是否启用
     * @return true已启用，false未启用
     */
    bool is_csv_mode_enabled() const;

    // 生成一轮完整的mock数据
    void generate_mock_data_round();

    // 重置序列号
    void reset_sequence();
    
    // 获取当前序列号
    uint32_t get_sequence() const;

private:
    uint32_t _sequence;
    MockCodeConfig _mock_codes;
    
    // 回调函数
    IndexDataCallback _index_callback;
    DepthDataCallback _depth_callback;
    DepthFiftyDataCallback _depth_fifty_callback;
    EntrustDataCallback _entrust_callback;
    TradeDataCallback _trade_callback;
    
    // CSV数据相关
    bool _csv_mode_enabled;
    std::map<std::string, std::string> _csv_file_paths;  // 数据类型 -> CSV文件路径
    
    // 各种CSV数据记录
    std::vector<CSVIndexRecord> _csv_index_records;
    std::vector<CSVDepthRecord> _csv_depth_records;
    std::vector<CSVDepthFiftyRecord> _csv_depth_fifty_records;
    std::vector<CSVEntrustRecord> _csv_entrust_records;
    std::vector<CSVTradeRecord> _csv_trade_records;
    
    // 各种数据类型的当前播放索引
    size_t _csv_index_current_index;
    size_t _csv_depth_current_index;
    size_t _csv_depth_fifty_current_index;
    size_t _csv_entrust_current_index;
    size_t _csv_trade_current_index;

    // 内部生成方法
    void generate_sse_index_data();
    void generate_sse_depth_data();
    void generate_sse_entrust_data();
    void generate_sse_trade_data();
    void generate_sse_depth_fifty_data();
    void generate_szse_index_data();
    void generate_szse_depth_data();
    void generate_szse_entrust_data();
    void generate_szse_trade_data();
    void generate_szse_depth_fifty_data();
    
    // CSV数据处理方法
    void generate_csv_index_data();
    void generate_csv_depth_data();
    void generate_csv_depth_fifty_data();
    void generate_csv_entrust_data();
    void generate_csv_trade_data();
    
    // CSV文件解析方法
    bool _parse_csv_index_file(const std::string& file_path);
    bool _parse_csv_depth_file(const std::string& file_path);
    bool _parse_csv_depth_fifty_file(const std::string& file_path);
    bool _parse_csv_entrust_file(const std::string& file_path);
    bool _parse_csv_trade_file(const std::string& file_path);
    
    // CSV行解析方法
    CSVIndexRecord _parse_csv_index_line(const std::string& line);
    CSVDepthRecord _parse_csv_depth_line(const std::string& line);
    CSVDepthFiftyRecord _parse_csv_depth_fifty_line(const std::string& line);
    CSVEntrustRecord _parse_csv_entrust_line(const std::string& line);
    CSVTradeRecord _parse_csv_trade_line(const std::string& line);
    
    // 通用辅助方法
    MarketType _parse_market_type(const std::string& market_str);
    TradingPhase _parse_trading_phase(const std::string& phase_str);
    char _safe_parse_char(const std::string& str, char default_val = 0);
    uint8_t _safe_parse_uint8(const std::string& str, uint8_t default_val = 0);
    std::vector<std::string> _split_csv_line(const std::string& line, char delimiter = ',');
    std::string _trim_string(const std::string& str);
    double _safe_parse_double(const std::string& str, double default_val = 0.0);
    uint64_t _safe_parse_uint64(const std::string& str, uint64_t default_val = 0);
    uint32_t _safe_parse_uint32(const std::string& str, uint32_t default_val = 0);
    
    // 初始化默认代码配置
    void init_default_codes();
};

NS_FT_END

#endif // UTILS_MOCK_H