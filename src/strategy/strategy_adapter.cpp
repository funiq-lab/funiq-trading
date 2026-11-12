#include "strategy/strategy_adapter.h"
#include "strategy/base_strategy.h"
#include "quote/quote_parser.h"
#include "trade/trade_handler.h"
#include "logger/logger.hpp"
#include <algorithm>

#include "strategies/stop_profit_loss_strategy/stop_profit_loss_strategy.h"

NS_FT_BEGIN

// ===== 构造函数和析构函数 =====

StrategyAdapter::StrategyAdapter(size_t thread_count, const std::vector<int>& cores)
    : _thread_count(thread_count == 0 ? std::thread::hardware_concurrency() : thread_count)
    , _cores(cores)
    , _quote_parser(nullptr)
    , _trade_handler(nullptr) {
    
    // 初始化空快照
    _strategy_snapshot = std::make_shared<StrategySnapshot>();
    
    STRATEGY_LOG_INFO("StrategyAdapter created with {} threads", _thread_count);
}

StrategyAdapter::~StrategyAdapter() {
    release();
    STRATEGY_LOG_INFO("StrategyAdapter destroyed");
}

// ===== 生命周期管理 =====

int StrategyAdapter::init() {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    
    if (_initialized.load()) {
        STRATEGY_LOG_INFO("StrategyAdapter already initialized");
        return 0;
    }
    
    try {
        // 初始化线程池，带CPU绑核功能
        STRATEGY_LOG_INFO("Initializing thread pool with {} threads", _thread_count);

        if (_cores.empty()) {
            // 如果未指定核心，默认不绑核
            STRATEGY_LOG_INFO("No cores specified, not binding threads to any CPU core");
            _thread_pool = std::make_unique<BS::thread_pool<>>(_thread_count);
        } else {
            // 绑定指定核心
            std::string core_list = "";
            for (auto core : _cores) {
                core_list += std::to_string(core) + ", ";
            }
            STRATEGY_LOG_INFO("Binding threads to cores: {}", core_list);   
            _thread_pool = std::make_unique<BS::thread_pool<>>(_thread_count, [this](std::size_t thread_idx) {
                // 为每个线程设置CPU亲和性（绑核）
                _bind_thread_to_cpu(thread_idx);
            });
        }
        
        // 清理之前的状态
        _strategy_factories.clear();
        _strategy_configs.clear();
        _strategy_instances.clear();
        _active_strategy_ids.clear();
        
        // 重置快照
        _strategy_snapshot = std::make_shared<StrategySnapshot>();
        
        // 重置错误状态
        {
            std::lock_guard<std::mutex> error_lock(_error_mutex);
            _last_error.clear();
        }
        
        _initialized.store(true);
        STRATEGY_LOG_INFO("StrategyAdapter initialized successfully with {} threads", _thread_count);
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception during adapter initialization: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("StrategyAdapter init failed: {}", e.what());
        return -1;
    }
}

int StrategyAdapter::start() {
    if (!_check_state(true, false)) {
        return -1;
    }
    
    if (_running.load()) {
        STRATEGY_LOG_INFO("StrategyAdapter already running");
        return 0;
    }
    
    try {
        _running.store(true);
        
        // 自动启动所有配置的策略实例
        int started_count = start_all_strategies();
        if (started_count > 0) {
            STRATEGY_LOG_INFO("Auto-started {} strategy instances on adapter start", started_count);
        }
        
        STRATEGY_LOG_INFO("StrategyAdapter started successfully");
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception during adapter start: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("StrategyAdapter start failed: {}", e.what());
        _running.store(false);
        return -1;
    }
}

int StrategyAdapter::stop() {
    if (!_running.load()) {
        STRATEGY_LOG_INFO("StrategyAdapter already stopped");
        return 0;
    }
    
    try {
        _running.store(false);
        
        // 停止所有活跃策略
        stop_all_strategies();
        
        STRATEGY_LOG_INFO("StrategyAdapter stopped successfully");
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception during adapter stop: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("StrategyAdapter stop failed: {}", e.what());
        return -1;
    }
}

void StrategyAdapter::release() {
    try {
        // 停止运行
        _running.store(false);
        
        // 销毁所有策略实例
        destroy_all_strategies();
        
        // 清理策略注册信息
        {
            std::lock_guard<std::mutex> lock(_strategy_management_mutex);
            _strategy_factories.clear();
            _strategy_configs.clear();
            _strategy_instances.clear();
            _active_strategy_ids.clear();
        }
        
        // 重置快照
        _strategy_snapshot = std::make_shared<StrategySnapshot>();
        
        // 销毁线程池
        _thread_pool.reset();
        
        // 清理依赖服务引用
        _quote_parser = nullptr;
        _trade_handler = nullptr;
        
        // 重置状态
        _initialized.store(false);
        
        {
            std::lock_guard<std::mutex> error_lock(_error_mutex);
            _last_error.clear();
        }
        
        STRATEGY_LOG_INFO("StrategyAdapter released successfully");
        
    } catch (const std::exception& e) {
        STRATEGY_LOG_ERROR("Exception during StrategyAdapter release: {}", e.what());
    }
}

bool StrategyAdapter::is_initialized() const {
    return _initialized.load();
}

bool StrategyAdapter::is_running() const {
    return _running.load();
}

// ===== 配置文件驱动的策略管理 =====

int StrategyAdapter::load_strategies_from_config(const nlohmann::json& config_json) {
    try {
        if (!config_json.contains("strategies") || !config_json["strategies"].is_array()) {
            _set_error("Invalid config: missing or invalid 'strategies' field");
            return -1;
        }
        
        int success_count = 0;
        for (const auto& strategy_json : config_json["strategies"]) {
            if (!strategy_json.contains("id") || !strategy_json.contains("module")) {
                STRATEGY_LOG_ERROR("Invalid strategy config: missing 'id' or 'module' field");
                continue;
            }
            
            StrategyConfig config;
            config.id = strategy_json["id"];
            config.module = strategy_json["module"];
            config.name = strategy_json.value("name", config.id);
            config.description = strategy_json.value("description", "");
            
            // 将默认配置转为JSON字符串
            if (strategy_json.contains("config")) {
                config.config = strategy_json["config"].dump();
            } else {
                config.config = "{}";
            }
            
            // 直接注册策略
            if (register_strategy_from_config(config.id, config) == 0) {
                // 创建策略实例
                if (create_strategy_instance(config.id, config.config.c_str()) == 0) {
                    success_count++;
                    STRATEGY_LOG_INFO("Strategy instance created from config: {}", config.id);
                } else {
                    STRATEGY_LOG_ERROR("Failed to create strategy instance from config: {}", config.id);
                }
            }
        }
        
        STRATEGY_LOG_INFO("Loaded {} strategies from config", success_count);
        return success_count > 0 ? 0 : -1;
        
    } catch (const std::exception& e) {
        _set_error("Exception loading strategies from config: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to load strategies from config: {}", e.what());
        return -1;
    }
}

int StrategyAdapter::register_strategy_from_config(const std::string& strategy_id, const StrategyConfig& config) {
    if (strategy_id.empty() || config.module.empty()) {
        _set_error("Invalid strategy ID or module name");
        return -1;
    }
    
    // 直接根据模块名创建工厂函数
    StrategyFactory strategy_factory = _create_builtin_strategy_factory(config.module);
    if (!strategy_factory) {
        _set_error("Unknown strategy module: " + config.module);
        STRATEGY_LOG_ERROR("Unknown strategy module: {}", config.module);
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        
        // 存储策略配置
        _strategy_configs[strategy_id] = config;
        
        // 注册策略到适配器
        _strategy_factories[strategy_id] = strategy_factory;
        
        STRATEGY_LOG_INFO("Strategy registered from config: id={}, module={}", strategy_id, config.module);
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception registering strategy from config: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to register strategy from config {}: {}", strategy_id, e.what());
        return -1;
    }
}

// ===== 策略注册和管理 =====

int StrategyAdapter::register_strategy(const std::string& strategy_id, StrategyFactory factory) {
    if (strategy_id.empty() || !factory) {
        _set_error("Invalid strategy ID or factory");
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        
        // 检查是否已经注册
        if (_strategy_factories.find(strategy_id) != _strategy_factories.end()) {
            STRATEGY_LOG_WARN("Strategy {} already registered, overwriting", strategy_id);
        }
        
        _strategy_factories[strategy_id] = factory;
        STRATEGY_LOG_INFO("Strategy {} registered successfully", strategy_id);
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception during strategy registration: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to register strategy {}: {}", strategy_id, e.what());
        return -1;
    }
}

int StrategyAdapter::unregister_strategy(const std::string& strategy_id) {
    if (strategy_id.empty()) {
        _set_error("Invalid strategy ID");
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        
        // 如果策略实例存在，先销毁
        _destroy_strategy_instance_internal(strategy_id);
        
        auto it = _strategy_factories.find(strategy_id);
        if (it != _strategy_factories.end()) {
            _strategy_factories.erase(it);
            _strategy_configs.erase(strategy_id); // 同时删除配置
            _update_strategy_snapshot(); // 更新快照
            STRATEGY_LOG_INFO("Strategy {} unregistered successfully", strategy_id);
            return 0;
        } else {
            _set_error("Strategy not found: " + strategy_id);
            return -1;
        }
        
    } catch (const std::exception& e) {
        _set_error("Exception during strategy unregistration: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to unregister strategy {}: {}", strategy_id, e.what());
        return -1;
    }
}

std::vector<std::string> StrategyAdapter::get_registered_strategy_ids() const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    
    std::vector<std::string> ids;
    ids.reserve(_strategy_factories.size());
    
    for (const auto& pair : _strategy_factories) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

bool StrategyAdapter::is_strategy_registered(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    return _strategy_factories.find(strategy_id) != _strategy_factories.end();
}

const StrategyConfig* StrategyAdapter::get_strategy_config(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    auto it = _strategy_configs.find(strategy_id);
    return it != _strategy_configs.end() ? &it->second : nullptr;
}

// ===== 多策略实例管理 =====

int StrategyAdapter::create_strategy_instance(const std::string& strategy_id, const char* config) {
    if (!_check_state(true, false)) {
        return -1;
    }
    
    if (strategy_id.empty()) {
        _set_error("Invalid strategy ID");
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        
        // 检查策略是否已注册
        auto factory_it = _strategy_factories.find(strategy_id);
        if (factory_it == _strategy_factories.end()) {
            _set_error("Strategy not registered: " + strategy_id);
            return -1;
        }
        
        // 检查是否已存在实例
        if (_strategy_instances.find(strategy_id) != _strategy_instances.end()) {
            STRATEGY_LOG_WARN("Strategy instance {} already exists, destroying old instance", strategy_id);
            _destroy_strategy_instance_internal(strategy_id);
        }
        
        // 创建新策略实例
        auto strategy_instance = factory_it->second();
        if (!strategy_instance) {
            _set_error("Failed to create strategy instance: " + strategy_id);
            return -1;
        }
        
        // 注入依赖服务
        _inject_dependencies(strategy_instance.get());
        
        // 确定使用的配置
        std::string config_to_use;
        if (config != nullptr) {
            config_to_use = config;
        } else {
            auto config_it = _strategy_configs.find(strategy_id);
            if (config_it != _strategy_configs.end()) {
                config_to_use = config_it->second.config;
            } else {
                config_to_use = "{}";
            }
        }
        
        // 初始化策略
        int result = strategy_instance->init(config_to_use.c_str());
        if (result != 0) {
            _set_error("Failed to initialize strategy: " + strategy_id);
            return result;
        }

        // 设置策略ID
        strategy_instance->set_strategy_id(strategy_id.c_str());

        // 存储策略实例
        _strategy_instances[strategy_id] = std::move(strategy_instance);
        
        // 更新快照（但策略还未启动，所以不加入活跃列表）
        _update_strategy_snapshot();
        
        STRATEGY_LOG_INFO("Strategy instance created successfully: {}", strategy_id);
        return 0;
        
    } catch (const std::exception& e) {
        _set_error("Exception creating strategy instance: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to create strategy instance {}: {}", strategy_id, e.what());
        return -1;
    }
}

int StrategyAdapter::start_strategy_instance(const std::string& strategy_id) {
    if (!_check_state(true, false)) {
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        
        auto it = _strategy_instances.find(strategy_id);
        if (it == _strategy_instances.end()) {
            _set_error("Strategy instance not found: " + strategy_id);
            return -1;
        }
        
        int result = it->second->start();
        if (result == 0) {
            _active_strategy_ids.insert(strategy_id);
            // 更新快照，包含新启动的策略
            _update_strategy_snapshot();
            STRATEGY_LOG_INFO("Strategy instance started successfully: {}", strategy_id);
        } else {
            _set_error("Failed to start strategy instance: " + strategy_id);
        }
        
        return result;
        
    } catch (const std::exception& e) {
        _set_error("Exception starting strategy instance: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to start strategy instance {}: {}", strategy_id, e.what());
        return -1;
    }
}

int StrategyAdapter::stop_strategy_instance(const std::string& strategy_id) {
    try {
        BaseStrategy* strategy = nullptr;
        
        // 在锁保护下修改活跃策略列表并获取策略指针
        {
            std::lock_guard<std::mutex> lock(_strategy_management_mutex);
            
            // 从活跃列表中移除
            _active_strategy_ids.erase(strategy_id);
            // 更新快照，移除已停止的策略
            _update_strategy_snapshot();
            
            // 获取策略实例指针
            auto it = _strategy_instances.find(strategy_id);
            if (it == _strategy_instances.end()) {
                _set_error("Strategy instance not found: " + strategy_id);
                return -1;
            }
            strategy = it->second.get();
        }
        
        // 在锁外调用 stop（完全避免死锁）
        if (strategy) {
            int result = strategy->stop();
            if (result == 0) {
                STRATEGY_LOG_INFO("Strategy instance stopped successfully: {}", strategy_id);
            } else {
                _set_error("Failed to stop strategy instance: " + strategy_id);
            }
            return result;
        }
        
        return -1;
        
    } catch (const std::exception& e) {
        _set_error("Exception stopping strategy instance: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to stop strategy instance {}: {}", strategy_id, e.what());
        return -1;
    }
}

int StrategyAdapter::destroy_strategy_instance(const std::string& strategy_id) {
    try {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        int result = _destroy_strategy_instance_internal(strategy_id);
        if (result == 0) {
            // 更新快照，移除已销毁的策略
            _update_strategy_snapshot();
        }
        return result;
        
    } catch (const std::exception& e) {
        _set_error("Exception destroying strategy instance: " + std::string(e.what()));
        STRATEGY_LOG_ERROR("Failed to destroy strategy instance {}: {}", strategy_id, e.what());
        return -1;
    }
}

BaseStrategy* StrategyAdapter::get_strategy_instance(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    
    auto it = _strategy_instances.find(strategy_id);
    return it != _strategy_instances.end() ? it->second.get() : nullptr;
}

std::vector<std::string> StrategyAdapter::get_active_strategy_ids() const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    
    std::vector<std::string> ids;
    ids.reserve(_active_strategy_ids.size());
    
    for (const auto& id : _active_strategy_ids) {
        ids.push_back(id);
    }
    
    return ids;
}

bool StrategyAdapter::is_strategy_instance_active(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    return _active_strategy_ids.find(strategy_id) != _active_strategy_ids.end();
}

size_t StrategyAdapter::get_active_strategy_count() const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    return _active_strategy_ids.size();
}

// ===== 批量策略操作 =====

int StrategyAdapter::start_all_strategies() {
    std::vector<std::string> strategy_ids;
    
    // 获取所有未启动的策略ID
    {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        for (const auto& pair : _strategy_instances) {
            if (_active_strategy_ids.find(pair.first) == _active_strategy_ids.end()) {
                strategy_ids.push_back(pair.first);
            }
        }
    }
    
    // 在锁外启动策略
    int success_count = 0;
    for (const auto& strategy_id : strategy_ids) {
        if (start_strategy_instance(strategy_id) == 0) {
            success_count++;
        }
    }
    
    STRATEGY_LOG_INFO("Started {} strategy instances", success_count);
    return success_count;
}

int StrategyAdapter::stop_all_strategies() {
    std::vector<std::string> active_ids;
    
    // 获取活跃策略ID列表
    {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        for (const auto& id : _active_strategy_ids) {
            active_ids.push_back(id);
        }
    }
    
    // 在锁外停止策略
    int success_count = 0;
    for (const auto& strategy_id : active_ids) {
        if (stop_strategy_instance(strategy_id) == 0) {
            success_count++;
        }
    }
    
    STRATEGY_LOG_INFO("Stopped {} strategy instances", success_count);
    return success_count;
}

int StrategyAdapter::destroy_all_strategies() {
    if (_strategy_instances.empty()) {
        return 0;
    }
    
    STRATEGY_LOG_INFO("Destroying {} strategy instances...", _strategy_instances.size());
    
    // 1. 先停止所有策略，防止新任务提交
    {
        std::lock_guard<std::mutex> lock(_strategy_management_mutex);
        for (auto& [strategy_id, strategy] : _strategy_instances) {
            if (strategy) {
                strategy->stop();
            }
        }
    }
    
    // 2. 等待线程池完成所有任务（在锁外进行）
    if (_thread_pool) {
        _thread_pool->wait();
        STRATEGY_LOG_INFO("All thread pool tasks completed");
    }
    
    // 3. 在锁保护下收集策略ID并执行销毁
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    
    std::vector<std::string> strategy_ids_to_destroy;
    strategy_ids_to_destroy.reserve(_strategy_instances.size());
    
    for (const auto& [strategy_id, strategy] : _strategy_instances) {
        strategy_ids_to_destroy.push_back(strategy_id);
    }
    
    // 逐个销毁策略实例
    size_t destroyed_count = 0;
    for (const auto& strategy_id : strategy_ids_to_destroy) {
        if (_destroy_strategy_instance_internal_safe(strategy_id) == 0) {
            destroyed_count++;
        }
    }
    
    // 清理剩余状态
    _active_strategy_ids.clear();
    _strategy_instances.clear(); // 确保清空
    _update_strategy_snapshot();
    
    STRATEGY_LOG_INFO("Destroyed {} strategy instances successfully", destroyed_count);
    return 0;
}

// ===== 依赖服务管理 =====

void StrategyAdapter::set_quote_parser(IQuoteParser* quote_parser) {
    _quote_parser = quote_parser;
    
    // 为所有现有策略实例设置行情解析器
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    for (const auto& pair : _strategy_instances) {
        pair.second->set_quote_parser(quote_parser);
    }
    
    STRATEGY_LOG_INFO("Quote parser set for strategy adapter and {} instances", _strategy_instances.size());
}

void StrategyAdapter::set_trade_handler(ITradeHandler* trade_handler) {
    _trade_handler = trade_handler;
    
    // 为所有现有策略实例设置交易处理器
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    for (const auto& pair : _strategy_instances) {
        pair.second->set_trade_handler(trade_handler);
    }
    
    STRATEGY_LOG_INFO("Trade handler set for strategy adapter and {} instances", _strategy_instances.size());
}

void StrategyAdapter::set_trading_time_manager(TradingTimeManager* trading_time_manager) {
    _trading_time_manager = trading_time_manager;

    // 为所有现有策略实例设置交易时间管理器
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    for (const auto& pair : _strategy_instances) {
        pair.second->set_trading_time_manager(trading_time_manager);
    }
}

IQuoteParser* StrategyAdapter::get_quote_parser() const {
    return _quote_parser;
}

ITradeHandler* StrategyAdapter::get_trade_handler() const {
    return _trade_handler;
}

TradingTimeManager* StrategyAdapter::get_trading_time_manager() const {
    return _trading_time_manager;
}

// ===== 数据分发接口（低锁竞争并发处理） =====

void StrategyAdapter::dispatch_index_data(const IndexData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<IndexData>(*data);
    _dispatch_data_with_snapshot("dispatch_index_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_index_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_depth_data(const DepthData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<DepthData>(*data);
    _dispatch_data_with_snapshot("dispatch_depth_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_depth_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_depth_fifty_data(const DepthFiftyData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<DepthFiftyData>(*data);
    _dispatch_data_with_snapshot("dispatch_depth_fifty_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_depth_fifty_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_entrust_data(const EntrustData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<EntrustData>(*data);
    _dispatch_data_with_snapshot("dispatch_entrust_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_entrust_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_trade_data(const TradeData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeData>(*data);
    _dispatch_data_with_snapshot("dispatch_trade_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_trade_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_order_rtn_data(const TradeOrderData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeOrderData>(*data);
    _dispatch_data_with_snapshot("dispatch_order_rtn_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_order_rtn_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_order_query_data(const TradeOrderData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeOrderData>(*data);
    _dispatch_data_with_snapshot("dispatch_order_query_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_order_query_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_fill_data(const TradeFillData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeFillData>(*data);
    _dispatch_data_with_snapshot("dispatch_fill_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_fill_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_position_data(const PositionData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<PositionData>(*data);
    _dispatch_data_with_snapshot("dispatch_position_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_position_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_account_data(const AccountData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<AccountData>(*data);
    _dispatch_data_with_snapshot("dispatch_account_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_account_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_trade_error(int error_code, const char* error_msg) {
    if (!_running.load()) return;
    
    auto error_msg_ptr = std::make_shared<std::string>(error_msg);
    _dispatch_data_with_snapshot("dispatch_trade_error", [error_code, error_msg_ptr](BaseStrategy* strategy) {
        strategy->on_trade_error(error_code, error_msg_ptr->c_str());
    });
}

void StrategyAdapter::dispatch_order_rsp_data(const TradeOrderRspData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeOrderRspData>(*data);
    _dispatch_data_with_snapshot("dispatch_order_rsp_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_order_rsp_data(data_ptr.get());
    });
}

void StrategyAdapter::dispatch_cancel_rsp_data(const TradeOrderCancelRsqData* data) {
    if (!data || !_running.load()) return;
    
    auto data_ptr = std::make_shared<TradeOrderCancelRsqData>(*data);
    _dispatch_data_with_snapshot("dispatch_cancel_rsp_data", [data_ptr](BaseStrategy* strategy) {
        strategy->on_cancel_rsp_data(data_ptr.get());
    });
}

// ===== 状态查询 =====

const char* StrategyAdapter::get_last_error() const {
    std::lock_guard<std::mutex> lock(_error_mutex);
    return _last_error.c_str();
}

std::string StrategyAdapter::get_thread_pool_status() const {
    if (!_thread_pool) {
        return R"({"status": "not_initialized"})";
    }
    
    nlohmann::json status;
    status["thread_count"] = _thread_pool->get_thread_count();
    status["queued_tasks"] = _thread_pool->get_tasks_queued();
    status["running_tasks"] = _thread_pool->get_tasks_running();
    status["total_tasks"] = _thread_pool->get_tasks_total();
    
    return status.dump();
}

// ===== 策略元信息查询 =====

std::string StrategyAdapter::get_strategy_name(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    auto it = _strategy_configs.find(strategy_id);
    return it != _strategy_configs.end() ? it->second.name : "";
}

std::string StrategyAdapter::get_strategy_description(const std::string& strategy_id) const {
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    auto it = _strategy_configs.find(strategy_id);
    return it != _strategy_configs.end() ? it->second.description : "";
}

// ===== 数据库管理器 =====

void StrategyAdapter::set_db_manager(DatabaseManager* db_manager) {
    _db_manager = db_manager;

     // 为所有现有策略实例设置行情解析器
    std::lock_guard<std::mutex> lock(_strategy_management_mutex);
    for (const auto& pair : _strategy_instances) {
        pair.second->set_db_manager(db_manager);
    }
    
    STRATEGY_LOG_INFO("Database manager set for strategy adapter and {} instances", _strategy_instances.size());
}

DatabaseManager* StrategyAdapter::get_db_manager() const {
    return _db_manager;
}

// ===== 私有方法实现 =====

void StrategyAdapter::_set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(_error_mutex);
    _last_error = error;
    STRATEGY_LOG_ERROR("StrategyAdapter error: {}", error);
}

bool StrategyAdapter::_check_state(bool require_initialized, bool require_running) {
    if (require_initialized && !_initialized.load()) {
        _set_error("Adapter not initialized");
        return false;
    }
    
    if (require_running && !_running.load()) {
        _set_error("Adapter not running");
        return false;
    }
    
    return true;
}

void StrategyAdapter::_inject_dependencies(BaseStrategy* strategy) {
    if (strategy) {
        strategy->set_quote_parser(_quote_parser);
        strategy->set_trade_handler(_trade_handler);
        strategy->set_db_manager(_db_manager);
        strategy->set_trading_time_manager(_trading_time_manager);
    }
}

void StrategyAdapter::_update_strategy_snapshot() {
    // 此方法应该在持有 _strategy_management_mutex 的上下文中调用
    auto new_snapshot = StrategySnapshot::create_from_instances(_strategy_instances, _active_strategy_ids);
    
    // 使用写锁更新快照
    {
        std::unique_lock<std::shared_mutex> lock(_snapshot_mutex);
        _strategy_snapshot = new_snapshot;
    }
}

std::shared_ptr<const StrategyAdapter::StrategySnapshot> StrategyAdapter::_get_strategy_snapshot() const {
    std::shared_lock<std::shared_mutex> lock(_snapshot_mutex);
    return _strategy_snapshot;
}

template<typename Dispatcher>
void StrategyAdapter::_dispatch_data_with_snapshot(const std::string& task_name, Dispatcher dispatcher) {
    if (!_thread_pool) {
        STRATEGY_LOG_WARN("Thread pool not available for {}", task_name);
        return;
    }

    // 获取当前快照（使用读锁，锁竞争很小）
    auto snapshot = _get_strategy_snapshot();
    
    if (!snapshot || snapshot->active_strategies.empty()) {
        return; // 没有活跃策略
    }

    // 并发分发到所有活跃策略（快照已复制，无需锁）
    for (BaseStrategy* strategy : snapshot->active_strategies) {
        if (strategy) {
            _thread_pool->detach_task([strategy, dispatcher, task_name]() {
                try {
                    dispatcher(strategy);
                } catch (const std::exception& e) {
                    STRATEGY_LOG_ERROR("Exception in {} for strategy: {}", task_name, e.what());
                }
            });
        }
    }
}

int StrategyAdapter::_destroy_strategy_instance_internal_safe(const std::string& strategy_id) {
    auto it = _strategy_instances.find(strategy_id);
    if (it == _strategy_instances.end()) {
        return -1;
    }
    
    try {
        STRATEGY_LOG_INFO("Destroying strategy instance: {}", strategy_id);
        
        // 从活跃列表中移除
        _active_strategy_ids.erase(strategy_id);
        
        // 安全地获取策略指针的副本用于日志
        std::string strategy_id_copy = strategy_id; // 创建副本避免悬空引用
        
        // 停止策略
        if (it->second) {
            it->second->stop();
            // 清理策略资源
            it->second->release();
        }
        
        // 删除策略实例
        _strategy_instances.erase(it);
        
        STRATEGY_LOG_INFO("Strategy instance destroyed successfully: {}", strategy_id_copy);
        return 0;
        
    } catch (const std::exception& e) {
        STRATEGY_LOG_ERROR("Exception destroying strategy instance {}: {}", strategy_id, e.what());
        // 强制删除，防止资源泄漏
        _active_strategy_ids.erase(strategy_id);
        if (it != _strategy_instances.end()) {
            _strategy_instances.erase(it);
        }
        return -1;
    }
}

int StrategyAdapter::_destroy_strategy_instance_internal(const std::string& strategy_id) {
    int result = _destroy_strategy_instance_internal_safe(strategy_id);
    if (result == 0) {
        _update_strategy_snapshot();
    }
    return result;
}

StrategyFactory StrategyAdapter::_create_builtin_strategy_factory(const std::string& module_name) {
    if (module_name == "StopProfitLossStrategy") {
        return []() -> std::unique_ptr<BaseStrategy> {
            return std::make_unique<StopProfitLossStrategy>();
        };
    }
    return nullptr;
}

std::vector<std::string> StrategyAdapter::get_builtin_strategy_modules() const {
    return {"StopProfitLossStrategy"};
}

// ===== CPU绑核实现 =====

void StrategyAdapter::_bind_thread_to_cpu(std::size_t thread_idx) {
    try {
        // 获取系统CPU核心数
        const std::size_t num_cpus = std::thread::hardware_concurrency();
        if (num_cpus == 0) {
            STRATEGY_LOG_WARN("Cannot determine CPU count, skipping CPU affinity for thread {}", thread_idx);
            return;
        }

        const std::size_t cores_size = _cores.size();
        // 计算要绑定的CPU核心（循环分配）
        const std::size_t cpu_id = _cores[thread_idx % cores_size];
        
        // 创建CPU亲和性掩码，只绑定到指定核心
        std::vector<bool> affinity(num_cpus, false);
        affinity[cpu_id] = true;
        
        // 设置当前线程的CPU亲和性
        if (BS::this_thread::set_os_thread_affinity(affinity)) {
            STRATEGY_LOG_INFO("Thread {} bound to CPU core {}", thread_idx, cpu_id);
        } else {
            STRATEGY_LOG_WARN("Failed to bind thread {} to CPU core {}", thread_idx, cpu_id);
        }
        
    } catch (const std::exception& e) {
        STRATEGY_LOG_ERROR("Exception binding thread {} to CPU: {}", thread_idx, e.what());
    }
}

NS_FT_END