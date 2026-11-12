#ifndef STRATEGY_ADAPTER_H
#define STRATEGY_ADAPTER_H
#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include "strategy/base_strategy.h"
#include "database/database_manager.hpp"
#include "core/macros.h"
#include "utils/BS_thread_pool.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>

NS_FT_BEGIN

// 前向声明
class BaseStrategy;
class IQuoteParser;
class ITradeHandler;
class TradingTimeManager;
class TwapAlgorithm;

/**
 * 策略工厂函数类型定义
 * 用于创建具体的策略实例
 */
using StrategyFactory = std::function<std::unique_ptr<BaseStrategy>()>;

/**
 * 策略模块注册函数类型定义
 * 用于注册策略模块到全局注册表
 */
using StrategyModuleRegistrar = std::function<void(const std::string &, StrategyFactory)>;

/**
 * 策略适配器
 *
 * 负责统一管理和抽象多个策略实例，主要功能：
 * 1. 多策略并发管理：支持多个策略同时运行
 * 2. 策略注册和创建：根据配置文件中的ID绑定具体策略
 * 3. 数据并发分发：使用线程池将行情和交易数据并发分发给所有策略（低锁竞争设计）
 * 4. 依赖注入：为策略实例注入行情和交易服务接口
 * 5. 生命周期管理：管理策略的注册、初始化、启动、停止等
 * 6. 配置管理：从配置文件加载策略信息和默认配置
 *
 * 设计特点：
 * - 工厂模式：通过策略工厂创建不同类型的策略
 * - 适配器模式：统一不同策略的接口差异
 * - 代理模式：代理策略与底层服务的交互
 * - 多策略并发：支持多个策略同时运行，互不阻塞
 * - 配置驱动：根据JSON配置文件自动注册和管理策略
 * - 线程池优化：使用BS::thread_pool进行高效的任务调度
 * - 低锁竞争：数据分发使用快照机制，最小化锁竞争
 */
class StrategyAdapter
{
public:
    /**
     * 构造函数
     * @param thread_count 线程池线程数量，默认为0（自动检测CPU核心数）
     * @param cores 绑定的CPU核心列表，默认为空（不绑定）
     */
    StrategyAdapter(size_t thread_count = 0, const std::vector<int>& cores = {});

    /**
     * 析构函数
     */
    ~StrategyAdapter();

    // ===== 生命周期管理 =====

    /**
     * 初始化适配器
     * @return 0成功，负数失败
     */
    int init();

    /**
     * 启动适配器
     * @return 0成功，负数失败
     */
    int start();

    /**
     * 停止适配器
     * @return 0成功，负数失败
     */
    int stop();

    /**
     * 释放适配器资源
     */
    void release();

    /**
     * 检查适配器是否已初始化
     * @return true已初始化，false未初始化
     */
    bool is_initialized() const;

    /**
     * 检查适配器是否正在运行
     * @return true正在运行，false未运行
     */
    bool is_running() const;

    // ===== 配置文件驱动的策略管理 =====

    /**
     * 从配置文件加载策略配置
     * @param config_json 配置JSON对象
     * @return 0成功，负数失败
     */
    int load_strategies_from_config(const nlohmann::json &config_json);

    /**
     * 根据策略ID注册策略（从配置信息）
     * @param strategy_id 策略ID
     * @param config 策略配置信息
     * @return 0成功，负数失败
     */
    int register_strategy_from_config(const std::string &strategy_id, const StrategyConfig &config);

    // ===== 策略注册和管理 =====

    /**
     * 注册策略工厂
     * @param strategy_id 策略ID
     * @param factory 策略工厂函数
     * @return 0成功，负数失败
     */
    int register_strategy(const std::string &strategy_id, StrategyFactory factory);

    /**
     * 注销策略工厂
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int unregister_strategy(const std::string &strategy_id);

    /**
     * 获取所有已注册的策略ID列表
     * @return 策略ID列表
     */
    std::vector<std::string> get_registered_strategy_ids() const;

    /**
     * 检查策略是否已注册
     * @param strategy_id 策略ID
     * @return true已注册，false未注册
     */
    bool is_strategy_registered(const std::string &strategy_id) const;

    /**
     * 获取策略配置信息
     * @param strategy_id 策略ID
     * @return 策略配置信息，未找到返回nullptr
     */
    const StrategyConfig *get_strategy_config(const std::string &strategy_id) const;

    // ===== 多策略实例管理 =====

    /**
     * 创建并启动策略实例
     * @param strategy_id 策略ID
     * @param config 策略配置，为空时使用默认配置
     * @return 0成功，负数失败
     */
    int create_strategy_instance(const std::string &strategy_id, const char *config = nullptr);

    /**
     * 启动指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int start_strategy_instance(const std::string &strategy_id);

    /**
     * 停止指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int stop_strategy_instance(const std::string &strategy_id);

    /**
     * 销毁指定策略实例
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int destroy_strategy_instance(const std::string &strategy_id);

    /**
     * 获取指定策略实例
     * @param strategy_id 策略ID
     * @return 策略实例指针，未找到返回nullptr
     */
    BaseStrategy *get_strategy_instance(const std::string &strategy_id) const;

    /**
     * 获取所有活跃策略实例的ID列表
     * @return 策略ID列表
     */
    std::vector<std::string> get_active_strategy_ids() const;

    /**
     * 检查策略实例是否存在且活跃
     * @param strategy_id 策略ID
     * @return true存在且活跃，false不存在或未活跃
     */
    bool is_strategy_instance_active(const std::string &strategy_id) const;

    /**
     * 获取活跃策略实例数量
     * @return 策略实例数量
     */
    size_t get_active_strategy_count() const;

    // ===== 批量策略操作 =====

    /**
     * 启动所有已注册的策略实例
     * @return 成功启动的策略数量
     */
    int start_all_strategies();

    /**
     * 停止所有活跃的策略实例
     * @return 成功停止的策略数量
     */
    int stop_all_strategies();

    /**
     * 销毁所有策略实例
     * @return 成功销毁的策略数量
     */
    int destroy_all_strategies();

    // ===== 依赖服务管理 =====

    /**
     * 设置行情解析器
     * @param quote_parser 行情解析器接口
     */
    void set_quote_parser(IQuoteParser *quote_parser);

    /**
     * 设置交易处理器
     * @param trade_handler 交易处理器接口
     */
    void set_trade_handler(ITradeHandler *trade_handler);

    /**
     * 获取行情解析器
     * @return 行情解析器接口
     */
    IQuoteParser *get_quote_parser() const;

    /**
     * 获取交易处理器
     * @return 交易处理器接口
     */
    ITradeHandler *get_trade_handler() const;

    /**
     * 设置交易时间管理器
     * @param trading_time_manager 交易时间管理器指针
     */
    void set_trading_time_manager(TradingTimeManager *trading_time_manager);

    /**
     * 获取交易时间管理器
     * @return 交易时间管理器指针
     */
    TradingTimeManager *get_trading_time_manager() const;

    // ===== 数据分发接口（低锁竞争并发处理） =====

    /**
     * 分发指数数据到所有活跃策略
     * @param data 指数数据
     */
    void dispatch_index_data(const IndexData *data);

    /**
     * 分发深度数据到所有活跃策略
     * @param data 深度数据
     */
    void dispatch_depth_data(const DepthData *data);

    /**
     * 分发50笔深度数据到所有活跃策略
     * @param data 50笔深度数据
     */
    void dispatch_depth_fifty_data(const DepthFiftyData *data);

    /**
     * 分发委托数据到所有活跃策略
     * @param data 委托数据
     */
    void dispatch_entrust_data(const EntrustData *data);

    /**
     * 分发成交数据到所有活跃策略
     * @param data 成交数据
     */
    void dispatch_trade_data(const TradeData *data);

    /**
     * 分发订单回调数据到所有活跃策略
     * @param data 订单数据
     */
    void dispatch_order_rtn_data(const TradeOrderData *data);

    /**
     * 分发订单数据到所有活跃策略
     * @param data 订单数据
     */
    void dispatch_order_query_data(const TradeOrderData *data);

    /**
     * 分发成交回报数据到所有活跃策略
     * @param data 成交回报数据
     */
    void dispatch_fill_data(const TradeFillData *data);

    /**
     * 分发持仓数据到所有活跃策略
     * @param data 持仓数据
     */
    void dispatch_position_data(const PositionData *data);

    /**
     * 分发账户数据到所有活跃策略
     * @param data 账户数据
     */
    void dispatch_account_data(const AccountData *data);

    /**
     * 分发交易错误到所有活跃策略
     * @param error_code 错误代码
     * @param error_msg 错误信息
     */
    void dispatch_trade_error(int error_code, const char *error_msg);

    /**
     * 分发委托响应数据到所有活跃策略
     * @param data 委托响应数据
     */
    void dispatch_order_rsp_data(const TradeOrderRspData *data);

    /**
     * 分发撤单响应数据到所有活跃策略
     * @param data 撤单响应数据
     */
    void dispatch_cancel_rsp_data(const TradeOrderCancelRsqData *data);

    // ===== 状态查询 =====

    /**
     * 获取最后的错误信息
     * @return 错误信息字符串
     */
    const char *get_last_error() const;

    /**
     * 获取线程池状态信息
     * @return JSON格式的线程池状态
     */
    std::string get_thread_pool_status() const;

    // ===== 策略元信息查询 =====

    /**
     * 获取策略名称
     * @param strategy_id 策略ID
     * @return 策略名称，未找到返回空字符串
     */
    std::string get_strategy_name(const std::string &strategy_id) const;

    /**
     * 获取策略描述
     * @param strategy_id 策略ID
     * @return 策略描述，未找到返回空字符串
     */
    std::string get_strategy_description(const std::string &strategy_id) const;

    // ===== 数据库管理器 =====
    /**
     * 设置数据库管理器
     * @param db_manager 数据库管理器指针
     */
    void set_db_manager(DatabaseManager *db_manager);

    /**
     * 获取数据库管理器
     * @return 数据库管理器指针
     */
    DatabaseManager *get_db_manager() const;

    /**
     * 获取所有内置策略模块名称
     * @return 策略模块名称列表
     */
    std::vector<std::string> get_builtin_strategy_modules() const;

private:
    // ===== 私有成员 =====

    // 策略快照结构体（用于低锁竞争数据分发）
    struct StrategySnapshot
    {
        std::vector<BaseStrategy *> active_strategies; ///< 活跃策略指针列表

        StrategySnapshot() = default;

        // 从策略实例创建新快照
        static std::shared_ptr<const StrategySnapshot> create_from_instances(
            const std::unordered_map<std::string, std::unique_ptr<BaseStrategy>> &instances,
            const std::unordered_set<std::string> &active_ids)
        {

            auto snapshot = std::make_shared<StrategySnapshot>();
            snapshot->active_strategies.reserve(active_ids.size());

            for (const auto &strategy_id : active_ids)
            {
                auto it = instances.find(strategy_id);
                if (it != instances.end() && it->second)
                {
                    snapshot->active_strategies.push_back(it->second.get());
                }
            }

            return snapshot;
        }
    };

    // 策略管理
    std::unordered_map<std::string, StrategyFactory> _strategy_factories;               ///< 策略工厂映射
    std::unordered_map<std::string, StrategyConfig> _strategy_configs;                  ///< 策略配置映射
    std::unordered_map<std::string, std::unique_ptr<BaseStrategy>> _strategy_instances; ///< 策略实例映射
    std::unordered_set<std::string> _active_strategy_ids;                               ///< 活跃策略ID集合

    // 策略快照（使用锁保护以兼容C++14）
    std::shared_ptr<const StrategySnapshot> _strategy_snapshot;

    // 并发控制
    std::unique_ptr<BS::thread_pool<>> _thread_pool; ///< 线程池
    mutable std::mutex _strategy_management_mutex;   ///< 策略管理锁（仅用于策略生命周期管理）
    mutable std::shared_mutex _snapshot_mutex;       ///< 快照读写锁（用于数据分发）
    mutable std::mutex _error_mutex;                 ///< 错误信息锁

    // 状态管理
    std::atomic<bool> _initialized{false}; ///< 初始化状态
    std::atomic<bool> _running{false};     ///< 运行状态
    std::string _last_error;               ///< 最后错误信息

    // 依赖服务
    IQuoteParser *_quote_parser{nullptr};     ///< 行情解析器
    ITradeHandler *_trade_handler{nullptr};   ///< 交易处理器
    DatabaseManager *_db_manager{nullptr};    ///< 数据库管理器
    TradingTimeManager *_trading_time_manager{nullptr}; ///< 交易时间管理器

    // 配置参数
    size_t _thread_count; ///< 线程池线程数量
    std::vector<int> _cores; ///< 绑定的CPU核心列表

    // ===== 私有方法 =====

    /**
     * 更新策略快照（线程安全）
     * 在策略实例变化后调用，更新快照
     */
    void _update_strategy_snapshot();

    /**
     * 获取当前策略快照（读锁保护）
     * @return 当前策略快照的常量引用
     */
    std::shared_ptr<const StrategySnapshot> _get_strategy_snapshot() const;

    /**
     * 低锁竞争数据分发到所有活跃策略
     * @param task_name 任务名称（用于日志）
     * @param dispatcher 分发函数
     */
    template <typename Dispatcher>
    void _dispatch_data_with_snapshot(const std::string &task_name, Dispatcher dispatcher);

    /**
     * 设置错误信息（线程安全）
     * @param error 错误信息
     */
    void _set_error(const std::string &error);

    /**
     * 检查适配器状态
     * @param require_initialized 是否要求已初始化
     * @param require_running 是否要求正在运行
     * @return true状态正确，false状态错误
     */
    bool _check_state(bool require_initialized = true, bool require_running = false);

    /**
     * 为策略实例注入依赖服务
     * @param strategy 策略实例
     */
    void _inject_dependencies(BaseStrategy *strategy);

    /**
     * 安全销毁指定策略实例（内部版本，不加锁，不更新快照）
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int _destroy_strategy_instance_internal_safe(const std::string &strategy_id);

    /**
     * 销毁指定策略实例（内部版本，不加锁）
     * @param strategy_id 策略ID
     * @return 0成功，负数失败
     */
    int _destroy_strategy_instance_internal(const std::string &strategy_id);

    /**
     * 根据模块名创建内置策略工厂
     * @param module_name 策略模块名
     * @return 策略工厂函数，失败返回nullptr
     */
    StrategyFactory _create_builtin_strategy_factory(const std::string &module_name);

    /**
     * 绑定线程到指定CPU核心
     * @param thread_idx 线程索引
     */
    void _bind_thread_to_cpu(std::size_t thread_idx);
};

NS_FT_END
#endif // STRATEGY_ADAPTER_H