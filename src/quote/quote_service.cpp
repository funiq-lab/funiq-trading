#include "quote/quote_service.h"
#include "quote/quote_parser_adapter.h"
#include <fstream>
#include "logger/logger.hpp"
#include "laserpants/dotenv/dotenv.h"

NS_FT_BEGIN

QuoteService::QuoteService() 
    : _running(false), _initialized(false) {
    dotenv::init();
    QUOTE_LOG_INFO("QuoteService created");
}

QuoteService::~QuoteService() noexcept {
    try {
        release();
        QUOTE_LOG_INFO("QuoteService destroyed");
    } catch (const std::exception& e) {
        QUOTE_LOG_ERROR("Exception in destructor: {}", e.what());
    }
}

int QuoteService::init() {
   std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (_initialized.load()) {
        // 已经初始化，直接返回成功
        QUOTE_LOG_INFO("Service already initialized");
        return 0;
    }
    
    try {
        // 从环境变量获取配置文件路径，使用 dotenv::getenv 提供默认值
        std::string env_config_file = dotenv::getenv("QUOTE_CONFIG_FILE", "");
        if(env_config_file.empty()) {
            _config_file = "./quote_config.json";
            QUOTE_LOG_INFO("Using default quote config file: {}", _config_file);
        } else {
            _config_file = env_config_file;
            QUOTE_LOG_INFO("Using quote config file from environment: {}", _config_file);
        }
        
        // 加载配置文件，找到active的parser配置
        nlohmann::json config;
        if (!_load_config(_config_file.c_str(), config)) {
            return -1;
        }
        
        nlohmann::json* active_parser = _find_active_parser_config(config);
        if (!active_parser) {
            _last_error = "No active parser found in config";
            QUOTE_LOG_ERROR(_last_error);
            return -1;
        }
        
        // 获取解析器ID
        if (active_parser->contains("id") && active_parser->contains("id")) {
            _parser_id = active_parser->at("id").get<std::string>();
        } else {
            _parser_id = "default_parser";
        }
        
        // 创建 QuoteParserAdapter 实例
        _parser_adapter = std::make_unique<QuoteParserAdapter>();
        
        // 初始化 QuoteParserAdapter
        if (!_parser_adapter->init(_parser_id.c_str(), active_parser)) {
            _last_error = "Failed to initialize parser adapter: " + std::string(_parser_adapter->get_last_error());
            QUOTE_LOG_ERROR(_last_error);
            _parser_adapter.reset();
            return -1;
        }
        
        _initialized.store(true);
        QUOTE_LOG_INFO("QuoteService initialized successfully with parser: {}", _parser_id);
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during initialization: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return -1;
    }
}

int QuoteService::start() {
   std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (!_initialized.load()) {
        _last_error = "Service not initialized";
        QUOTE_LOG_ERROR(_last_error);
        return -1;
    }
    
    if (_running.load()) {
        // 已经在运行，直接返回成功
        QUOTE_LOG_INFO("Service already running");
        return 0;
    }
    
    try {
        // 启动解析器
        if (!_parser_adapter->run()) {
            _last_error = "Failed to start parser: " + std::string(_parser_adapter->get_last_error());
            QUOTE_LOG_ERROR(_last_error);
            return -1;
        }
        
        _running.store(true);
        QUOTE_LOG_INFO("QuoteService started successfully");
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during start: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return -1;
    }
}

int QuoteService::stop() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (!_running.load()) {
        QUOTE_LOG_INFO("Service already stopped");
        return 0;
    }
    
    try {
        // 通过adapter正确停止parser
        if (_parser_adapter) {
            if (!_parser_adapter->stop()) {
                _last_error = "Failed to stop parser: " + std::string(_parser_adapter->get_last_error());
                QUOTE_LOG_ERROR(_last_error);
                return -1;
            }
        }
        
        _running.store(false);
        QUOTE_LOG_INFO("QuoteService stopped");
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during stop: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return -1;
    }
}

void QuoteService::release() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    try {
        if (_running.load()) {
            // 先停止服务，但不重置初始化状态
            if (_parser_adapter) {
                _parser_adapter->stop();
            }
            _running.store(false);
        }
        
        // 只有在真正需要完全清理时才调用release
        if (_parser_adapter) {
            _parser_adapter->release();
            _parser_adapter.reset();
        }
        
        _initialized.store(false);
        QUOTE_LOG_INFO("QuoteService release completed");
        
    } catch (const std::exception& e) {
        QUOTE_LOG_ERROR("Exception during release: {}", e.what());
    }
}

bool QuoteService::_load_config(const char* config_file, nlohmann::json& config) {
    try {
        // 读取JSON文件
        std::ifstream file(config_file);
        if (!file.is_open()) {
            _last_error = "Cannot open config file: " + std::string(config_file);
            QUOTE_LOG_ERROR(_last_error);
            return false;
        }
        
        // 使用nlohmann::json解析
        file >> config;
        file.close();
        
        QUOTE_LOG_INFO("Config file loaded successfully: {}", config_file);
        return true;
        
    } catch (const nlohmann::json::parse_error& e) {
        _last_error = "JSON parse error: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    } catch (const std::exception& e) {
        _last_error = "Exception loading config: " + std::string(e.what());
        QUOTE_LOG_ERROR(_last_error);
        return false;
    }
}

nlohmann::json* QuoteService::_find_active_parser_config(const nlohmann::json& config) {
    if (!config.contains("parsers") || !config["parsers"].is_array()) {
        return nullptr;
    }
    
    const auto& parsers = config["parsers"];
    
    for (auto& parser_config : parsers) {
        if (parser_config.is_object() && 
            parser_config.contains("active") && 
            parser_config["active"].is_boolean() &&
            parser_config["active"].get<bool>()) {
            
            // 返回第一个active的解析器
            return const_cast<nlohmann::json*>(&parser_config);
        }
    }
    
    return nullptr;
}


IQuoteParser* QuoteService::get_parser() const {
   std::shared_lock<std::shared_mutex> lock(_mutex);
    return _parser_adapter ? _parser_adapter->get_parser() : nullptr;
}

bool QuoteService::is_running() const {
    return _running.load();
}

const char* QuoteService::get_last_error() const {
   std::shared_lock<std::shared_mutex> lock(_mutex);
    return _last_error.c_str();
}

const std::string& QuoteService::get_parser_id() const {
   std::shared_lock<std::shared_mutex> lock(_mutex);
    return _parser_id;
}

// 全局服务实例
static std::unique_ptr<QuoteService> g_quote_service;
static std::shared_mutex g_service_mutex;

NS_FT_END

// ===== C接口实现 =====
extern "C" {

// ===== 生命周期管理接口 =====
int quote_init() {
   std::unique_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        ft::g_quote_service = std::make_unique<ft::QuoteService>();
    }
    
    return ft::g_quote_service->init();
}

int quote_start() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    return ft::g_quote_service->start();
}

int quote_stop() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return 0;
    }
    
    return ft::g_quote_service->stop();
}

void quote_release() {
   std::unique_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (ft::g_quote_service) {
        ft::g_quote_service->release();
        ft::g_quote_service.reset();
    }
}

void* quote_get_parser() {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    return ft::g_quote_service ? ft::g_quote_service->get_parser() : nullptr;
}

// ===== 状态查询接口 =====
int quote_is_running() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return 0;
    }
    
    return ft::g_quote_service->is_running() ? 1 : 0;
}

int quote_is_connected() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return 0;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return 0;
    }
    
    return parser->is_connected() ? 1 : 0;
}

Status quote_get_status() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return STATUS_ERROR;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return STATUS_ERROR;
    }
    
    return parser->get_status();
}

const char* quote_get_last_error() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return "Service not initialized";
    }
    
    return ft::g_quote_service->get_last_error();
}

const char* quote_get_parser_id() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return nullptr;
    }
    
    return ft::g_quote_service->get_parser_id().c_str();
}

// ===== 订阅管理接口 =====
int quote_subscribe(const SubscriptionConfig* config) {
    if (!config) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->subscribe(config) : -1;
}

int quote_unsubscribe() {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->unsubscribe() : -1;
}

int quote_add_markets(MarketType* markets, int count) {
    if (!markets || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->add_markets(markets, count) : -1;
}

int quote_add_data_types(FTDataType* data_types, int count) {
    if (!data_types || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->add_data_types(data_types, count) : -1;
}

int quote_add_securities(SecurityInfo* securities, int count) {
    if (!securities || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->add_securities(securities, count) : -1;
}

int quote_remove_markets(MarketType* markets, int count) {
    if (!markets || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->remove_markets(markets, count) : -1;
}

int quote_remove_data_types(FTDataType* data_types, int count) {
    if (!data_types || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->remove_data_types(data_types, count) : -1;
}

int quote_remove_securities(SecurityInfo* securities, int count) {
    if (!securities || count <= 0) return -1;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return -1;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return parser ? parser->remove_securities(securities, count) : -1;
}

// ===== 订阅状态查询接口 =====
int quote_is_market_subscribed(MarketType market) {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return 0;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return (parser && parser->is_market_subscribed(market)) ? 1 : 0;
}

int quote_is_data_type_subscribed(FTDataType data_type) {
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return 0;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return (parser && parser->is_data_type_subscribed(data_type)) ? 1 : 0;
}

int quote_is_code_subscribed(const char* code) {
    if (!code) return 0;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return 0;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return (parser && parser->is_code_subscribed(code)) ? 1 : 0;
}

int quote_should_process_data(MarketType market, FTDataType data_type, const char* code) {
    if (!code) return 0;
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    if (!ft::g_quote_service) return 0;
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    return (parser && parser->should_process_data(market, data_type, code)) ? 1 : 0;
}

// ===== 回调函数注册接口 =====
IndexDataCallbackHandle quote_set_index_data_callback(IndexDataCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return IndexDataCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return IndexDataCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_index_data_callback(callback);
}

int quote_remove_index_data_callback(IndexDataCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_index_data_callback(handle);
}

DepthDataCallbackHandle quote_set_depth_data_callback(DepthDataCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return DepthDataCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return DepthDataCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_depth_data_callback(callback);
}

int quote_remove_depth_data_callback(DepthDataCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_depth_data_callback(handle);
}

DepthFiftyDataCallbackHandle quote_set_depth_fifty_data_callback(DepthFiftyDataCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return DepthFiftyDataCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return DepthFiftyDataCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_depth_fifty_data_callback(callback);
}

int quote_remove_depth_fifty_data_callback(DepthFiftyDataCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_depth_fifty_data_callback(handle);
}

EntrustDataCallbackHandle quote_set_entrust_data_callback(EntrustDataCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return EntrustDataCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return EntrustDataCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_entrust_data_callback(callback);
}

int quote_remove_entrust_data_callback(EntrustDataCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_entrust_data_callback(handle);
}

TradeDataCallbackHandle quote_set_trade_data_callback(TradeDataCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return TradeDataCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return TradeDataCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_trade_data_callback(callback);
}

int quote_remove_trade_data_callback(TradeDataCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_trade_data_callback(handle);
}

StatusCallbackHandle quote_set_status_callback(QuoteStatusCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return StatusCallbackHandle{}; // 返回空句柄
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return StatusCallbackHandle{}; // 返回空句柄
    }
    
    return parser->set_status_callback(callback);
}

int quote_remove_status_callback(StatusCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->remove_status_callback(handle);
}

// ===== 数据获取接口 =====
int quote_get_index_data(IndexData* data) {
    if (!data) {
        return -1;
    }
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_index_data(data);
}

int quote_get_depth_data(DepthData* data) {
    if (!data) {
        return -1;
    }
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_depth_data(data);
}

int quote_get_depth_fifty_data(DepthFiftyData* data) {
    if (!data) {
        return -1;
    }
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_depth_fifty_data(data);
}

int quote_get_entrust_data(EntrustData* data) {
    if (!data) {
        return -1;
    }
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_entrust_data(data);
}

int quote_get_trade_data(TradeData* data) {
    if (!data) {
        return -1;
    }
    
   std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_trade_data(data);
}

int quote_get_index_data_by_code(const char* code, IndexData* data) {
    if (!code || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_index_data_by_code(code, data);
}

int quote_get_depth_data_by_code(const char* code, DepthData* data) {
    if (!code || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_depth_data_by_code(code, data);
}

int quote_get_depth_fifty_data_by_code(const char* code, DepthFiftyData* data) {
    if (!code || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_depth_fifty_data_by_code(code, data);
}

int quote_get_entrust_data_by_code(const char* code, EntrustData* data) {
    if (!code || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_entrust_data_by_code(code, data);
}

int quote_get_trade_data_by_code(const char* code, TradeData* data) {
    if (!code || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_service_mutex);
    
    if (!ft::g_quote_service) {
        return -1;
    }
    
    ft::IQuoteParser* parser = ft::g_quote_service->get_parser();
    if (!parser) {
        return -1;
    }
    
    return parser->get_trade_data_by_code(code, data);
}

} // extern "C"
