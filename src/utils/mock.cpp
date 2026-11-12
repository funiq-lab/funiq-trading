#include "utils/mock.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include "utils/string.hpp"
#include "utils/time.h"

NS_FT_BEGIN

MockDataGenerator::MockDataGenerator() : _sequence(1), _csv_mode_enabled(false),
                                         _csv_index_current_index(0), _csv_depth_current_index(0), _csv_depth_fifty_current_index(0),
                                         _csv_entrust_current_index(0), _csv_trade_current_index(0)
{
    init_default_codes();
}

void MockDataGenerator::init_default_codes()
{
    _mock_codes.sse_index_code = "000001";
    _mock_codes.sse_index_name = "上证指数";
    _mock_codes.sse_stock_code = "600000";
    _mock_codes.sse_stock_name = "浦发银行";
    _mock_codes.szse_index_code = "399001";
    _mock_codes.szse_index_name = "深证成指";
    _mock_codes.szse_stock_code = "000001";
    _mock_codes.szse_stock_name = "平安银行";
}

void MockDataGenerator::set_index_callback(IndexDataCallback callback)
{
    _index_callback = callback;
}

void MockDataGenerator::set_depth_callback(DepthDataCallback callback)
{
    _depth_callback = callback;
}

void MockDataGenerator::set_depth_fifty_callback(DepthFiftyDataCallback callback)
{
    _depth_fifty_callback = callback;
}

void MockDataGenerator::set_entrust_callback(EntrustDataCallback callback)
{
    _entrust_callback = callback;
}

void MockDataGenerator::set_trade_callback(TradeDataCallback callback)
{
    _trade_callback = callback;
}

void MockDataGenerator::set_mock_codes(const MockCodeConfig &config)
{
    _mock_codes = config;
}

void MockDataGenerator::generate_mock_data_round()
{
    if (_csv_mode_enabled)
    {
        // CSV模式：使用CSV数据生成行情
        if (!_csv_index_records.empty())
        {
            generate_csv_index_data();
        }

        if (!_csv_depth_records.empty())
        {
            generate_csv_depth_data();
        }

        if (!_csv_entrust_records.empty())
        {
            generate_csv_entrust_data();
        }

        if (!_csv_trade_records.empty())
        {
            generate_csv_trade_data();
        }

        // 50档数据频率较低
        if (!_csv_depth_fifty_records.empty())
        {
            generate_csv_depth_fifty_data();
        }
    }
    else
    {
        // 传统模式：使用内置数据生成
        // 上交所数据
        generate_sse_index_data();
        generate_sse_depth_data();
        generate_sse_entrust_data();
        generate_sse_trade_data();

        // 深交所数据 - 频率与上交所一致
        generate_szse_index_data();
        generate_szse_depth_data();
        generate_szse_entrust_data();
        generate_szse_trade_data();

        // 50档数据频率较低
        generate_sse_depth_fifty_data();
        generate_szse_depth_fifty_data();
    }

    _sequence++;
}

void MockDataGenerator::reset_sequence()
{
    _sequence = 1;
}

uint32_t MockDataGenerator::get_sequence() const
{
    return _sequence;
}

void MockDataGenerator::generate_sse_index_data()
{
    if (!_index_callback)
        return;

    IndexData mock_index = {};
    mock_index.security_info.market = MARKET_SSE;
    safe_copy_string(mock_index.security_info.code, sizeof(mock_index.security_info.code), _mock_codes.sse_index_code);
    safe_copy_string(mock_index.security_info.name, sizeof(mock_index.security_info.name), _mock_codes.sse_index_name);

    mock_index.timestamp = get_current_timestamp();
    mock_index.update_time = _sequence * 1000;
    mock_index.trading_phase = TRADING_PHASE_CONTINUOUS;

    mock_index.last_index = 3000.0 + (_sequence % 100);
    mock_index.pre_close_index = 3000.0;
    mock_index.open_index = 2995.0 + (_sequence % 10);
    mock_index.high_index = mock_index.last_index + (_sequence % 5);
    mock_index.low_index = mock_index.last_index - (_sequence % 5);
    mock_index.close_index = mock_index.last_index;

    mock_index.total_volume = _sequence * 10000;
    mock_index.total_value = _sequence * 50000000;
    mock_index.num_trades = _sequence * 100;
    mock_index.sequence = _sequence;

    _index_callback(mock_index);
}

void MockDataGenerator::generate_sse_depth_data()
{
    if (!_depth_callback)
        return;

    DepthData mock_depth = {};
    mock_depth.security_info.market = MARKET_SSE;
    safe_copy_string(mock_depth.security_info.code, sizeof(mock_depth.security_info.code), _mock_codes.sse_stock_code);
    safe_copy_string(mock_depth.security_info.name, sizeof(mock_depth.security_info.name), _mock_codes.sse_stock_name);

    mock_depth.timestamp = get_current_timestamp();
    mock_depth.update_time = _sequence * 1000;
    mock_depth.trading_phase = TRADING_PHASE_CONTINUOUS;
    safe_copy_string(mock_depth.instrument_status, sizeof(mock_depth.instrument_status), "0");

    mock_depth.last_price = 10.0 + (_sequence % 100) * 0.01;
    mock_depth.pre_close_price = 10.0;
    mock_depth.open_price = 9.95 + (_sequence % 10) * 0.01;
    mock_depth.high_price = mock_depth.last_price + (_sequence % 5) * 0.01;
    mock_depth.low_price = mock_depth.last_price - (_sequence % 5) * 0.01;
    mock_depth.close_price = mock_depth.last_price;
    mock_depth.upper_limit_price = 11.0;
    mock_depth.lower_limit_price = 9.0;
    mock_depth.avg_price = mock_depth.last_price;

    mock_depth.total_volume = _sequence * 1000;
    mock_depth.total_value = mock_depth.last_price * mock_depth.total_volume;
    mock_depth.num_trades = _sequence * 10;
    mock_depth.total_bid_qty = _sequence * 500;
    mock_depth.total_ask_qty = _sequence * 500;
    mock_depth.weighted_avg_bid_price = mock_depth.last_price - 0.01;
    mock_depth.weighted_avg_ask_price = mock_depth.last_price + 0.01;
    mock_depth.total_bid_number = _sequence % 50;
    mock_depth.total_ask_number = _sequence % 50;

    // 生成买一卖一总价在1500万-3000万之间，较低频率命中2000万以下
    // 目标总价：1500万-3000万，较低频率(约20%)命中2000万以下
    uint64_t target_value;
    if (_sequence % 5 == 0)
    {
        // 20%的概率生成1500万-2000万之间的值
        target_value = 15000000 + (_sequence % 5000000); // 1500万-2000万
    }
    else
    {
        // 80%的概率生成2000万-3000万之间的值
        target_value = 20000000 + (_sequence % 10000000); // 2000万-3000万
    }

    for (int i = 0; i < 10; ++i)
    {
        mock_depth.bid_price[i] = mock_depth.last_price - (i + 1) * 0.01;
        mock_depth.ask_price[i] = mock_depth.last_price + (i + 1) * 0.01;

        if (i == 0)
        {
            // 买一和卖一使用计算出的数量
            mock_depth.bid_qty[i] = (uint64_t)(target_value / mock_depth.bid_price[i]);
            mock_depth.ask_qty[i] = (uint64_t)(target_value / mock_depth.ask_price[i]);
        }
        else
        {
            // 其他档位使用相对较小的数量
            mock_depth.bid_qty[i] = (i + 1) * 50000 + (_sequence % 20000);
            mock_depth.ask_qty[i] = (i + 1) * 50000 + (_sequence % 20000);
        }
    }

    mock_depth.sequence = _sequence;
    _depth_callback(mock_depth);
}

void MockDataGenerator::generate_sse_entrust_data()
{
    if (!_entrust_callback)
        return;

    EntrustData mock_entrust = {};
    mock_entrust.security_info.market = MARKET_SSE;
    safe_copy_string(mock_entrust.security_info.code, sizeof(mock_entrust.security_info.code), _mock_codes.sse_stock_code);

    mock_entrust.timestamp = get_current_timestamp();
    mock_entrust.transact_time = _sequence * 1000;

    mock_entrust.order_type = (_sequence % 2 == 0) ? 'A' : 'D';
    mock_entrust.side = (_sequence % 2 == 0) ? 'B' : 'S';
    mock_entrust.bs_flag = mock_entrust.side;

    mock_entrust.order_no = _sequence * 100;
    mock_entrust.order_index = _sequence;
    mock_entrust.price = 10.0 + (_sequence % 20) * 0.01;
    mock_entrust.qty = (_sequence % 10 + 1) * 100;
    mock_entrust.balance = mock_entrust.qty;

    mock_entrust.channel_no = _sequence % 8;
    mock_entrust.biz_index = _sequence;
    mock_entrust.sequence = _sequence;

    _entrust_callback(mock_entrust);
}

void MockDataGenerator::generate_sse_trade_data()
{
    TradeData mock_trade = {};
    mock_trade.security_info.market = MARKET_SSE;
    safe_copy_string(mock_trade.security_info.code, sizeof(mock_trade.security_info.code), _mock_codes.sse_stock_code);

    mock_trade.timestamp = get_current_timestamp();
    mock_trade.transact_time = _sequence * 1000;

    mock_trade.trade_id = _sequence;
    mock_trade.trade_index = _sequence;
    mock_trade.price = 10.0 + (_sequence % 20) * 0.01;
    mock_trade.qty = (_sequence % 5 + 1) * 100;
    mock_trade.value = mock_trade.price * mock_trade.qty;

    mock_trade.bid_order_no = _sequence * 100;
    mock_trade.ask_order_no = _sequence * 100 + 1;
    mock_trade.exec_type = 'F';                               // 成交
    mock_trade.trade_flag = (_sequence % 2 == 0) ? 'B' : 'S'; // 内外盘标志

    mock_trade.channel_no = _sequence % 8;
    mock_trade.biz_index = _sequence;
    mock_trade.sequence = _sequence;

    _trade_callback(mock_trade);
}

void MockDataGenerator::generate_sse_depth_fifty_data()
{
    DepthFiftyData mock_depth_fifty = {};
    mock_depth_fifty.security_info.market = MARKET_SSE;
    safe_copy_string(mock_depth_fifty.security_info.code, sizeof(mock_depth_fifty.security_info.code), _mock_codes.sse_stock_code);

    mock_depth_fifty.timestamp = get_current_timestamp();
    mock_depth_fifty.update_time = _sequence * 1000;

    mock_depth_fifty.bid_price = 10.0 + (_sequence % 20) * 0.01;
    mock_depth_fifty.bid_qty = (_sequence % 10 + 1) * 1000;
    mock_depth_fifty.ask_price = mock_depth_fifty.bid_price + 0.01;
    mock_depth_fifty.ask_qty = (_sequence % 10 + 1) * 1000;

    mock_depth_fifty.bid_num_orders = _sequence % 50 + 1;
    mock_depth_fifty.ask_num_orders = _sequence % 50 + 1;
    mock_depth_fifty.bid_show_orders = std::min(50, (int)mock_depth_fifty.bid_num_orders);
    mock_depth_fifty.ask_show_orders = std::min(50, (int)mock_depth_fifty.ask_num_orders);

    // 生成50档委托队列数据
    for (int i = 0; i < 100; ++i)
    {
        mock_depth_fifty.orders[i] = (_sequence + i) % 1000 + 100; // 模拟委托量
    }

    mock_depth_fifty.sequence = _sequence;
    _depth_fifty_callback(mock_depth_fifty);
}

void MockDataGenerator::generate_szse_index_data()
{
    IndexData szse_index = {};
    szse_index.security_info.market = MARKET_SZSE;
    safe_copy_string(szse_index.security_info.code, sizeof(szse_index.security_info.code), _mock_codes.szse_index_code);
    safe_copy_string(szse_index.security_info.name, sizeof(szse_index.security_info.name), _mock_codes.szse_index_name);

    szse_index.timestamp = get_current_timestamp();
    szse_index.update_time = _sequence * 1000;
    szse_index.trading_phase = TRADING_PHASE_CONTINUOUS;

    szse_index.last_index = 10000.0 + (_sequence % 200);
    szse_index.pre_close_index = 10000.0;
    szse_index.open_index = 9990.0 + (_sequence % 20);
    szse_index.high_index = szse_index.last_index + (_sequence % 10);
    szse_index.low_index = szse_index.last_index - (_sequence % 10);
    szse_index.close_index = szse_index.last_index;

    szse_index.total_volume = _sequence * 20000;
    szse_index.total_value = _sequence * 100000000;
    szse_index.num_trades = _sequence * 200;
    szse_index.sequence = _sequence;

    _index_callback(szse_index);
}

void MockDataGenerator::generate_szse_depth_data()
{
    DepthData szse_depth = {};
    szse_depth.security_info.market = MARKET_SZSE;
    safe_copy_string(szse_depth.security_info.code, sizeof(szse_depth.security_info.code), _mock_codes.szse_stock_code);
    safe_copy_string(szse_depth.security_info.name, sizeof(szse_depth.security_info.name), _mock_codes.szse_stock_name);
    safe_copy_string(szse_depth.security_info.security_type, sizeof(szse_depth.security_info.security_type), "stock");

    szse_depth.timestamp = get_current_timestamp();
    szse_depth.update_time = _sequence * 1000;
    szse_depth.trading_phase = TRADING_PHASE_CONTINUOUS;

    szse_depth.last_price = 15.0 + (_sequence % 50) * 0.01;
    szse_depth.pre_close_price = 15.0;
    szse_depth.open_price = 14.95 + (_sequence % 10) * 0.01;
    szse_depth.high_price = szse_depth.last_price + (_sequence % 3) * 0.01;
    szse_depth.low_price = szse_depth.last_price - (_sequence % 3) * 0.01;
    szse_depth.close_price = szse_depth.last_price;
    szse_depth.upper_limit_price = 16.5;
    szse_depth.lower_limit_price = 13.5;
    szse_depth.avg_price = szse_depth.last_price;

    szse_depth.total_volume = _sequence * 800;
    szse_depth.total_value = szse_depth.last_price * szse_depth.total_volume;
    szse_depth.num_trades = _sequence * 8;
    szse_depth.total_bid_qty = _sequence * 400;
    szse_depth.total_ask_qty = _sequence * 400;
    szse_depth.weighted_avg_bid_price = szse_depth.last_price - 0.01;
    szse_depth.weighted_avg_ask_price = szse_depth.last_price + 0.01;
    szse_depth.total_bid_number = _sequence % 40;
    szse_depth.total_ask_number = _sequence % 40;

    // 生成买一卖一总价在1500万-3000万之间，较低频率命中2000万以下
    // 目标总价：1500万-3000万，较低频率(约20%)命中2000万以下
    uint64_t target_value;
    if (_sequence % 5 == 0)
    {
        // 20%的概率生成1500万-2000万之间的值
        target_value = 15000000 + (_sequence % 5000000); // 1500万-2000万
    }
    else
    {
        // 80%的概率生成2000万-3000万之间的值
        target_value = 20000000 + (_sequence % 10000000); // 2000万-3000万
    }

    for (int i = 0; i < 10; ++i)
    {
        szse_depth.bid_price[i] = szse_depth.last_price - (i + 1) * 0.01;
        szse_depth.ask_price[i] = szse_depth.last_price + (i + 1) * 0.01;

        if (i == 0)
        {
            // 买一和卖一使用计算出的数量
            szse_depth.bid_qty[i] = (uint64_t)(target_value / szse_depth.bid_price[i]);
            szse_depth.ask_qty[i] = (uint64_t)(target_value / szse_depth.ask_price[i]);
        }
        else
        {
            // 其他档位使用相对较小的数量
            szse_depth.bid_qty[i] = (i + 1) * 80000 + (_sequence % 40000);
            szse_depth.ask_qty[i] = (i + 1) * 80000 + (_sequence % 40000);
        }
    }

    szse_depth.channel_no = _sequence % 4 + 1; // 深交所通道号
    szse_depth.sequence = _sequence;
    _depth_callback(szse_depth);
}

void MockDataGenerator::generate_szse_entrust_data()
{
    EntrustData mock_entrust = {};
    mock_entrust.security_info.market = MARKET_SZSE;
    safe_copy_string(mock_entrust.security_info.code, sizeof(mock_entrust.security_info.code), _mock_codes.szse_stock_code);
    safe_copy_string(mock_entrust.security_info.security_type, sizeof(mock_entrust.security_info.security_type), "stock");

    mock_entrust.timestamp = get_current_timestamp();
    mock_entrust.transact_time = _sequence * 1000;

    mock_entrust.order_type = (_sequence % 3 == 0) ? 'A' : (_sequence % 3 == 1) ? 'D'
                                                                                : 'S'; // 新增/删除/状态
    mock_entrust.side = (_sequence % 2 == 0) ? 'B' : 'S';                              // 买或卖
    mock_entrust.bs_flag = mock_entrust.side;

    mock_entrust.order_no = 0;    // 深交所没有直接的订单号
    mock_entrust.order_index = 0; // 深交所没有订单索引
    mock_entrust.price = 15.0 + (_sequence % 30) * 0.01;
    mock_entrust.qty = (_sequence % 8 + 1) * 100;
    mock_entrust.balance = mock_entrust.qty;

    mock_entrust.channel_no = _sequence % 4 + 1;
    mock_entrust.biz_index = 0;                 // 深交所没有BizIndex
    mock_entrust.appl_seq_num = _sequence * 10; // 深交所特有的应用序列号
    mock_entrust.sequence = _sequence;

    _entrust_callback(mock_entrust);
}

void MockDataGenerator::generate_szse_trade_data()
{
    TradeData mock_trade = {};
    mock_trade.security_info.market = MARKET_SZSE;
    safe_copy_string(mock_trade.security_info.code, sizeof(mock_trade.security_info.code), _mock_codes.szse_stock_code);
    safe_copy_string(mock_trade.security_info.security_type, sizeof(mock_trade.security_info.security_type), "stock");

    mock_trade.timestamp = get_current_timestamp();
    mock_trade.transact_time = _sequence * 1000;

    mock_trade.trade_id = 0;    // 深交所没有直接的成交ID
    mock_trade.trade_index = 0; // 深交所没有成交索引
    mock_trade.price = 15.0 + (_sequence % 30) * 0.01;
    mock_trade.qty = (_sequence % 6 + 1) * 100;
    mock_trade.value = mock_trade.price * mock_trade.qty;

    mock_trade.bid_order_no = 0; // 深交所没有直接的订单号
    mock_trade.ask_order_no = 0;
    mock_trade.bid_appl_seq_num = _sequence * 10;     // 买方应用序列号
    mock_trade.ask_appl_seq_num = _sequence * 10 + 1; // 卖方应用序列号

    mock_trade.exec_type = 'F'; // 成交
    mock_trade.trade_flag = 0;  // 深交所没有交易标志

    mock_trade.channel_no = _sequence % 4 + 1;
    mock_trade.biz_index = 0; // 深交所没有BizIndex
    mock_trade.appl_seq_num = _sequence * 10;
    mock_trade.sequence = _sequence;

    _trade_callback(mock_trade);
}

void MockDataGenerator::generate_szse_depth_fifty_data()
{
    DepthFiftyData mock_depth_fifty = {};
    mock_depth_fifty.security_info.market = MARKET_SZSE;
    safe_copy_string(mock_depth_fifty.security_info.code, sizeof(mock_depth_fifty.security_info.code), _mock_codes.szse_stock_code);
    safe_copy_string(mock_depth_fifty.security_info.security_type, sizeof(mock_depth_fifty.security_info.security_type), "stock");

    mock_depth_fifty.timestamp = get_current_timestamp();
    mock_depth_fifty.update_time = _sequence * 1000;

    mock_depth_fifty.bid_price = 15.0 + (_sequence % 30) * 0.01;
    mock_depth_fifty.bid_qty = (_sequence % 12 + 1) * 1000;
    mock_depth_fifty.ask_price = mock_depth_fifty.bid_price + 0.01;
    mock_depth_fifty.ask_qty = (_sequence % 12 + 1) * 1000;

    mock_depth_fifty.bid_num_orders = _sequence % 50 + 1;
    mock_depth_fifty.ask_num_orders = _sequence % 50 + 1;
    mock_depth_fifty.bid_show_orders = std::min(50, (int)mock_depth_fifty.bid_num_orders);
    mock_depth_fifty.ask_show_orders = std::min(50, (int)mock_depth_fifty.ask_num_orders);

    // 生成50档委托队列数据
    for (int i = 0; i < 100; ++i)
    {
        mock_depth_fifty.orders[i] = (_sequence + i) % 1200 + 200; // 模拟委托量
    }

    mock_depth_fifty.channel_no = _sequence % 4 + 1;
    mock_depth_fifty.sequence = _sequence;
    _depth_fifty_callback(mock_depth_fifty);
}

// ===== CSV数据支持实现 =====

void MockDataGenerator::enable_csv_mode(bool enabled)
{
    _csv_mode_enabled = enabled;
}

bool MockDataGenerator::set_csv_file(const std::string &data_type, const std::string &csv_file_path)
{
    _csv_file_paths[data_type] = csv_file_path;
    return true;
}

bool MockDataGenerator::load_csv_data()
{
    if (!_csv_mode_enabled)
    {
        return false;
    }

    // 清空之前的数据
    _csv_index_records.clear();
    _csv_depth_records.clear();
    _csv_depth_fifty_records.clear();
    _csv_entrust_records.clear();
    _csv_trade_records.clear();

    // 重置索引
    _csv_index_current_index = 0;
    _csv_depth_current_index = 0;
    _csv_depth_fifty_current_index = 0;
    _csv_entrust_current_index = 0;
    _csv_trade_current_index = 0;

    bool loaded_any = false;

    // 加载指数数据CSV文件
    auto index_it = _csv_file_paths.find("index");
    if (index_it != _csv_file_paths.end())
    {
        if (_parse_csv_index_file(index_it->second))
        {
            std::cout << "Loaded " << _csv_index_records.size() << " index records from CSV" << std::endl;
            loaded_any = true;
        }
        else
        {
            std::cerr << "Failed to load index CSV file: " << index_it->second << std::endl;
        }
    }

    // 加载深度数据CSV文件
    auto depth_it = _csv_file_paths.find("depth");
    if (depth_it != _csv_file_paths.end())
    {
        if (_parse_csv_depth_file(depth_it->second))
        {
            std::cout << "Loaded " << _csv_depth_records.size() << " depth records from CSV" << std::endl;
            loaded_any = true;
        }
        else
        {
            std::cerr << "Failed to load depth CSV file: " << depth_it->second << std::endl;
        }
    }

    // 加载50档数据CSV文件
    auto depth_fifty_it = _csv_file_paths.find("depth_fifty");
    if (depth_fifty_it != _csv_file_paths.end())
    {
        if (_parse_csv_depth_fifty_file(depth_fifty_it->second))
        {
            std::cout << "Loaded " << _csv_depth_fifty_records.size() << " depth_fifty records from CSV" << std::endl;
            loaded_any = true;
        }
        else
        {
            std::cerr << "Failed to load depth_fifty CSV file: " << depth_fifty_it->second << std::endl;
        }
    }

    // 加载委托数据CSV文件
    auto entrust_it = _csv_file_paths.find("entrust");
    if (entrust_it != _csv_file_paths.end())
    {
        if (_parse_csv_entrust_file(entrust_it->second))
        {
            std::cout << "Loaded " << _csv_entrust_records.size() << " entrust records from CSV" << std::endl;
            loaded_any = true;
        }
        else
        {
            std::cerr << "Failed to load entrust CSV file: " << entrust_it->second << std::endl;
        }
    }

    // 加载成交数据CSV文件
    auto trade_it = _csv_file_paths.find("trade");
    if (trade_it != _csv_file_paths.end())
    {
        if (_parse_csv_trade_file(trade_it->second))
        {
            std::cout << "Loaded " << _csv_trade_records.size() << " trade records from CSV" << std::endl;
            loaded_any = true;
        }
        else
        {
            std::cerr << "Failed to load trade CSV file: " << trade_it->second << std::endl;
        }
    }

    return loaded_any;
}

bool MockDataGenerator::is_csv_mode_enabled() const
{
    return _csv_mode_enabled;
}

void MockDataGenerator::generate_csv_depth_data()
{
    if (!_depth_callback || _csv_depth_records.empty())
    {
        return;
    }

    // 循环播放CSV数据
    if (_csv_depth_current_index >= _csv_depth_records.size())
    {
        _csv_depth_current_index = 0;
    }

    const CSVDepthRecord &csv_record = _csv_depth_records[_csv_depth_current_index];

    // 转换CSV记录为DepthData结构
    DepthData mock_depth = {};
    mock_depth.security_info.market = csv_record.market;
    safe_copy_string(mock_depth.security_info.code, sizeof(mock_depth.security_info.code), csv_record.security_code.c_str());
    safe_copy_string(mock_depth.security_info.name, sizeof(mock_depth.security_info.name), csv_record.security_name.c_str());
    safe_copy_string(mock_depth.security_info.security_type, sizeof(mock_depth.security_info.security_type), csv_record.security_type.c_str());

    mock_depth.timestamp = csv_record.timestamp > 0 ? csv_record.timestamp : get_current_timestamp();
    mock_depth.update_time = csv_record.update_time > 0 ? csv_record.update_time : _sequence * 1000;
    mock_depth.trading_phase = csv_record.trading_phase;
    safe_copy_string(mock_depth.instrument_status, sizeof(mock_depth.instrument_status), csv_record.instrument_status.c_str());

    // 价格信息
    mock_depth.last_price = csv_record.last_price;
    mock_depth.pre_close_price = csv_record.pre_close_price;
    mock_depth.open_price = csv_record.open_price;
    mock_depth.high_price = csv_record.high_price;
    mock_depth.low_price = csv_record.low_price;
    mock_depth.close_price = csv_record.close_price;
    mock_depth.upper_limit_price = csv_record.upper_limit_price;
    mock_depth.lower_limit_price = csv_record.lower_limit_price;
    mock_depth.avg_price = csv_record.avg_price;

    // 成交信息
    mock_depth.total_volume = csv_record.total_volume;
    mock_depth.total_value = csv_record.total_value;
    mock_depth.num_trades = csv_record.num_trades;
    mock_depth.total_bid_qty = csv_record.total_bid_qty;
    mock_depth.total_ask_qty = csv_record.total_ask_qty;
    mock_depth.weighted_avg_bid_price = csv_record.weighted_avg_bid_price;
    mock_depth.weighted_avg_ask_price = csv_record.weighted_avg_ask_price;
    mock_depth.total_bid_number = csv_record.total_bid_number;
    mock_depth.total_ask_number = csv_record.total_ask_number;

    // 十档行情
    for (int i = 0; i < 10; ++i)
    {
        mock_depth.bid_price[i] = csv_record.bid_prices[i];
        mock_depth.bid_qty[i] = csv_record.bid_qtys[i];
        mock_depth.ask_price[i] = csv_record.ask_prices[i];
        mock_depth.ask_qty[i] = csv_record.ask_qtys[i];
    }

    // 其他字段
    mock_depth.total_position = csv_record.total_position;
    mock_depth.reference_price = csv_record.reference_price;
    mock_depth.iopv = csv_record.iopv;
    mock_depth.pre_iopv = csv_record.pre_iopv;
    mock_depth.channel_no = csv_record.channel_no;
    mock_depth.sequence = _sequence;

    // 推送数据
    _depth_callback(mock_depth);

    // 移动到下一条记录
    _csv_depth_current_index++;
}

bool MockDataGenerator::_parse_csv_depth_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Cannot open CSV file: " << file_path << std::endl;
        return false;
    }

    std::string line;
    bool is_header = true;

    while (std::getline(file, line))
    {
        // 跳过空行和注释行
        line = _trim_string(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        // 跳过表头
        if (is_header)
        {
            is_header = false;
            continue;
        }

        try
        {
            CSVDepthRecord record = _parse_csv_depth_line(line);
            _csv_depth_records.push_back(record);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing CSV line: " << line << " - " << e.what() << std::endl;
            continue;
        }
    }

    file.close();
    return !_csv_depth_records.empty();
}

MockDataGenerator::CSVDepthRecord MockDataGenerator::_parse_csv_depth_line(const std::string &line)
{
    std::vector<std::string> fields = _split_csv_line(line);

    CSVDepthRecord record;

    if (fields.size() >= 10)
    {
        record.security_code = _trim_string(fields[0]);
        record.security_name = _trim_string(fields[1]);
        record.security_type = fields.size() > 2 ? _trim_string(fields[2]) : "stock";
        record.market = _parse_market_type(_trim_string(fields[3]));
        record.timestamp = _safe_parse_uint64(fields[4]);
        record.update_time = _safe_parse_uint32(fields[5]);
        record.trading_phase = _parse_trading_phase(_trim_string(fields[6]));
        record.instrument_status = fields.size() > 7 ? _trim_string(fields[7]) : "0";

        // 价格信息 (假设字段顺序和CSV模板一致)
        if (fields.size() >= 18)
        {
            record.last_price = _safe_parse_double(fields[8]);
            record.pre_close_price = _safe_parse_double(fields[9]);
            record.open_price = _safe_parse_double(fields[10]);
            record.high_price = _safe_parse_double(fields[11]);
            record.low_price = _safe_parse_double(fields[12]);
            record.close_price = _safe_parse_double(fields[13]);
            record.upper_limit_price = _safe_parse_double(fields[14]);
            record.lower_limit_price = _safe_parse_double(fields[15]);
            record.avg_price = _safe_parse_double(fields[16]);
        }

        // 成交信息
        if (fields.size() >= 26)
        {
            record.total_volume = _safe_parse_uint64(fields[17]);
            record.total_value = _safe_parse_uint64(fields[18]);
            record.num_trades = _safe_parse_uint64(fields[19]);
            record.total_bid_qty = _safe_parse_uint64(fields[20]);
            record.total_ask_qty = _safe_parse_uint64(fields[21]);
            record.weighted_avg_bid_price = _safe_parse_double(fields[22]);
            record.weighted_avg_ask_price = _safe_parse_double(fields[23]);
            record.total_bid_number = _safe_parse_uint32(fields[24]);
            record.total_ask_number = _safe_parse_uint32(fields[25]);
        }

        // 十档行情 (bid_price_1, bid_qty_1, ..., ask_price_1, ask_qty_1, ...)
        if (fields.size() >= 66)
        {
            for (int i = 0; i < 10; ++i)
            {
                record.bid_prices[i] = _safe_parse_double(fields[26 + i * 2]);
                record.bid_qtys[i] = _safe_parse_uint64(fields[27 + i * 2]);
            }
            for (int i = 0; i < 10; ++i)
            {
                record.ask_prices[i] = _safe_parse_double(fields[46 + i * 2]);
                record.ask_qtys[i] = _safe_parse_uint64(fields[47 + i * 2]);
            }
        }

        // 其他字段
        if (fields.size() >= 71)
        {
            record.total_position = _safe_parse_uint64(fields[66]);
            record.reference_price = _safe_parse_double(fields[67]);
            record.iopv = _safe_parse_double(fields[68]);
            record.pre_iopv = _safe_parse_double(fields[69]);
            record.channel_no = _safe_parse_uint32(fields[70]);
            record.sequence = fields.size() > 71 ? _safe_parse_uint32(fields[71]) : 1;
        }
    }

    return record;
}

MarketType MockDataGenerator::_parse_market_type(const std::string &market_str)
{
    if (market_str == "sse" || market_str == "SSE")
    {
        return MARKET_SSE;
    }
    else if (market_str == "szse" || market_str == "SZSE")
    {
        return MARKET_SZSE;
    }
    return MARKET_SSE; // 默认值
}

TradingPhase MockDataGenerator::_parse_trading_phase(const std::string &phase_str)
{
    if (phase_str == "3" || phase_str == "continuous")
    {
        return TRADING_PHASE_CONTINUOUS;
    }
    else if (phase_str == "1" || phase_str == "before_open")
    {
        return TRADING_PHASE_BEFORE_OPEN;
    }
    else if (phase_str == "2" || phase_str == "open_auction")
    {
        return TRADING_PHASE_OPEN_AUCTION;
    }
    else if (phase_str == "4" || phase_str == "close_auction")
    {
        return TRADING_PHASE_CLOSE_AUCTION;
    }
    else if (phase_str == "5" || phase_str == "after_close")
    {
        return TRADING_PHASE_AFTER_CLOSE;
    }
    return TRADING_PHASE_CONTINUOUS; // 默认值
}

std::vector<std::string> MockDataGenerator::_split_csv_line(const std::string &line, char delimiter)
{
    std::vector<std::string> result;
    std::stringstream stream(line);
    std::string item;

    while (std::getline(stream, item, delimiter))
    {
        result.push_back(item);
    }

    return result;
}

std::string MockDataGenerator::_trim_string(const std::string &str)
{
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }

    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

double MockDataGenerator::_safe_parse_double(const std::string &str, double default_val)
{
    try
    {
        return std::stod(_trim_string(str));
    }
    catch (const std::exception &)
    {
        return default_val;
    }
}

uint64_t MockDataGenerator::_safe_parse_uint64(const std::string &str, uint64_t default_val)
{
    try
    {
        return std::stoull(_trim_string(str));
    }
    catch (const std::exception &)
    {
        return default_val;
    }
}

uint32_t MockDataGenerator::_safe_parse_uint32(const std::string &str, uint32_t default_val)
{
    try
    {
        return std::stoul(_trim_string(str));
    }
    catch (const std::exception &)
    {
        return default_val;
    }
}

char MockDataGenerator::_safe_parse_char(const std::string &str, char default_val)
{
    std::string trimmed = _trim_string(str);
    return trimmed.empty() ? default_val : trimmed[0];
}

uint8_t MockDataGenerator::_safe_parse_uint8(const std::string &str, uint8_t default_val)
{
    try
    {
        return static_cast<uint8_t>(std::stoul(_trim_string(str)));
    }
    catch (const std::exception &)
    {
        return default_val;
    }
}

// ===== 指数数据CSV支持 =====

void MockDataGenerator::generate_csv_index_data()
{
    if (!_index_callback || _csv_index_records.empty())
    {
        return;
    }

    // 循环播放CSV数据
    if (_csv_index_current_index >= _csv_index_records.size())
    {
        _csv_index_current_index = 0;
    }

    const CSVIndexRecord &csv_record = _csv_index_records[_csv_index_current_index];

    // 转换CSV记录为IndexData结构
    IndexData mock_index = {};
    mock_index.security_info.market = csv_record.market;
    safe_copy_string(mock_index.security_info.code, sizeof(mock_index.security_info.code), csv_record.security_code.c_str());
    safe_copy_string(mock_index.security_info.name, sizeof(mock_index.security_info.name), csv_record.security_name.c_str());

    mock_index.timestamp = csv_record.timestamp > 0 ? csv_record.timestamp : get_current_timestamp();
    mock_index.update_time = csv_record.update_time > 0 ? csv_record.update_time : _sequence * 1000;
    mock_index.trading_phase = csv_record.trading_phase;

    mock_index.last_index = csv_record.last_index;
    mock_index.pre_close_index = csv_record.pre_close_index;
    mock_index.open_index = csv_record.open_index;
    mock_index.high_index = csv_record.high_index;
    mock_index.low_index = csv_record.low_index;
    mock_index.close_index = csv_record.close_index;

    mock_index.total_volume = csv_record.total_volume;
    mock_index.total_value = csv_record.total_value;
    mock_index.num_trades = csv_record.num_trades;
    mock_index.sequence = _sequence;

    // 推送数据
    _index_callback(mock_index);

    // 移动到下一条记录
    _csv_index_current_index++;
}

bool MockDataGenerator::_parse_csv_index_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Cannot open CSV index file: " << file_path << std::endl;
        return false;
    }

    std::string line;
    bool is_header = true;

    while (std::getline(file, line))
    {
        line = _trim_string(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        if (is_header)
        {
            is_header = false;
            continue;
        }

        try
        {
            CSVIndexRecord record = _parse_csv_index_line(line);
            _csv_index_records.push_back(record);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing CSV index line: " << line << " - " << e.what() << std::endl;
            continue;
        }
    }

    file.close();
    return !_csv_index_records.empty();
}

MockDataGenerator::CSVIndexRecord MockDataGenerator::_parse_csv_index_line(const std::string &line)
{
    std::vector<std::string> fields = _split_csv_line(line);

    CSVIndexRecord record;

    if (fields.size() >= 16)
    {
        record.security_code = _trim_string(fields[0]);
        record.security_name = _trim_string(fields[1]);
        record.market = _parse_market_type(_trim_string(fields[2]));
        record.timestamp = _safe_parse_uint64(fields[3]);
        record.update_time = _safe_parse_uint32(fields[4]);
        record.trading_phase = _parse_trading_phase(_trim_string(fields[5]));

        record.last_index = _safe_parse_double(fields[6]);
        record.pre_close_index = _safe_parse_double(fields[7]);
        record.open_index = _safe_parse_double(fields[8]);
        record.high_index = _safe_parse_double(fields[9]);
        record.low_index = _safe_parse_double(fields[10]);
        record.close_index = _safe_parse_double(fields[11]);

        record.total_volume = _safe_parse_uint64(fields[12]);
        record.total_value = _safe_parse_uint64(fields[13]);
        record.num_trades = _safe_parse_uint64(fields[14]);
        record.sequence = _safe_parse_uint32(fields[15]);
    }

    return record;
}

// ===== 50档数据CSV支持 =====

void MockDataGenerator::generate_csv_depth_fifty_data()
{
    if (!_depth_fifty_callback || _csv_depth_fifty_records.empty())
    {
        return;
    }

    // 循环播放CSV数据
    if (_csv_depth_fifty_current_index >= _csv_depth_fifty_records.size())
    {
        _csv_depth_fifty_current_index = 0;
    }

    const CSVDepthFiftyRecord &csv_record = _csv_depth_fifty_records[_csv_depth_fifty_current_index];

    // 转换CSV记录为DepthFiftyData结构
    DepthFiftyData mock_depth_fifty = {};
    mock_depth_fifty.security_info.market = csv_record.market;
    safe_copy_string(mock_depth_fifty.security_info.code, sizeof(mock_depth_fifty.security_info.code), csv_record.security_code.c_str());
    safe_copy_string(mock_depth_fifty.security_info.name, sizeof(mock_depth_fifty.security_info.name), csv_record.security_name.c_str());
    safe_copy_string(mock_depth_fifty.security_info.security_type, sizeof(mock_depth_fifty.security_info.security_type), csv_record.security_type.c_str());

    mock_depth_fifty.timestamp = csv_record.timestamp > 0 ? csv_record.timestamp : get_current_timestamp();
    mock_depth_fifty.update_time = csv_record.update_time > 0 ? csv_record.update_time : _sequence * 1000;

    mock_depth_fifty.bid_price = csv_record.bid_price;
    mock_depth_fifty.bid_qty = csv_record.bid_qty;
    mock_depth_fifty.bid_num_orders = csv_record.bid_num_orders;
    mock_depth_fifty.bid_show_orders = csv_record.bid_show_orders;

    mock_depth_fifty.ask_price = csv_record.ask_price;
    mock_depth_fifty.ask_qty = csv_record.ask_qty;
    mock_depth_fifty.ask_num_orders = csv_record.ask_num_orders;
    mock_depth_fifty.ask_show_orders = csv_record.ask_show_orders;

    // 复制委托队列数据
    for (int i = 0; i < 100; ++i)
    {
        mock_depth_fifty.orders[i] = csv_record.orders[i];
    }

    mock_depth_fifty.channel_no = csv_record.channel_no;
    mock_depth_fifty.sequence = _sequence;

    // 推送数据
    _depth_fifty_callback(mock_depth_fifty);

    // 移动到下一条记录
    _csv_depth_fifty_current_index++;
}

bool MockDataGenerator::_parse_csv_depth_fifty_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Cannot open CSV depth fifty file: " << file_path << std::endl;
        return false;
    }

    std::string line;
    bool is_header = true;

    while (std::getline(file, line))
    {
        line = _trim_string(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        if (is_header)
        {
            is_header = false;
            continue;
        }

        try
        {
            CSVDepthFiftyRecord record = _parse_csv_depth_fifty_line(line);
            _csv_depth_fifty_records.push_back(record);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing CSV depth fifty line: " << line << " - " << e.what() << std::endl;
            continue;
        }
    }

    file.close();
    return !_csv_depth_fifty_records.empty();
}

MockDataGenerator::CSVDepthFiftyRecord MockDataGenerator::_parse_csv_depth_fifty_line(const std::string &line)
{
    std::vector<std::string> fields = _split_csv_line(line);

    CSVDepthFiftyRecord record;

    if (fields.size() >= 15)
    {
        record.security_code = _trim_string(fields[0]);
        record.security_name = _trim_string(fields[1]);
        record.security_type = _trim_string(fields[2]);
        record.market = _parse_market_type(_trim_string(fields[3]));
        record.timestamp = _safe_parse_uint64(fields[4]);
        record.update_time = _safe_parse_uint32(fields[5]);

        record.bid_price = _safe_parse_double(fields[6]);
        record.bid_qty = _safe_parse_uint64(fields[7]);
        record.bid_num_orders = _safe_parse_uint32(fields[8]);
        record.bid_show_orders = _safe_parse_uint8(fields[9]);

        record.ask_price = _safe_parse_double(fields[10]);
        record.ask_qty = _safe_parse_uint64(fields[11]);
        record.ask_num_orders = _safe_parse_uint32(fields[12]);
        record.ask_show_orders = _safe_parse_uint8(fields[13]);

        // 解析orders字段（第15个字段，可能是逗号分隔的字符串）
        if (fields.size() > 14)
        {
            std::string orders_str = _trim_string(fields[14]);
            if (orders_str.front() == '"' && orders_str.back() == '"')
            {
                orders_str = orders_str.substr(1, orders_str.length() - 2);
            }

            std::vector<std::string> order_values = _split_csv_line(orders_str);
            for (size_t i = 0; i < std::min(order_values.size(), static_cast<size_t>(100)); ++i)
            {
                record.orders[i] = _safe_parse_uint32(order_values[i]);
            }
        }

        record.channel_no = fields.size() > 15 ? _safe_parse_uint32(fields[15]) : 0;
        record.sequence = fields.size() > 16 ? _safe_parse_uint32(fields[16]) : 1;
    }

    return record;
}

// ===== 委托数据CSV支持 =====

void MockDataGenerator::generate_csv_entrust_data()
{
    if (!_entrust_callback || _csv_entrust_records.empty())
    {
        return;
    }

    // 循环播放CSV数据
    if (_csv_entrust_current_index >= _csv_entrust_records.size())
    {
        _csv_entrust_current_index = 0;
    }

    const CSVEntrustRecord &csv_record = _csv_entrust_records[_csv_entrust_current_index];

    // 转换CSV记录为EntrustData结构
    EntrustData mock_entrust = {};
    mock_entrust.security_info.market = csv_record.market;
    safe_copy_string(mock_entrust.security_info.code, sizeof(mock_entrust.security_info.code), csv_record.security_code.c_str());
    safe_copy_string(mock_entrust.security_info.name, sizeof(mock_entrust.security_info.name), csv_record.security_name.c_str());
    safe_copy_string(mock_entrust.security_info.security_type, sizeof(mock_entrust.security_info.security_type), csv_record.security_type.c_str());

    mock_entrust.timestamp = csv_record.timestamp > 0 ? csv_record.timestamp : get_current_timestamp();
    mock_entrust.transact_time = csv_record.transact_time > 0 ? csv_record.transact_time : _sequence * 1000;

    mock_entrust.order_type = csv_record.order_type;
    mock_entrust.side = csv_record.side;
    mock_entrust.bs_flag = csv_record.bs_flag;

    mock_entrust.order_no = csv_record.order_no;
    mock_entrust.order_index = csv_record.order_index;
    mock_entrust.price = csv_record.price;
    mock_entrust.qty = csv_record.qty;
    mock_entrust.balance = csv_record.balance;

    mock_entrust.channel_no = csv_record.channel_no;
    mock_entrust.biz_index = csv_record.biz_index;
    mock_entrust.appl_seq_num = csv_record.appl_seq_num;
    mock_entrust.sequence = _sequence;

    // 推送数据
    _entrust_callback(mock_entrust);

    // 移动到下一条记录
    _csv_entrust_current_index++;
}

bool MockDataGenerator::_parse_csv_entrust_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Cannot open CSV entrust file: " << file_path << std::endl;
        return false;
    }

    std::string line;
    bool is_header = true;

    while (std::getline(file, line))
    {
        line = _trim_string(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        if (is_header)
        {
            is_header = false;
            continue;
        }

        try
        {
            CSVEntrustRecord record = _parse_csv_entrust_line(line);
            _csv_entrust_records.push_back(record);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing CSV entrust line: " << line << " - " << e.what() << std::endl;
            continue;
        }
    }

    file.close();
    return !_csv_entrust_records.empty();
}

MockDataGenerator::CSVEntrustRecord MockDataGenerator::_parse_csv_entrust_line(const std::string &line)
{
    std::vector<std::string> fields = _split_csv_line(line);

    CSVEntrustRecord record;

    if (fields.size() >= 18)
    {
        record.security_code = _trim_string(fields[0]);
        record.security_name = _trim_string(fields[1]);
        record.security_type = _trim_string(fields[2]);
        record.market = _parse_market_type(_trim_string(fields[3]));
        record.timestamp = _safe_parse_uint64(fields[4]);
        record.transact_time = _safe_parse_uint64(fields[5]);

        record.order_type = _safe_parse_char(fields[6], 'A');
        record.side = _safe_parse_char(fields[7], 'B');
        record.bs_flag = _safe_parse_char(fields[8], record.side);

        record.order_no = _safe_parse_uint64(fields[9]);
        record.order_index = _safe_parse_uint32(fields[10]);
        record.price = _safe_parse_double(fields[11]);
        record.qty = _safe_parse_uint64(fields[12]);
        record.balance = _safe_parse_uint64(fields[13]);

        record.channel_no = _safe_parse_uint32(fields[14]);
        record.biz_index = _safe_parse_uint64(fields[15]);
        record.appl_seq_num = _safe_parse_uint64(fields[16]);
        record.sequence = _safe_parse_uint32(fields[17]);
    }

    return record;
}

// ===== 成交数据CSV支持 =====

void MockDataGenerator::generate_csv_trade_data()
{
    if (!_trade_callback || _csv_trade_records.empty())
    {
        return;
    }

    // 循环播放CSV数据
    if (_csv_trade_current_index >= _csv_trade_records.size())
    {
        _csv_trade_current_index = 0;
    }

    const CSVTradeRecord &csv_record = _csv_trade_records[_csv_trade_current_index];

    // 转换CSV记录为TradeData结构
    TradeData mock_trade = {};
    mock_trade.security_info.market = csv_record.market;
    safe_copy_string(mock_trade.security_info.code, sizeof(mock_trade.security_info.code), csv_record.security_code.c_str());
    safe_copy_string(mock_trade.security_info.name, sizeof(mock_trade.security_info.name), csv_record.security_name.c_str());
    safe_copy_string(mock_trade.security_info.security_type, sizeof(mock_trade.security_info.security_type), csv_record.security_type.c_str());

    mock_trade.timestamp = csv_record.timestamp > 0 ? csv_record.timestamp : get_current_timestamp();
    mock_trade.transact_time = csv_record.transact_time > 0 ? csv_record.transact_time : _sequence * 1000;

    mock_trade.trade_id = csv_record.trade_id;
    mock_trade.trade_index = csv_record.trade_index;
    mock_trade.price = csv_record.price;
    mock_trade.qty = csv_record.qty;
    mock_trade.value = csv_record.value;

    mock_trade.bid_order_no = csv_record.bid_order_no;
    mock_trade.ask_order_no = csv_record.ask_order_no;
    mock_trade.bid_appl_seq_num = csv_record.bid_appl_seq_num;
    mock_trade.ask_appl_seq_num = csv_record.ask_appl_seq_num;

    mock_trade.exec_type = csv_record.exec_type;
    mock_trade.trade_flag = csv_record.trade_flag;

    mock_trade.channel_no = csv_record.channel_no;
    mock_trade.biz_index = csv_record.biz_index;
    mock_trade.appl_seq_num = csv_record.appl_seq_num;
    mock_trade.sequence = _sequence;

    // 推送数据
    _trade_callback(mock_trade);

    // 移动到下一条记录
    _csv_trade_current_index++;
}

bool MockDataGenerator::_parse_csv_trade_file(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Cannot open CSV trade file: " << file_path << std::endl;
        return false;
    }

    std::string line;
    bool is_header = true;

    while (std::getline(file, line))
    {
        line = _trim_string(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        if (is_header)
        {
            is_header = false;
            continue;
        }

        try
        {
            CSVTradeRecord record = _parse_csv_trade_line(line);
            _csv_trade_records.push_back(record);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing CSV trade line: " << line << " - " << e.what() << std::endl;
            continue;
        }
    }

    file.close();
    return !_csv_trade_records.empty();
}

MockDataGenerator::CSVTradeRecord MockDataGenerator::_parse_csv_trade_line(const std::string &line)
{
    std::vector<std::string> fields = _split_csv_line(line);

    CSVTradeRecord record;

    if (fields.size() >= 21)
    {
        record.security_code = _trim_string(fields[0]);
        record.security_name = _trim_string(fields[1]);
        record.security_type = _trim_string(fields[2]);
        record.market = _parse_market_type(_trim_string(fields[3]));
        record.timestamp = _safe_parse_uint64(fields[4]);
        record.transact_time = _safe_parse_uint64(fields[5]);

        record.trade_id = _safe_parse_uint64(fields[6]);
        record.trade_index = _safe_parse_uint32(fields[7]);
        record.price = _safe_parse_double(fields[8]);
        record.qty = _safe_parse_uint64(fields[9]);
        record.value = _safe_parse_uint64(fields[10]);

        record.bid_order_no = _safe_parse_uint64(fields[11]);
        record.ask_order_no = _safe_parse_uint64(fields[12]);
        record.bid_appl_seq_num = _safe_parse_uint64(fields[13]);
        record.ask_appl_seq_num = _safe_parse_uint64(fields[14]);

        record.exec_type = _safe_parse_char(fields[15], 'F');
        record.trade_flag = _safe_parse_char(fields[16], 0);

        record.channel_no = _safe_parse_uint32(fields[17]);
        record.biz_index = _safe_parse_uint64(fields[18]);
        record.appl_seq_num = _safe_parse_uint64(fields[19]);
        record.sequence = _safe_parse_uint32(fields[20]);
    }

    return record;
}

NS_FT_END