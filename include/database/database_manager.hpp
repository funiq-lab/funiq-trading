#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <map>
#include "core/macros.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include "types/trade_types.h"
#include "types/strategy_types.h"

NS_FT_BEGIN
template<typename T>
struct BatchQueryResult
{
    std::vector<T> records;
    int total_count = 0;    // 总记录数
    int returned_count = 0; // 返回记录数
    bool has_more = false;  // 是否还有更多数据
};

using StrategyPendingOrderBatchResult = BatchQueryResult<StrategyPendingOrder>;
using StrategyOrderBatchResult = BatchQueryResult<StrategyOrder>;

/**
 * 批量操作结果
 */
struct BatchOperationResult
{
    int total_count = 0;        // 总操作数
    int success_count = 0;      // 成功数
    int failed_count = 0;       // 失败数
    std::vector<std::string> errors; // 错误信息列表
    bool transaction_success = true; // 事务是否成功
    
    // 获取成功率
    double get_success_rate() const {
        return total_count > 0 ? (double)success_count / total_count : 0.0;
    }
};

/**
 * 批量更新选项
 */
struct BatchUpdateOptions
{
    bool use_transaction = true;        // 是否使用事务
    bool stop_on_first_error = false;  // 遇到第一个错误时是否停止
    bool ignore_not_found = true;      // 是否忽略找不到记录的错误
    int batch_size = 1000;            // 批处理大小
};

// 数据库管理器类
class DatabaseManager
{
public:
    /**
     * 默认构造函数
     */
    DatabaseManager();

    /**
     * 虚析构函数
     */
    virtual ~DatabaseManager() = default;

    // ===== 生命周期管理 =====

    /**
     * 初始化数据库管理器
     * 从 .env 文件和环境变量读取配置
     * @return true成功，false失败
     */
    bool initialize();

    /**
     * 关闭数据库连接
     * @return true成功，false失败
     */
    bool close();

    /**
     * 检查是否已初始化
     * @return true已初始化，false未初始化
     */
    bool is_initialized() const { return _initialized; }

    // ===== 策略挂单表操作 =====

    /**
     * 插入策略挂单（创建时使用，只需要基本信息）
     * @param strategy_id 策略ID
     * @param pending_order_info 策略挂单信息(JSON)
     * @return 新插入记录的ID，-1表示失败
     */
    int64_t insert_strategy_pending_order_basic(const std::string &strategy_id,
                                                const std::string &pending_order_info);

    /**
     * 完整插入策略挂单
     */
    bool insert_strategy_pending_order(const StrategyPendingOrder &order);
    
    /**
     * 更新策略挂单
     */
    bool update_strategy_pending_order(const StrategyPendingOrder &order);
    
    bool delete_strategy_pending_order(int64_t id);
    std::optional<StrategyPendingOrder> query_strategy_pending_order_by_id(int64_t id);
    std::vector<StrategyPendingOrder> query_strategy_pending_orders_by_strategy_id(const std::string &strategy_id);

    /**
     * 批量查询策略挂单
     */
    StrategyPendingOrderBatchResult batch_query_strategy_pending_orders(const StrategyPendingOrderQuery &query);

    // ===== 策略订单表操作 =====

    /**
     * 插入策略订单（策略触发时创建）
     * @param pending_order_id 关联的策略挂单ID
     * @param security_code 证券代码
     * @param side 买卖方向
     * @param sign 订单标识
     * @param order_type 订单类型
     * @param price 价格
     * @param qty 数量
     * @param order_id 订单ID（必需）
     * @return 新插入记录的ID，-1表示失败
     */
    int64_t insert_strategy_order_basic(int64_t pending_order_id,
                                        const std::string &security_code,
                                        OrderSide side,
                                        OrderSign sign,
                                        OrderType order_type,
                                        double price,
                                        uint64_t qty,
                                        int64_t order_id);

    /**
     * 完整插入策略订单
     */
    bool insert_strategy_order(const StrategyOrder &order);
    
    /**
     * 更新策略订单
     */
    bool update_strategy_order(const StrategyOrder &order);
    
    /**
     * 从交易回调数据更新策略订单
     * @param id 策略订单ID
     * @param trade_data 交易数据
     * @return true成功，false失败
     */
    bool update_strategy_order_from_trade_data(int64_t id, const TradeOrderData &trade_data);
    
    /**
     * 更新策略订单的order_id（当收到委托响应时）
     * @param id 策略订单ID
     * @param order_id 订单ID
     * @return true成功，false失败
     */
    bool update_strategy_order_id(int64_t id, int64_t order_id);
    
    bool delete_strategy_order(int64_t id);
    std::optional<StrategyOrder> query_strategy_order_by_id(int64_t id);
    std::optional<StrategyOrder> query_strategy_order_by_order_id(int64_t order_id);
    std::vector<StrategyOrder> query_strategy_orders_by_pending_order_id(int64_t pending_order_id);
    std::vector<StrategyOrder> query_strategy_orders_by_strategy_id(const std::string &strategy_id);

    /**
     * 批量查询策略订单
     */
    StrategyOrderBatchResult batch_query_strategy_orders(const StrategyOrderQuery &query);

    // ===== 联表查询 =====

    /**
     * 查询策略挂单及其关联的所有订单
     */
    std::vector<StrategyPendingOrderWithOrders> query_strategy_pending_orders_with_orders(const std::string &strategy_id);

    // ===== 事务操作 =====

    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    // ===== 备份操作 =====

    /**
     * 创建数据库备份
     * @param backup_file_path 备份文件路径，为空则自动生成
     * @return true成功，false失败
     */
    bool create_backup(const std::string &backup_file_path = "");

    // ===== 工具函数 =====

    /**
     * 获取最后的错误信息
     * @return 错误描述字符串
     */
    std::string get_last_error() const;

    /**
     * 更新策略挂单状态
     * @param id 挂单ID
     * @param status 新状态
     * @return true成功，false失败
     */
    bool update_strategy_pending_order_status(int64_t id, PendingOrderStatus status);

    // ===== 批量操作接口 =====

    /**
     * 批量插入策略挂单
     * @param orders 策略挂单列表
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_insert_strategy_pending_orders(
        const std::vector<StrategyPendingOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量更新策略挂单
     * @param orders 策略挂单列表（必须包含有效的ID）
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_update_strategy_pending_orders(
        const std::vector<StrategyPendingOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量插入策略订单
     * @param orders 策略订单列表
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_insert_strategy_orders(
        const std::vector<StrategyOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量更新策略订单
     * @param orders 策略订单列表（必须包含有效的ID）
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_update_strategy_orders(
        const std::vector<StrategyOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量更新策略挂单状态
     * @param ids 挂单ID列表
     * @param status 新状态
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_update_strategy_pending_order_status(
        const std::vector<int64_t>& ids,
        PendingOrderStatus status,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量从交易数据更新策略订单
     * @param updates 更新数据列表（ID和交易数据的配对）
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_update_strategy_orders_from_trade_data(
        const std::vector<std::pair<int64_t, TradeOrderData>>& updates,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量删除策略挂单
     * @param ids 挂单ID列表
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_delete_strategy_pending_orders(
        const std::vector<int64_t>& ids,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量删除策略订单
     * @param ids 策略订单ID列表
     * @param options 批量操作选项
     * @return 批量操作结果
     */
    BatchOperationResult batch_delete_strategy_orders(
        const std::vector<int64_t>& ids,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量Upsert策略挂单（插入或更新）
     * @param orders 策略挂单列表
     * @param options 批量操作选项
     * @return 批量操作结果
     * @note 如果ID > 0则更新，否则插入
     */
    BatchOperationResult batch_upsert_strategy_pending_orders(
        const std::vector<StrategyPendingOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

    /**
     * 批量Upsert策略订单（插入或更新）
     * @param orders 策略订单列表
     * @param options 批量操作选项
     * @return 批量操作结果
     * @note 如果ID > 0则更新，否则插入
     */
    BatchOperationResult batch_upsert_strategy_orders(
        const std::vector<StrategyOrder>& orders,
        const BatchUpdateOptions& options = BatchUpdateOptions());

private:
    // ===== 私有成员变量 =====
    std::string _db_path;                  // 数据库文件路径
    std::string _backup_path;              // 备份目录路径
    std::unique_ptr<SQLite::Database> _db; // SQLite数据库连接
    bool _initialized = false;             // 初始化状态
    mutable std::string _last_error;       // 最后的错误信息

    // ===== 私有方法 =====

    /**
     * 从环境变量加载配置
     * @return true成功，false失败
     */
    bool load_config_from_env();

    /**
     * 创建数据库目录
     * @return true成功，false失败
     */
    bool create_database_directory();

    /**
     * 创建数据库表
     * @return true成功，false失败
     */
    bool create_tables();

    /**
     * 创建数据库索引
     * @return true成功，false失败
     */
    bool create_indexes();

    /**
     * 设置错误信息
     * @param error 错误描述
     */
    void set_error(const std::string &error) const;

    /**
     * 辅助函数：将查询结果转换为策略挂单对象
     */
    StrategyPendingOrder row_to_strategy_pending_order(SQLite::Statement &query);

    /**
     * 辅助函数：将查询结果转换为策略订单对象
     */
    StrategyOrder row_to_strategy_order(SQLite::Statement &query);

    /**
     * 获取当前时间戳（毫秒）
     */
    uint64_t get_current_timestamp();

    /**
     * 执行批量操作的通用模板
     * @param items 待操作的项目列表
     * @param operation 操作函数
     * @param options 批量操作选项
     * @param operation_name 操作名称（用于日志）
     * @return 批量操作结果
     */
    template<typename T, typename OperationFunc>
    BatchOperationResult execute_batch_operation(
        const std::vector<T>& items,
        OperationFunc operation,
        const BatchUpdateOptions& options,
        const std::string& operation_name);

    /**
     * 分批处理大数据集
     * @param total_size 总数据大小
     * @param batch_size 批处理大小
     * @param processor 处理函数
     * @return 批量操作结果
     */
    template<typename ProcessorFunc>
    BatchOperationResult process_in_batches(
        int total_size,
        int batch_size,
        ProcessorFunc processor);
};
NS_FT_END

#endif // DATABASE_MANAGER_HPP