#pragma once

#include "base_api_handler.h"
#include "types/trade_types.h"
#include "types/quote_types.h"
#include "utils/url_parser.h"
#include <memory>

NS_FT_BEGIN

// 前向声明
class TwapAlgorithm;
class TradingTimeManager;
class IQuoteParser;
class ITradeHandler;

namespace network
{

    /**
     * @brief 交易API处理器
     *
     * 提供简化的股票交易接口，只需要登陆账号和股票代码即可完成交易操作。
     * 自动从行情服务获取实时价格和市场信息，统一使用限价单。
     *
     * 功能范围：
     * - 股票买入（输入总金额，根据实时价格计算股数）
     * - 股票卖出（输入股数，使用实时价格）
     * - TWAP拆单卖出（智能拆单算法）
     * - 订单撤销
     * - 银证转账
     * - 市场间资金转账
     *
     * 路径前缀：/trade
     */
    class TradeApiHandler : public BaseApiHandler
    {
    public:
        /**
         * @brief 构造交易API处理器
         */
        TradeApiHandler();

        /**
         * @brief 析构函数
         */
        ~TradeApiHandler() override;

        /**
         * @brief 注册交易相关路由
         *
         * @param service 网络服务实例指针，用于注册路由
         */
        void register_routes(NetworkService *service) override;

        /**
         * @brief 设置行情解析器依赖（从外部注入）
         * @param quote_parser 行情解析器实例
         */
        void set_quote_parser(IQuoteParser *quote_parser);

        /**
         * @brief 设置交易处理器依赖（从外部注入）
         * @param trade_handler 交易处理器实例
         */
        void set_trade_handler(ITradeHandler *trade_handler);

        /**
         * @brief 设置交易时间管理器依赖（从外部注入）
         * @param trading_time_manager 交易时间管理器实例
         */
        void set_trading_time_manager(TradingTimeManager *trading_time_manager);

        /**
         * @brief 手动初始化并启动TWAP算法（当所有依赖都设置后调用）
         */
        void init_and_start_twap();

    private:
        // ===== 交易API处理函数 =====

        /**
         * @brief 处理股票买入请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "security_code": "000001",
         *   "amount": 10000.0,
         *   "chasing_rise_halfway": false,  // 可选，是否启用追涨用的价格笼子
         *   "price": 15.50  // 可选，指定价格(忽略价格笼子)
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含下单结果
         */
        HttpResponse handle_buy_stock(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理股票卖出请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "security_code": "000001",
         *   "quantity": 1000,
         *   "price": 15.50  // 可选，指定价格
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含下单结果
         */
        HttpResponse handle_sell_stock(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理撤单请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "market": 1,
         *   "order_id": 123456
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含撤单结果
         */
        HttpResponse handle_cancel_order(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理银证转账请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "direction": 1,               // 转账方向：1-银行转证券，2-证券转银行
         *   "market": 1,                  // 市场代码（必填）：1-上海，2-深圳
         *   "transfer_amount": 10000.0,   // 转账金额
         *   "currency": "CNY",            // 货币类型（可选，默认CNY）
         *   "bank_password": "CHANGE_ME",    // 银行密码（可选）
         *   "fund_password": "CHANGE_ME",    // 资金密码（可选）
         *   "remark": "银证转账"           // 备注（可选）
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含转账结果
         */
        HttpResponse handle_bank_securities_transfer(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理市场间资金转账请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "from_market": 1,             // 转出市场：1-上海，2-深圳
         *   "to_market": 2,               // 转入市场：1-上海，2-深圳
         *   "transfer_amount": 5000.0,    // 转账金额
         *   "remark": "市场间转账"         // 备注（可选）
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含转账结果
         */
        HttpResponse handle_fund_transfer(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理TWAP拆单卖出请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "security_code": "000001",
         *   "quantity": 10000,
         *   "start_time": "2024-01-15T09:30:00",     // 可选，ISO8601格式，默认当前时间
         *   "end_time": "2024-01-15T09:40:00",       // 可选，ISO8601格式，默认start_time+10分钟
         *   "interval_seconds": 10,                  // 可选，默认10秒
         *   "enable_price_threshold": false,         // 可选，默认false
         *   "price_threshold_ratio": 0.05            // 可选，默认5%
         * }
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含TWAP任务信息
         */
        HttpResponse handle_twap_sell(const HttpRequest &req, const UrlParseResult& url_result);

        /**
         * @brief 处理TWAP任务查询请求
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含任务状态信息
         */
        HttpResponse handle_twap_task_status(const HttpRequest &req, const UrlParseResult &url_result);

        /**
         * @brief 处理TWAP任务取消请求
         *
         * @param req HTTP请求对象
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应对象，包含取消结果
         */
        HttpResponse handle_twap_cancel_task(const HttpRequest &req, const UrlParseResult &url_result);

        // ===== 查询处理方法 =====

        /**
         * 处理委托查询请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "market": 1,
         *   "security_code": "000001",
         *   "order_id": 123456,
         *   "condition": 0,
         *   "sort": 0,
         *   "limit": 100
         * }
         *
         * @param req HTTP请求
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应
         */
        HttpResponse handle_query_orders(const HttpRequest &req, const UrlParseResult &url_result);

        /**
         * 处理成交查询请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "market": 1,
         *   "security_code": "000001",
         *   "order_id": 123456,
         *   "condition": 0,
         *   "sort": 0,
         *   "limit": 100
         * }
         *
         * @param req HTTP请求
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应
         */
        HttpResponse handle_query_trades(const HttpRequest &req, const UrlParseResult &url_result);

        /**
         * 处理持仓查询请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "market": 1,
         *   "security_code": "000001"
         *   "limit": 100
         * }
         *
         * @param req HTTP请求
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应
         */
        HttpResponse handle_query_positions(const HttpRequest &req, const UrlParseResult &url_result);

        /**
         * 处理资金查询请求
         *
         * 请求体：{
         *   "login_id": "DEMO_CUSTOMER",
         *   "market": 1,
         *   "currency": "CNY"
         * }
         *
         * @param req HTTP请求
         * @param url_result 已解析的URL信息（包含查询参数）
         * @return HTTP响应
         */
        HttpResponse handle_query_funds(const HttpRequest &req, const UrlParseResult &url_result);

        // ===== 私有辅助方法 =====

        /**
         * @brief 根据登陆账号获取账户信息
         *
         * @param login_id 登陆账号
         * @param account 输出的账户信息
         * @return true 成功，false 失败
         */
        bool get_account_by_login_id(const std::string &login_id, AccountInfo &account);

        /**
         * @brief 从行情服务获取股票完整深度数据
         *
         * @param security_code 股票代码
         * @param depth_data 输出的完整深度行情数据
         * @return true 成功，false 失败
         */
        bool get_stock_info_from_quote(const std::string &security_code, DepthData &depth_data);

        /**
         * @brief 构造交易订单请求
         *
         * @param account 账户信息
         * @param security_code 股票代码
         * @param market 市场类型
         * @param side 买卖方向
         * @param quantity 股数
         * @param price 价格
         * @return 构造的订单请求
         */
        TradeOrderRequest build_order_request(
            const AccountInfo &account,
            const std::string &security_code,
            MarketType market,
            OrderSide side,
            int64_t quantity,
            double price);

        /**
         * @brief 解析ISO8601时间字符串
         * @param time_str ISO8601格式时间字符串
         * @return 时间点，解析失败返回epoch时间
         */
        std::chrono::system_clock::time_point parse_iso8601_time(const std::string &time_str);

        /**
         * @brief 初始化TWAP算法
         * @return true成功，false失败
         */
        bool init_twap_algorithm();

        /**
         * @brief 启动TWAP算法
         * @return true成功，false失败
         */
        bool start_twap_algorithm();

        /**
         * @brief 停止TWAP算法
         */
        void stop_twap_algorithm();

        /**
         * @brief 检查TWAP算法是否可用
         * @return true可用，false不可用
         */
        bool is_twap_available() const;

    private:
        static constexpr int DEFAULT_TIMEOUT_MS = 30000; ///< 默认超时时间（毫秒）

        IQuoteParser* _quote_parser = nullptr;
        ITradeHandler* _trade_handler = nullptr;
        TradingTimeManager *_trading_time_manager = nullptr;
        std::unique_ptr<TwapAlgorithm> _twap_algorithm;
        bool _twap_initialized = false;
    };

} // namespace network

NS_FT_END