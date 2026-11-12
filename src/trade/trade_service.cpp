#include "trade/trade_service.h"
#include "trade/trade_handler_adapter.h"
#include <fstream>
#include "logger/logger.hpp"
#include "laserpants/dotenv/dotenv.h"

#define TRADE_ERROR_MSG_SIZE 256

NS_FT_BEGIN

TradeService::TradeService() 
    : _running(false), _initialized(false) {
    dotenv::init();
    TRADE_LOG_INFO("TradeService created");
}

TradeService::~TradeService() noexcept {
    try {
        release();
        TRADE_LOG_INFO("TradeService destroyed");
    } catch (const std::exception& e) {
        TRADE_LOG_ERROR("Exception in destructor: {}", e.what());
    }
}

int TradeService::init() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (_initialized.load()) {
        TRADE_LOG_INFO("Service already initialized");
        return 0;
    }
    
    try {
        // 从环境变量获取配置文件路径
        std::string env_config_file = dotenv::getenv("TRADE_CONFIG_FILE", "");
        if(env_config_file.empty()) {
            _config_file = "./trade_config.json";
            TRADE_LOG_INFO("Using default trade config file: {}", _config_file);
        } else {
            _config_file = env_config_file;
            TRADE_LOG_INFO("Using trade config file from environment: {}", _config_file);
        }
        
        // 加载配置文件，找到active的handler配置
        nlohmann::json config;
        if (!_load_config(_config_file.c_str(), config)) {
            return -1;
        }
        
        nlohmann::json* active_handler = _find_active_handler_config(config);
        if (!active_handler) {
            _last_error = "No active handler found in config";
            TRADE_LOG_ERROR(_last_error);
            return -1;
        }
        
        // 获取处理器ID
        if (active_handler->contains("id")) {
            _handler_id = active_handler->at("id").get<std::string>();
        } else {
            _handler_id = "default_handler";
        }
        
        // 创建 TradeHandlerAdapter 实例
        _handler_adapter = std::make_unique<TradeHandlerAdapter>();
        
        // 初始化 TradeHandlerAdapter
        if (!_handler_adapter->init(_handler_id.c_str(), active_handler)) {
            _last_error = "Failed to initialize handler adapter: " + std::string(_handler_adapter->get_last_error());
            TRADE_LOG_ERROR(_last_error);
            _handler_adapter.reset();
            return -1;
        }
        
        _initialized.store(true);
        TRADE_LOG_INFO("TradeService initialized successfully with handler: {}", _handler_id);
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during initialization: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return -1;
    }
}

int TradeService::start() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (!_initialized.load()) {
        _last_error = "Service not initialized";
        TRADE_LOG_ERROR(_last_error);
        return -1;
    }
    
    if (_running.load()) {
        TRADE_LOG_INFO("Service already running");
        return 0;
    }
    
    try {
        // 启动处理器
        if (!_handler_adapter->run()) {
            _last_error = "Failed to start handler: " + std::string(_handler_adapter->get_last_error());
            TRADE_LOG_ERROR(_last_error);
            return -1;
        }
        
        _running.store(true);
        TRADE_LOG_INFO("TradeService started successfully");
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during start: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return -1;
    }
}

int TradeService::stop() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (!_running.load()) {
        TRADE_LOG_INFO("Service already stopped");
        return 0;
    }
    
    try {
        // 通过adapter正确停止handler
        if (_handler_adapter) {
            if (!_handler_adapter->stop()) {
                _last_error = "Failed to stop handler: " + std::string(_handler_adapter->get_last_error());
                TRADE_LOG_ERROR(_last_error);
                return -1;
            }
        }
        
        _running.store(false);
        TRADE_LOG_INFO("TradeService stopped");
        return 0;
        
    } catch (const std::exception& e) {
        _last_error = "Exception during stop: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return -1;
    }
}

void TradeService::release() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    try {
        if (_running.load()) {
            // 先停止服务
            if (_handler_adapter) {
                ITradeHandler* handler = _handler_adapter->get_handler();
                if (handler) {
                    handler->disconnect();
                }
            }
            _running.store(false);
        }
        
        // 完全清理资源
        if (_handler_adapter) {
            _handler_adapter->release();
            _handler_adapter.reset();
        }
        
        _initialized.store(false);
        TRADE_LOG_INFO("TradeService release completed");
        
    } catch (const std::exception& e) {
        TRADE_LOG_ERROR("Exception during release: {}", e.what());
    }
}

ITradeHandler* TradeService::get_handler() const {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _handler_adapter ? _handler_adapter->get_handler() : nullptr;
}

bool TradeService::is_running() const {
    return _running.load();
}

const char* TradeService::get_last_error() const {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _last_error.c_str();
}

const std::string& TradeService::get_handler_id() const {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _handler_id;
}

bool TradeService::_load_config(const char* config_file, nlohmann::json& config) {
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            _last_error = "Cannot open config file: " + std::string(config_file);
            TRADE_LOG_ERROR(_last_error);
            return false;
        }
        
        file >> config;
        file.close();
        
        TRADE_LOG_INFO("Config file loaded successfully: {}", config_file);
        return true;
        
    } catch (const nlohmann::json::parse_error& e) {
        _last_error = "JSON parse error: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    } catch (const std::exception& e) {
        _last_error = "Exception loading config: " + std::string(e.what());
        TRADE_LOG_ERROR(_last_error);
        return false;
    }
}

nlohmann::json* TradeService::_find_active_handler_config(const nlohmann::json& config) {
    if (!config.contains("parsers") || !config["parsers"].is_array()) {
        return nullptr;
    }
    
    for (auto& parser : const_cast<nlohmann::json&>(config)["parsers"]) {
        if (parser.contains("active") && parser["active"].get<bool>()) {
            return &parser;
        }
    }
    
    return nullptr;
}

// 全局服务实例
static std::unique_ptr<TradeService> g_trade_service;
static std::shared_mutex g_trade_service_mutex;

NS_FT_END

// ===== C接口实现 =====
extern "C" {

int trade_init() {
    std::unique_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        ft::g_trade_service = std::make_unique<ft::TradeService>();
    }
    
    return ft::g_trade_service->init();
}

int trade_init_with_config(const char* config_file) {
    if (!config_file) {
        return -1;
    }
    
    setenv("TRADE_CONFIG_FILE", config_file, 1);
    return trade_init();
}

int trade_start() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    return ft::g_trade_service->start();
}

int trade_stop() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return 0;
    }
    
    return ft::g_trade_service->stop();
}

void trade_release() {
    std::unique_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (ft::g_trade_service) {
        ft::g_trade_service->release();
        ft::g_trade_service.reset();
    }
}

void* trade_get_handler() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    return ft::g_trade_service ? ft::g_trade_service->get_handler() : nullptr;
}

int trade_connect() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->connect() : -1;
}

int trade_disconnect() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->disconnect() : -1;
}

int trade_is_running() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return 0;
    }
    
    return ft::g_trade_service->is_running() ? 1 : 0;
}

int trade_is_connected() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return 0;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return (handler && handler->is_connected()) ? 1 : 0;
}

int trade_is_account_login(const char* login_id) {
    if (!login_id) {
        return 0;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return 0;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return (handler && handler->is_account_login(login_id)) ? 1 : 0;
}

int trade_get_logged_in_login_ids(char*** account_ids, int* count) {
    if (!account_ids || !count) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    auto ids = handler->get_logged_in_login_ids();
    *count = static_cast<int>(ids.size());
    
    if (*count == 0) {
        *account_ids = nullptr;
        return 0;
    }
    
    // 分配内存
    *account_ids = static_cast<char**>(malloc(*count * sizeof(char*)));
    if (!*account_ids) {
        return -1;
    }
    
    for (int i = 0; i < *count; ++i) {
        (*account_ids)[i] = static_cast<char*>(malloc((ids[i].length() + 1) * sizeof(char)));
        if (!(*account_ids)[i]) {
            // 释放已分配的内存
            for (int j = 0; j < i; ++j) {
                free((*account_ids)[j]);
            }
            free(*account_ids);
            *account_ids = nullptr;
            *count = 0;
            return -1;
        }
        strcpy((*account_ids)[i], ids[i].c_str());
    }
    
    return 0;
}

int trade_set_db_manager(void *db_manager) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    if (!ft::g_trade_service) {
        return -1;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }

    handler->set_db_manager(static_cast<ft::DatabaseManager *>(db_manager));
    return 0;
}

void* trade_get_db_manager() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    if (!ft::g_trade_service) {
        return nullptr;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_db_manager() : nullptr;
}

int trade_get_logged_in_account_count() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_logged_in_account_count() : -1;
}

Status trade_get_status() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return STATUS_ERROR;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_status() : STATUS_ERROR;
}

const char* trade_get_last_error() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return "Service not initialized";
    }
    
    return ft::g_trade_service->get_last_error();
}

const char* trade_get_interface_id() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return nullptr;
    }
    
    return ft::g_trade_service->get_handler_id().c_str();
}

// ===== 多账户管理接口 =====
int trade_get_account_info_by_id(const char* login_id, AccountInfo* account) {
    if (!login_id || !account) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_account_info_by_id(login_id, account) : -1;
}

int trade_get_all_login_ids(char*** account_ids, int* count) {
    if (!account_ids || !count) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    auto ids = handler->get_all_login_ids();
    *count = static_cast<int>(ids.size());
    
    if (*count == 0) {
        *account_ids = nullptr;
        return 0;
    }
    
    // 分配内存
    *account_ids = static_cast<char**>(malloc(*count * sizeof(char*)));
    if (!*account_ids) {
        return -1;
    }
    
    for (int i = 0; i < *count; ++i) {
        (*account_ids)[i] = static_cast<char*>(malloc((ids[i].length() + 1) * sizeof(char)));
        if (!(*account_ids)[i]) {
            // 释放已分配的内存
            for (int j = 0; j < i; ++j) {
                free((*account_ids)[j]);
            }
            free(*account_ids);
            *account_ids = nullptr;
            *count = 0;
            return -1;
        }
        strcpy((*account_ids)[i], ids[i].c_str());
    }
    
    return 0;
}

void trade_free_account_ids(char** account_ids, int count) {
    if (!account_ids || count <= 0) {
        return;
    }
    
    for (int i = 0; i < count; ++i) {
        if (account_ids[i]) {
            free(account_ids[i]);
        }
    }
    free(account_ids);
}

int trade_get_account_count() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_account_count() : -1;
}

int trade_has_account(const char* login_id) {
    if (!login_id) {
        return 0;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return 0;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return (handler && handler->has_account(login_id)) ? 1 : 0;
}

EXPORT_FLAG int trade_get_account_data_by_id(const char* login_id, const char* fund_account_id, const char* account_id, AccountData* data) {
    if (!login_id || !fund_account_id || !account_id || !data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    try {
        return handler->get_account_data_by_id(login_id, fund_account_id, account_id, data);
    } catch (const std::exception& e) {
        TRADE_LOG_ERROR("Exception in trade_get_account_data_by_id: {}", e.what());
        return -1;
    }
}

EXPORT_FLAG int trade_get_all_account_data_by_fund_id(const char* login_id, const char* fund_account_id, AccountData* sh_data, AccountData* sz_data) {
    if (!login_id || !fund_account_id) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    try {
        return handler->get_all_account_data_by_fund_id(login_id, fund_account_id, sh_data, sz_data);
    } catch (const std::exception& e) {
        TRADE_LOG_ERROR("Exception in trade_get_all_account_data_by_fund_id: {}", e.what());
        return -1;
    }
}

EXPORT_FLAG int trade_get_all_account_data_by_login_id(const char* login_id, AccountData** account_data_array, int* count) {
    if (!login_id || !account_data_array || !count) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    try {
        // 获取所有账户ID
        std::vector<std::string> login_ids = handler->get_all_login_ids();
        std::vector<AccountData> account_data_list;
        
        // 为每个匹配的客户收集所有账户数据
        for (const auto& key : login_ids) {
            if (key.find(login_id) == 0) {  // 以login_id开头
                AccountData sh_data = {}, sz_data = {};
                // 解析fund_account_id（从account_id中提取）
                size_t first_sep = key.find('|');
                if (first_sep != std::string::npos) {
                    std::string fund_account_id = key.substr(first_sep + 1);
                    size_t second_sep = fund_account_id.find('|');
                    if (second_sep != std::string::npos) {
                        fund_account_id = fund_account_id.substr(0, second_sep);
                        
                        if (handler->get_all_account_data_by_fund_id(login_id, fund_account_id.c_str(), &sh_data, &sz_data) > 0) {
                            if (sh_data.timestamp > 0) {
                                account_data_list.push_back(sh_data);
                            }
                            if (sz_data.timestamp > 0) {
                                account_data_list.push_back(sz_data);
                            }
                        }
                    }
                }
            }
        }
        
        // 分配内存并复制数据
        if (!account_data_list.empty()) {
            *account_data_array = static_cast<AccountData*>(malloc(sizeof(AccountData) * account_data_list.size()));
            if (*account_data_array) {
                std::copy(account_data_list.begin(), account_data_list.end(), *account_data_array);
                *count = static_cast<int>(account_data_list.size());
                return 0;
            }
        }
        
        *count = 0;
        return 0;
    } catch (const std::exception& e) {
        TRADE_LOG_ERROR("Exception in trade_get_all_account_data_by_login_id: {}", e.what());
        return -1;
    }
}

int64_t trade_send_order(const TradeOrderRequest* order) {
    if (!order) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->send_order(order) : -1;
}

int32_t trade_send_order_sync(const TradeOrderRequest* order, TradeOrderRspData* result, int timeout_ms) {
    if (!order || !result) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->send_order_sync(order, timeout_ms);
    if (sync_result.is_success()) {
        *result = sync_result.data;
        return 0;
    } else {
        if (sync_result.error_msg && sync_result.error_msg[0] != '\0') {
            strncpy(result->error_msg, sync_result.error_msg, sizeof(result->error_msg) - 1);
            result->error_msg[sizeof(result->error_msg) - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_cancel_order(const AccountInfo* account, const int64_t orig_order_id, 
                          uint64_t orig_batch_id, MarketType market) {
    if (!account || !orig_order_id) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->cancel_order(account, orig_order_id, orig_batch_id, market) : -1;
}

int32_t trade_cancel_order_sync(const AccountInfo* account, const int64_t orig_order_id, 
                                uint64_t orig_batch_id, MarketType market,
                                TradeOrderCancelRsqData* result, int timeout_ms) {
    if (!account || !orig_order_id || !result) {
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->cancel_order_sync(account, orig_order_id, orig_batch_id, market, timeout_ms);
    if (sync_result.is_success()) {
        *result = sync_result.data;
        return 0;
    } else {
        if (sync_result.error_msg && sync_result.error_msg[0] != '\0') {
            strncpy(result->error_msg, sync_result.error_msg, sizeof(result->error_msg) - 1);
            result->error_msg[sizeof(result->error_msg) - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_query_orders(const AccountInfo* account, MarketType market, const char* security_code, 
                          const int64_t order_id, const QueryRequestParams *params) {
    if (!account || market == MARKET_UNKNOWN) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->query_orders(account, market, security_code, order_id, params) : -1;
}

int32_t trade_query_orders_sync(const AccountInfo* account, MarketType market, const char* security_code, 
                                const int64_t order_id, const QueryRequestParams *params,
                                TradeOrderData** results, int* count, int timeout_ms,
                                char* error_msg) {
    if (!account || market == MARKET_UNKNOWN || !results || !count) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->query_orders_sync(account, market, security_code, order_id, params, timeout_ms);
    
    if (sync_result.is_success()) {
        const auto& items = sync_result.data;
        *count = static_cast<int>(items.size());
        
        if (*count > 0) {
            *results = static_cast<TradeOrderData*>(malloc(*count * sizeof(TradeOrderData)));
            if (*results) {
                for (int i = 0; i < *count; ++i) {
                    (*results)[i] = items[i];
                }
                return 0;
            } else {
                *count = 0;
                if (error_msg) {
                    strncpy(error_msg, "Memory allocation failed", TRADE_ERROR_MSG_SIZE - 1);
                    error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
                }
                return -1;
            }
        } else {
            *results = nullptr;
            *count = 0;
            return 0;
        }
    } else {
        *results = nullptr;
        *count = 0;
        // 将错误信息复制到输出参数
        if (error_msg) {
            strncpy(error_msg, sync_result.error_msg, TRADE_ERROR_MSG_SIZE - 1);
            error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_query_positions(const AccountInfo* account, MarketType market, const char* security_code,
                              const QueryRequestParams *params) {
    if (!account || market == MARKET_UNKNOWN) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->query_positions(account, market, security_code, params) : -1;
}

int32_t trade_query_positions_sync(const AccountInfo* account, MarketType market, const char* security_code, 
                                    const QueryRequestParams *params,
                                    PositionData** results, int* count, int timeout_ms,
                                    char* error_msg) {
    if (!account || market == MARKET_UNKNOWN || !results || !count) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->query_positions_sync(account, market, security_code, params, timeout_ms);
    
    if (sync_result.is_success()) {
        const auto& items = sync_result.data;
        *count = static_cast<int>(items.size());
        
        if (*count > 0) {
            *results = static_cast<PositionData*>(malloc(*count * sizeof(PositionData)));
            if (*results) {
                for (int i = 0; i < *count; ++i) {
                    (*results)[i] = items[i];
                }
                return 0;
            } else {
                *count = 0;
                if (error_msg) {
                    strncpy(error_msg, "Memory allocation failed", TRADE_ERROR_MSG_SIZE - 1);
                    error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
                }
                return -1;
            }
        } else {
            *results = nullptr;
            *count = 0;
            return 0;
        }
    } else {
        *results = nullptr;
        *count = 0;
        // 将错误信息复制到输出参数
        if (error_msg) {
            strncpy(error_msg, sync_result.error_msg, TRADE_ERROR_MSG_SIZE - 1);
            error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_query_funds(const AccountInfo* account, MarketType market, const char* currency,
                          const QueryRequestParams *params) {
    if (!account || market == MARKET_UNKNOWN) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->query_funds(account, market, currency, params) : -1;
}

int32_t trade_query_funds_sync(const AccountInfo* account, MarketType market, const char* currency, 
                               const QueryRequestParams *params,
                               AccountData** results, int* count, int timeout_ms,
                               char* error_msg) {
    if (!account || market == MARKET_UNKNOWN || !results || !count) {
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->query_funds_sync(account, market, currency, params, timeout_ms);
    
    if (sync_result.is_success()) {
        const auto& items = sync_result.data;
        *count = static_cast<int>(items.size());
        
        if (*count > 0) {
            *results = static_cast<AccountData*>(malloc(*count * sizeof(AccountData)));
            if (*results) {
                for (int i = 0; i < *count; ++i) {
                    (*results)[i] = items[i];
                }
                return 0;
            } else {
                *count = 0;
                if (error_msg) {
                    strncpy(error_msg, "Memory allocation failed", TRADE_ERROR_MSG_SIZE - 1);
                    error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
                }
                return -1;
            }
        } else {
            *results = nullptr;
            *count = 0;
            return 0;
        }
    } else {
        *results = nullptr;
        *count = 0;
        // 将错误信息复制到输出参数
        if (error_msg) {
            strncpy(error_msg, sync_result.error_msg, TRADE_ERROR_MSG_SIZE - 1);
            error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_query_trades(const AccountInfo* account, MarketType market, const char* security_code, 
                          const int64_t order_id, const QueryRequestParams *params) {
    if (!account || market == MARKET_UNKNOWN) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->query_trades(account, market, security_code, order_id) : -1;
}

int32_t trade_query_trades_sync(const AccountInfo* account, MarketType market, const char* security_code, 
                                const int64_t order_id, const QueryRequestParams *params,
                                TradeFillData** results, int* count, int timeout_ms,
                                char* error_msg) {
    if (!account || market == MARKET_UNKNOWN || !results || !count) {
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->query_trades_sync(account, market, security_code, order_id, params, timeout_ms);
    
    if (sync_result.is_success()) {
        const auto& items = sync_result.data;
        *count = static_cast<int>(items.size());
        
        if (*count > 0) {
            *results = static_cast<TradeFillData*>(malloc(*count * sizeof(TradeFillData)));
            if (*results) {
                for (int i = 0; i < *count; ++i) {
                    (*results)[i] = items[i];
                }
                return 0;
            } else {
                *count = 0;
                if (error_msg) {
                    strncpy(error_msg, "Memory allocation failed", TRADE_ERROR_MSG_SIZE - 1);
                    error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
                }
                return -1;
            }
        } else {
            *results = nullptr;
            *count = 0;
            return 0;
        }
    } else {
        *results = nullptr;
        *count = 0;
        // 将错误信息复制到输出参数
        if (error_msg) {
            strncpy(error_msg, sync_result.error_msg, TRADE_ERROR_MSG_SIZE - 1);
            error_msg[TRADE_ERROR_MSG_SIZE - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_bank_securities_transfer(const AccountInfo* account, 
                                       BankSecuritiesTransferDirection direction,
                                       MarketType market,
                                       double transfer_amount,
                                       const char* currency,
                                       const char* bank_password,
                                       const char* fund_password,
                                       const char* remark) {
    if (!account) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->bank_securities_transfer(account, direction, market, transfer_amount,
                                                      currency, bank_password, fund_password, 
                                                      remark) : -1;
}

int32_t trade_bank_securities_transfer_sync(const AccountInfo* account, 
                                            BankSecuritiesTransferDirection direction,
                                            MarketType market,
                                            double transfer_amount,
                                            const char* currency,
                                            const char* bank_password,
                                            const char* fund_password,
                                            const char* remark,
                                            BankSecuritiesTransferData* result, int timeout_ms) {
    if (!account || !result) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->bank_securities_transfer_sync(account, direction, market, transfer_amount,
                                                              currency, bank_password, fund_password, 
                                                              remark, timeout_ms);
    if (sync_result.is_success()) {
        *result = sync_result.data;
        return 0;
    } else {
        if (sync_result.error_msg && sync_result.error_msg[0] != '\0') {
            strncpy(result->error_msg, sync_result.error_msg, sizeof(result->error_msg) - 1);
            result->error_msg[sizeof(result->error_msg) - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

int64_t trade_fund_transfer(const AccountInfo* account,
                           const char* fund_out_account_id,
                           MarketType fund_out_market,
                           const char* fund_in_account_id,
                           MarketType fund_in_market,
                           double transfer_amount) {
    if (!account || !fund_out_account_id || !fund_in_account_id) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->fund_transfer(account, fund_out_account_id, fund_out_market,
                                           fund_in_account_id, fund_in_market, 
                                           transfer_amount) : -1;
}

int32_t trade_fund_transfer_sync(const AccountInfo* account,
                                 const char* fund_out_account_id,
                                 MarketType fund_out_market,
                                 const char* fund_in_account_id,
                                 MarketType fund_in_market,
                                 double transfer_amount,
                                 FundTransferData* result, int timeout_ms) {
    if (!account || !result) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    auto sync_result = handler->fund_transfer_sync(account, fund_out_account_id, fund_out_market,
                                                  fund_in_account_id, fund_in_market, 
                                                  transfer_amount, timeout_ms);
    if (sync_result.is_success()) {
        *result = sync_result.data;
        return 0;
    } else {
        if (sync_result.error_msg && sync_result.error_msg[0] != '\0') {
            strncpy(result->error_msg, sync_result.error_msg, sizeof(result->error_msg) - 1);
            result->error_msg[sizeof(result->error_msg) - 1] = '\0';
        }
        return sync_result.error_code;
    }
}

TradeOrderCallbackHandle trade_set_order_rtn_callback(TradeOrderCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeOrderCallbackHandle{}; // 返回空句柄
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeOrderCallbackHandle{}; // 返回空句柄
    }
    
    return handler->set_order_rtn_callback(callback);
}

int trade_remove_order_rtn_callback(TradeOrderCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_order_rtn_callback(handle);
}
    
TradeOrderQueryCallbackHandle trade_set_order_query_callback(TradeOrderQueryCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeOrderQueryCallbackHandle{}; // 返回空句柄
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeOrderQueryCallbackHandle{}; // 返回空句柄
    }
    
    return handler->set_order_query_callback(callback);
}

int trade_remove_order_query_callback(TradeOrderQueryCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_order_query_callback(handle);
}

TradeFillCallbackHandle trade_set_fill_callback(TradeFillCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeFillCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeFillCallbackHandle{};
    }
    
    return handler->set_fill_callback(callback);
}

int trade_remove_fill_callback(TradeFillCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_fill_callback(handle);
}

PositionCallbackHandle trade_set_position_callback(PositionCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return PositionCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return PositionCallbackHandle{};
    }
    
    return handler->set_position_callback(callback);
}

int trade_remove_position_callback(PositionCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_position_callback(handle);
}

AccountCallbackHandle trade_set_account_callback(AccountCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return AccountCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return AccountCallbackHandle{};
    }
    
    return handler->set_account_callback(callback);
}

int trade_remove_account_callback(AccountCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_account_callback(handle);
}

TradeErrorCallbackHandle trade_set_error_callback(TradeErrorCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeErrorCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeErrorCallbackHandle{};
    }
    
    return handler->set_error_callback(callback);
}

int trade_remove_error_callback(TradeErrorCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_error_callback(handle);
}

BankSecuritiesTransferCallbackHandle trade_set_bank_securities_transfer_callback(BankSecuritiesTransferCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return BankSecuritiesTransferCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return BankSecuritiesTransferCallbackHandle{};
    }
    
    return handler->set_bank_securities_transfer_callback(callback);
}

int trade_remove_bank_securities_transfer_callback(BankSecuritiesTransferCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_bank_securities_transfer_callback(handle);
}

FundTransferCallbackHandle trade_set_fund_transfer_callback(FundTransferCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return FundTransferCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return FundTransferCallbackHandle{};
    }
    
    return handler->set_fund_transfer_callback(callback);
}

int trade_remove_fund_transfer_callback(FundTransferCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_fund_transfer_callback(handle);
}

TradeOrderRspCallbackHandle trade_set_order_rsp_callback(TradeOrderRspCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeOrderRspCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeOrderRspCallbackHandle{};
    }
    
    return handler->set_order_rsp_callback(callback);
}

int trade_remove_order_rsp_callback(TradeOrderRspCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_order_rsp_callback(handle);
}

TradeOrderCancelRspCallbackHandle trade_set_cancel_rsp_callback(TradeOrderCancelRspCallbackType callback) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return TradeOrderCancelRspCallbackHandle{};
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return TradeOrderCancelRspCallbackHandle{};
    }
    
    return handler->set_cancel_rsp_callback(callback);
}

int trade_remove_cancel_rsp_callback(TradeOrderCancelRspCallbackHandle handle) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    return handler->remove_cancel_rsp_callback(handle);
}

int trade_get_order_rtn_data(TradeOrderData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_order_rtn_data(data) : -1;
}

int trade_get_order_query_data(TradeOrderData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);

    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_order_query_data(data) : -1;
}


int trade_get_fill_data(TradeFillData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_fill_data(data) : -1;
}

int trade_get_position_data(PositionData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_position_data(data) : -1;
}

int trade_get_bank_securities_transfer_data(BankSecuritiesTransferData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_bank_securities_transfer_data(data) : -1;
}

int trade_get_fund_transfer_data(FundTransferData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_fund_transfer_data(data) : -1;
}

int trade_get_order_rsp_data(TradeOrderRspData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_order_rsp_data(data) : -1;
}

int trade_get_cancel_rsp_data(TradeOrderCancelRsqData* data) {
    if (!data) {
        return -1;
    }
    
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }
    
    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    return handler ? handler->get_cancel_rsp_data(data) : -1;
}

void trade_free_query_results(void* results, int count) {
    if (results && count > 0) {
        free(results);
    }
}

int trade_set_quote_parser(void *quote_parser) {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return -1;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return -1;
    }
    
    handler->set_quote_parser(static_cast<ft::IQuoteParser*>(quote_parser));

    return 0;
}

void *trade_get_quote_parser() {
    std::shared_lock<std::shared_mutex> lock(ft::g_trade_service_mutex);
    
    if (!ft::g_trade_service) {
        return nullptr;
    }

    ft::ITradeHandler* handler = ft::g_trade_service->get_handler();
    if (!handler) {
        return nullptr;
    }
    
    return handler->get_quote_parser();
}

} // extern "C"