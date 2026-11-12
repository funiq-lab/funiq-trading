#include "database/database_manager.hpp"
#include "logger/logger.hpp"
#include "laserpants/dotenv/dotenv.h"
#include "utils/file.hpp"
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <numeric>

NS_FT_BEGIN

DatabaseManager::DatabaseManager()
    : _initialized(false)
{
}

bool DatabaseManager::initialize()
{
    if (_initialized)
    {
        LOG_WARN("Database already initialized");
        return true;
    }

    // 加载 .env 文件
    try
    {
        dotenv::init();
        LOG_INFO("Loaded .env file for database configuration");
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to load .env file: {}, using environment variables only", e.what());
    }

    // 从环境变量加载配置
    if (!load_config_from_env())
    {
        LOG_ERROR("Failed to load database configuration");
        return false;
    }

    // 创建数据库目录
    if (!create_database_directory())
    {
        return false;
    }

    try
    {
        // 创建数据库连接
        _db = std::make_unique<SQLite::Database>(_db_path,
                                                 SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        // 开启外键约束
        _db->exec("PRAGMA foreign_keys = ON");

        // 设置WAL模式以提高并发性能
        _db->exec("PRAGMA journal_mode = WAL");

        // 设置同步模式
        _db->exec("PRAGMA synchronous = NORMAL");

        // 创建表和索引
        if (!create_tables() || !create_indexes())
        {
            return false;
        }

        _initialized = true;
        LOG_INFO("Database initialized successfully: {}", _db_path);
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to initialize database: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::load_config_from_env()
{
    // 从环境变量读取数据库路径，提供默认值
    const char *db_path_env = std::getenv("FT_DB_PATH");
    if (db_path_env)
    {
        _db_path = std::string(db_path_env);
    }
    else
    {
        _db_path = "data/trading.db"; // 默认路径
    }

    // 从环境变量读取备份路径，提供默认值
    const char *backup_path_env = std::getenv("FT_DB_BACKUP_PATH");
    if (backup_path_env)
    {
        _backup_path = std::string(backup_path_env);
    }
    else
    {
        _backup_path = "data/backup"; // 默认备份目录
    }

    LOG_INFO("Database configuration loaded - db_path: {}, backup_path: {}",
             _db_path, _backup_path);

    return true;
}

bool DatabaseManager::create_database_directory()
{
    try
    {
        std::string db_dir = get_parent_path(_db_path);
        
        if (!db_dir.empty() && db_dir != ".") // 排除当前目录的情况
        {
            if (create_directories(db_dir))
            {
                LOG_INFO("Created database directory: {}", db_dir);
            }
            else
            {
                LOG_ERROR("Failed to create database directory: {}", db_dir);
                set_error("Failed to create database directory: " + db_dir);
                return false;
            }
        }

        // 同时创建备份目录
        if (!_backup_path.empty())
        {
            if (create_directories(_backup_path))
            {
                LOG_INFO("Created backup directory: {}", _backup_path);
            }
            else
            {
                LOG_ERROR("Failed to create backup directory: {}", _backup_path);
                set_error("Failed to create backup directory: " + _backup_path);
                return false;
            }
        }

        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to create database directory: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::close()
{
    if (!_initialized)
    {
        return true;
    }

    try
    {
        _db.reset();
        _initialized = false;
        LOG_INFO("Database closed successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to close database: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::create_backup(const std::string &backup_file_path)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        std::string backup_file;
        if (backup_file_path.empty())
        {
            // 自动生成备份文件名
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << _backup_path << "/trading_backup_" << time_t << ".db";
            backup_file = ss.str();
        }
        else
        {
            backup_file = backup_file_path;
        }

        // 执行备份
        _db->backup(backup_file.c_str(), SQLite::Database::Save);

        LOG_INFO("Database backup created successfully: {}", backup_file);
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to create backup: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::create_tables()
{
    try
    {
        // 创建策略挂单表 - 添加 status 字段
        const std::string create_pending_orders_sql = R"(
            CREATE TABLE IF NOT EXISTS strategy_pending_orders (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                strategy_id TEXT NOT NULL,
                pending_order_info TEXT,
                status INTEGER DEFAULT 1,
                create_time INTEGER NOT NULL,
                update_time INTEGER DEFAULT 0
            );
        )";

        // 创建策略订单表
        const std::string create_orders_sql = R"(
            CREATE TABLE IF NOT EXISTS strategy_orders (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                pending_order_id INTEGER NOT NULL,
                strategy_id TEXT NOT NULL,
                pending_order_info TEXT,
                pending_submit_time INTEGER DEFAULT 0,
                
                -- 账户信息
                login_id TEXT,
                cust_id TEXT,
                fund_account_id TEXT,
                account_id TEXT,
                
                -- 订单核心信息
                security_code TEXT NOT NULL,
                sign INTEGER DEFAULT 2,
                order_id INTEGER DEFAULT 0,
                exchange_order_id TEXT,
                batch_id INTEGER DEFAULT 0,
                
                side INTEGER NOT NULL,
                order_type INTEGER NOT NULL,
                status INTEGER DEFAULT 0,
                status_msg TEXT,
                
                price REAL NOT NULL,
                qty INTEGER NOT NULL,
                leaves_qty INTEGER DEFAULT 0,
                cum_qty INTEGER DEFAULT 0,
                avg_price REAL DEFAULT 0.0,
                cum_amt REAL DEFAULT 0.0,
                
                order_time INTEGER DEFAULT 0,
                update_time INTEGER DEFAULT 0,
                create_time INTEGER NOT NULL,
                
                error_code INTEGER DEFAULT 0,
                error_msg TEXT,
                
                FOREIGN KEY (pending_order_id) REFERENCES strategy_pending_orders(id) ON DELETE CASCADE
            );
        )";

        _db->exec(create_pending_orders_sql);
        _db->exec(create_orders_sql);

        LOG_INFO("Database tables created successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to create tables: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::create_indexes()
{
    try
    {
        const std::vector<std::string> indexes = {
            // 策略挂单表索引
            "CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_strategy_id ON strategy_pending_orders(strategy_id);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_status ON strategy_pending_orders(status);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_pending_orders_create_time ON strategy_pending_orders(create_time);",

            // 策略订单表索引
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_pending_order_id ON strategy_orders(pending_order_id);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_strategy_id ON strategy_orders(strategy_id);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_order_id ON strategy_orders(order_id);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_security_code ON strategy_orders(security_code);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_status ON strategy_orders(status);",
            "CREATE INDEX IF NOT EXISTS idx_strategy_orders_create_time ON strategy_orders(create_time);"};

        for (const auto &sql : indexes)
        {
            _db->exec(sql);
        }

        LOG_INFO("Database indexes created successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to create indexes: " + std::string(e.what()));
        return false;
    }
}

// ===== 策略挂单表操作 =====

int64_t DatabaseManager::insert_strategy_pending_order_basic(const std::string &strategy_id,
                                                             const std::string &pending_order_info)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return -1;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            INSERT INTO strategy_pending_orders 
            (strategy_id, pending_order_info, status, create_time, update_time) 
            VALUES (?, ?, ?, ?, ?);
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, strategy_id);
        query.bind(2, pending_order_info);
        query.bind(3, static_cast<int>(PendingOrderStatus::ACTIVE)); // 默认为激活状态
        query.bind(4, static_cast<int64_t>(current_time)); // create_time
        query.bind(5, static_cast<int64_t>(current_time)); // update_time

        query.exec();
        int64_t insert_id = _db->getLastInsertRowid();
        return insert_id;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to insert strategy pending order: " + std::string(e.what()));
        return -1;
    }
}

bool DatabaseManager::insert_strategy_pending_order(const StrategyPendingOrder &order)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            INSERT INTO strategy_pending_orders 
            (strategy_id, pending_order_info, status, create_time, update_time) 
            VALUES (?, ?, ?, ?, ?);
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order.strategy_id);
        query.bind(2, order.pending_order_info);
        query.bind(3, static_cast<int>(order.status));
        query.bind(4, static_cast<int64_t>(order.create_time > 0 ? order.create_time : current_time));
        query.bind(5, static_cast<int64_t>(order.update_time > 0 ? order.update_time : current_time));

        query.exec();
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to insert strategy pending order: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::update_strategy_pending_order(const StrategyPendingOrder &order)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            UPDATE strategy_pending_orders SET 
                strategy_id = ?, pending_order_info = ?, status = ?,
                create_time = ?, update_time = ? 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order.strategy_id);
        query.bind(2, order.pending_order_info);
        query.bind(3, static_cast<int>(order.status));
        query.bind(4, static_cast<int64_t>(order.create_time));
        query.bind(5, static_cast<int64_t>(current_time)); // 总是更新 update_time 为当前时间
        query.bind(6, order.id);

        int updated_count = query.exec();

        if (updated_count > 0)
        {
            LOG_INFO("Strategy pending order updated successfully: id={}", order.id);
            return true;
        }
        else
        {
            set_error("No strategy pending order found with id: " + std::to_string(order.id));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        set_error("Failed to update strategy pending order: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::update_strategy_pending_order_status(int64_t id, PendingOrderStatus status)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            UPDATE strategy_pending_orders SET 
                status = ?, update_time = ? 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, static_cast<int>(status));
        query.bind(2, static_cast<int64_t>(current_time));
        query.bind(3, id);

        int updated_count = query.exec();

        if (updated_count > 0)
        {
            LOG_INFO("Strategy pending order status updated successfully: id={}, status={}", id, static_cast<int>(status));
            return true;
        }
        else
        {
            set_error("No strategy pending order found with id: " + std::to_string(id));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        set_error("Failed to update strategy pending order status: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::delete_strategy_pending_order(int64_t id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        const std::string sql = "DELETE FROM strategy_pending_orders WHERE id = ?";

        SQLite::Statement query(*_db, sql);
        query.bind(1, id);

        int deleted_count = query.exec();

        if (deleted_count > 0)
        {
            return true;
        }
        else
        {
            set_error("No strategy pending order found with id: " + std::to_string(id));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        set_error("Failed to delete strategy pending order: " + std::string(e.what()));
        return false;
    }
}

std::optional<StrategyPendingOrder> DatabaseManager::query_strategy_pending_order_by_id(int64_t id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return std::nullopt;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, strategy_id, pending_order_info, status, create_time, update_time 
            FROM strategy_pending_orders 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, id);

        if (query.executeStep())
        {
            return row_to_strategy_pending_order(query);
        }

        return std::nullopt;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy pending order by ID: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::vector<StrategyPendingOrder> DatabaseManager::query_strategy_pending_orders_by_strategy_id(const std::string &strategy_id)
{
    std::vector<StrategyPendingOrder> orders;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return orders;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, strategy_id, pending_order_info, status, create_time, update_time 
            FROM strategy_pending_orders 
            WHERE strategy_id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, strategy_id);

        while (query.executeStep())
        {
            orders.push_back(row_to_strategy_pending_order(query));
        }

        return orders;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy pending orders by strategy ID: " + std::string(e.what()));
        return orders;
    }
}

StrategyPendingOrderBatchResult DatabaseManager::batch_query_strategy_pending_orders(const StrategyPendingOrderQuery &query_params)
{
    StrategyPendingOrderBatchResult result;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return result;
    }

    try
    {
        // 构建WHERE条件
        std::vector<std::string> conditions;
        std::vector<std::pair<int, std::string>> string_binds;
        std::vector<std::pair<int, int64_t>> int_binds;
        int bind_index = 1;

        if (!query_params.strategy_id.empty())
        {
            conditions.push_back("strategy_id = ?");
            string_binds.push_back({bind_index++, query_params.strategy_id});
        }

        if (query_params.status != PendingOrderStatus::ACTIVE || query_params.filter_by_status)
        {
            conditions.push_back("status = ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.status)});
        }

        if (query_params.start_time > 0)
        {
            conditions.push_back("create_time >= ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.start_time)});
        }

        if (query_params.end_time > 0)
        {
            conditions.push_back("create_time <= ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.end_time)});
        }

        std::string where_clause = conditions.empty() ? "" : "WHERE " + std::accumulate(conditions.begin() + 1, conditions.end(), conditions[0], [](const std::string &a, const std::string &b)
                                                                                        { return a + " AND " + b; });

        // 1. 获取总记录数
        std::string count_sql = "SELECT COUNT(*) FROM strategy_pending_orders " + where_clause;
        SQLite::Statement count_query(*_db, count_sql);

        // 绑定参数
        for (const auto &bind : string_binds)
        {
            count_query.bind(bind.first, bind.second);
        }
        for (const auto &bind : int_binds)
        {
            count_query.bind(bind.first, bind.second);
        }

        if (count_query.executeStep())
        {
            result.total_count = count_query.getColumn(0).getInt();
        }

        // 2. 查询数据
        std::string data_sql = R"(
            SELECT id, strategy_id, pending_order_info, status, create_time, update_time 
            FROM strategy_pending_orders 
        )" + where_clause;

        // 添加排序
        std::string valid_order_fields[] = {"id", "create_time", "update_time", "strategy_id", "status"};
        std::string order_field = query_params.order_by;
        if (std::find(std::begin(valid_order_fields), std::end(valid_order_fields), order_field) == std::end(valid_order_fields))
        {
            order_field = "create_time"; // 默认按创建时间排序
        }

        data_sql += " ORDER BY " + order_field + (query_params.desc ? " DESC" : " ASC");
        data_sql += " LIMIT ? OFFSET ?";

        SQLite::Statement data_query(*_db, data_sql);

        // 重新绑定参数
        bind_index = 1;
        for (const auto &bind : string_binds)
        {
            data_query.bind(bind_index++, bind.second);
        }
        for (const auto &bind : int_binds)
        {
            data_query.bind(bind_index++, bind.second);
        }
        data_query.bind(bind_index++, query_params.limit);
        data_query.bind(bind_index++, query_params.offset);

        while (data_query.executeStep())
        {
            result.records.push_back(row_to_strategy_pending_order(data_query));
        }

        result.returned_count = static_cast<int>(result.records.size());
        result.has_more = (query_params.offset + result.returned_count < result.total_count);

        LOG_INFO("Batch query pending orders completed: total={}, returned={}, has_more={}",
                 result.total_count, result.returned_count, result.has_more);

        return result;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to batch query strategy pending orders: " + std::string(e.what()));
        return result;
    }
}

// ===== 策略订单表操作 =====

int64_t DatabaseManager::insert_strategy_order_basic(int64_t pending_order_id,
                                                     const std::string &security_code,
                                                     OrderSide side,
                                                     OrderSign sign,
                                                     OrderType order_type,
                                                     double price,
                                                     uint64_t qty,
                                                     int64_t order_id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return -1;
    }

    try
    {
        // 首先获取策略挂单信息用于冗余字段
        const std::string pending_sql = "SELECT strategy_id, pending_order_info, create_time FROM strategy_pending_orders WHERE id = ?";
        SQLite::Statement pending_query(*_db, pending_sql);
        pending_query.bind(1, pending_order_id);

        std::string strategy_id;
        std::string pending_order_info;
        uint64_t pending_submit_time = 0;

        if (pending_query.executeStep())
        {
            strategy_id = pending_query.getColumn(0).getText();
            pending_order_info = pending_query.getColumn(1).getText();
            pending_submit_time = pending_query.getColumn(2).getInt64();
        }
        else
        {
            set_error("Pending order not found with id: " + std::to_string(pending_order_id));
            return -1;
        }

        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            INSERT INTO strategy_orders 
            (pending_order_id, strategy_id, pending_order_info, pending_submit_time, 
             security_code, order_id, side, sign, order_type, price, qty, create_time, update_time) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, pending_order_id);
        query.bind(2, strategy_id);
        query.bind(3, pending_order_info);
        query.bind(4, static_cast<int64_t>(pending_submit_time));
        query.bind(5, security_code);
        query.bind(6, order_id);
        query.bind(7, static_cast<int>(side));
        query.bind(8, static_cast<int>(sign));
        query.bind(9, static_cast<int>(order_type));
        query.bind(10, price);
        query.bind(11, static_cast<int64_t>(qty));
        query.bind(12, static_cast<int64_t>(current_time)); // create_time
        query.bind(13, static_cast<int64_t>(current_time)); // update_time

        query.exec();
        int64_t insert_id = _db->getLastInsertRowid();

        LOG_INFO("Strategy order inserted successfully, ID: {}, order_id: {}", insert_id, order_id);
        return insert_id;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to insert strategy order: " + std::string(e.what()));
        return -1;
    }
}

bool DatabaseManager::insert_strategy_order(const StrategyOrder &order)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            INSERT INTO strategy_orders 
            (pending_order_id, strategy_id, pending_order_info, pending_submit_time, 
             login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
             batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
             avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order.pending_order_id);
        query.bind(2, order.strategy_id);
        query.bind(3, order.pending_order_info);
        query.bind(4, static_cast<int64_t>(order.pending_submit_time));
        query.bind(5, order.login_id);
        query.bind(6, order.cust_id);
        query.bind(7, order.fund_account_id);
        query.bind(8, order.account_id);
        query.bind(9, order.security_code);
        query.bind(10, order.order_id);
        query.bind(11, order.exchange_order_id);
        query.bind(12, static_cast<int64_t>(order.batch_id));
        query.bind(13, static_cast<int>(order.side));
        query.bind(14, static_cast<int>(order.sign));
        query.bind(15, static_cast<int>(order.order_type));
        query.bind(16, static_cast<int>(order.status));
        query.bind(17, order.status_msg);
        query.bind(18, order.price);
        query.bind(19, static_cast<int64_t>(order.qty));
        query.bind(20, static_cast<int64_t>(order.leaves_qty));
        query.bind(21, static_cast<int64_t>(order.cum_qty));
        query.bind(22, order.avg_price);
        query.bind(23, order.cum_amt);
        query.bind(24, static_cast<int64_t>(order.order_time));
        query.bind(25, static_cast<int64_t>(order.update_time > 0 ? order.update_time : current_time));
        query.bind(26, static_cast<int64_t>(order.create_time > 0 ? order.create_time : current_time));
        query.bind(27, order.error_code);
        query.bind(28, order.error_msg);

        query.exec();
        LOG_INFO("Strategy order inserted successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to insert strategy order: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::update_strategy_order(const StrategyOrder &order)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            UPDATE strategy_orders SET 
                pending_order_id = ?, strategy_id = ?, pending_order_info = ?, pending_submit_time = ?, 
                login_id = ?, cust_id = ?, fund_account_id = ?, account_id = ?, security_code = ?, order_id = ?, 
                exchange_order_id = ?, batch_id = ?, side = ?, order_type = ?, status = ?, status_msg = ?, 
                price = ?, qty = ?, leaves_qty = ?, cum_qty = ?, avg_price = ?, cum_amt = ?, 
                order_time = ?, update_time = ?, create_time = ?, error_code = ?, error_msg = ? 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order.pending_order_id);
        query.bind(2, order.strategy_id);
        query.bind(3, order.pending_order_info);
        query.bind(4, static_cast<int64_t>(order.pending_submit_time));
        query.bind(5, order.login_id);
        query.bind(6, order.cust_id);
        query.bind(7, order.fund_account_id);
        query.bind(8, order.account_id);
        query.bind(9, order.security_code);
        query.bind(10, order.order_id);
        query.bind(11, order.exchange_order_id);
        query.bind(12, static_cast<int64_t>(order.batch_id));
        query.bind(13, static_cast<int>(order.side));
        query.bind(14, static_cast<int>(order.order_type));
        query.bind(15, static_cast<int>(order.status));
        query.bind(16, order.status_msg);
        query.bind(17, order.price);
        query.bind(18, static_cast<int64_t>(order.qty));
        query.bind(19, static_cast<int64_t>(order.leaves_qty));
        query.bind(20, static_cast<int64_t>(order.cum_qty));
        query.bind(21, order.avg_price);
        query.bind(22, order.cum_amt);
        query.bind(23, static_cast<int64_t>(order.order_time));
        query.bind(24, static_cast<int64_t>(current_time)); // 总是更新 update_time 为当前时间
        query.bind(25, static_cast<int64_t>(order.create_time));
        query.bind(26, order.error_code);
        query.bind(27, order.error_msg);
        query.bind(28, order.id);

        int updated_count = query.exec();

        if (updated_count > 0)
        {
            LOG_INFO("Strategy order updated successfully: id={}", order.id);
            return true;
        }
        else
        {
            set_error("No strategy order found with id: " + std::to_string(order.id));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        set_error("Failed to update strategy order: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::update_strategy_order_from_trade_data(int64_t id, const TradeOrderData &trade_data)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            UPDATE strategy_orders SET 
                login_id = ?, cust_id = ?, fund_account_id = ?, account_id = ?, 
                order_id = ?, exchange_order_id = ?, batch_id = ?, 
                status = ?, status_msg = ?, leaves_qty = ?, cum_qty = ?, 
                avg_price = ?, cum_amt = ?, order_time = ?, update_time = ?, 
                error_code = ?, error_msg = ? 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, trade_data.account.login_id);
        query.bind(2, trade_data.account.cust_id);
        query.bind(3, trade_data.account.fund_account_id);
        query.bind(4, std::string(trade_data.current_account_id));
        query.bind(5, trade_data.order_id);
        query.bind(6, trade_data.exchange_order_id);
        query.bind(7, static_cast<int64_t>(trade_data.batch_id));
        query.bind(8, static_cast<int>(trade_data.status));
        query.bind(9, trade_data.status_msg);
        query.bind(10, static_cast<int64_t>(trade_data.leaves_qty));
        query.bind(11, static_cast<int64_t>(trade_data.cum_qty));
        query.bind(12, trade_data.avg_price);
        query.bind(13, trade_data.cum_amt);
        query.bind(14, static_cast<int64_t>(trade_data.order_time));
        query.bind(15, static_cast<int64_t>(current_time)); // 更新 update_time 为当前时间
        query.bind(16, trade_data.error_code);
        query.bind(17, trade_data.error_msg);
        query.bind(18, id);

        query.exec();
        LOG_INFO("Strategy order updated from trade data successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to update strategy order from trade data: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::update_strategy_order_id(int64_t id, int64_t order_id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        uint64_t current_time = get_current_timestamp();

        const std::string sql = R"(
            UPDATE strategy_orders 
            SET order_id = ?, update_time = ? 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order_id);
        query.bind(2, static_cast<int64_t>(current_time)); // 更新 update_time
        query.bind(3, id);

        query.exec();
        LOG_INFO("Strategy order ID updated successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to update strategy order ID: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::delete_strategy_order(int64_t id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        const std::string sql = "DELETE FROM strategy_orders WHERE id = ?";

        SQLite::Statement query(*_db, sql);
        query.bind(1, id);

        int deleted_count = query.exec();

        if (deleted_count > 0)
        {
            LOG_INFO("Strategy order deleted successfully: id={}", id);
            return true;
        }
        else
        {
            set_error("No strategy order found with id: " + std::to_string(id));
            return false;
        }
    }
    catch (const std::exception &e)
    {
        set_error("Failed to delete strategy order: " + std::string(e.what()));
        return false;
    }
}

std::optional<StrategyOrder> DatabaseManager::query_strategy_order_by_id(int64_t id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return std::nullopt;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, pending_order_id, strategy_id, pending_order_info, pending_submit_time,
                   login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
                   batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
                   avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg 
            FROM strategy_orders 
            WHERE id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, id);

        if (query.executeStep())
        {
            return row_to_strategy_order(query);
        }

        return std::nullopt;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy order by ID: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<StrategyOrder> DatabaseManager::query_strategy_order_by_order_id(int64_t order_id)
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return std::nullopt;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, pending_order_id, strategy_id, pending_order_info, pending_submit_time,
                   login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
                   batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
                   avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg 
            FROM strategy_orders 
            WHERE order_id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, order_id);

        if (query.executeStep())
        {
            return row_to_strategy_order(query);
        }

        return std::nullopt;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy order by order ID: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::vector<StrategyOrder> DatabaseManager::query_strategy_orders_by_pending_order_id(int64_t pending_order_id)
{
    std::vector<StrategyOrder> orders;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return orders;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, pending_order_id, strategy_id, pending_order_info, pending_submit_time,
                   login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
                   batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
                   avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg 
            FROM strategy_orders 
            WHERE pending_order_id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, pending_order_id);

        while (query.executeStep())
        {
            orders.push_back(row_to_strategy_order(query));
        }

        return orders;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy orders by pending order ID: " + std::string(e.what()));
        return orders;
    }
}

std::vector<StrategyOrder> DatabaseManager::query_strategy_orders_by_strategy_id(const std::string &strategy_id)
{
    std::vector<StrategyOrder> orders;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return orders;
    }

    try
    {
        const std::string sql = R"(
            SELECT id, pending_order_id, strategy_id, pending_order_info, pending_submit_time,
                   login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
                   batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
                   avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg 
            FROM strategy_orders 
            WHERE strategy_id = ?;
        )";

        SQLite::Statement query(*_db, sql);
        query.bind(1, strategy_id);

        while (query.executeStep())
        {
            orders.push_back(row_to_strategy_order(query));
        }

        return orders;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy orders by strategy ID: " + std::string(e.what()));
        return orders;
    }
}

StrategyOrderBatchResult DatabaseManager::batch_query_strategy_orders(const StrategyOrderQuery &query_params)
{
    StrategyOrderBatchResult result;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return result;
    }

    try
    {
        // 构建WHERE条件
        std::vector<std::string> conditions;
        std::vector<std::pair<int, std::string>> string_binds;
        std::vector<std::pair<int, int64_t>> int_binds;
        int bind_index = 1;

        if (!query_params.strategy_id.empty())
        {
            conditions.push_back("strategy_id = ?");
            string_binds.push_back({bind_index++, query_params.strategy_id});
        }

        if (query_params.pending_order_id > 0)
        {
            conditions.push_back("pending_order_id = ?");
            int_binds.push_back({bind_index++, query_params.pending_order_id});
        }

        if (!query_params.security_code.empty())
        {
            conditions.push_back("security_code = ?");
            string_binds.push_back({bind_index++, query_params.security_code});
        }

        if (query_params.status != ORDER_STATUS_UNKNOWN)
        {
            conditions.push_back("status = ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.status)});
        }

        if (query_params.start_time > 0)
        {
            conditions.push_back("create_time >= ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.start_time)});
        }

        if (query_params.end_time > 0)
        {
            conditions.push_back("create_time <= ?");
            int_binds.push_back({bind_index++, static_cast<int64_t>(query_params.end_time)});
        }

        // 市场筛选（通过股票代码前缀判断）
        if (query_params.market == 1)
        {
            conditions.push_back("substr(security_code, 1, 1) = '6'");
        }
        else if (query_params.market == 2)
        {
            conditions.push_back("(substr(security_code, 1, 3) IN ('000', '002', '300'))");
        }

        std::string where_clause = conditions.empty() ? "" : "WHERE " + std::accumulate(conditions.begin() + 1, conditions.end(), conditions[0], [](const std::string &a, const std::string &b)
                                                                                        { return a + " AND " + b; });

        // 1. 获取总记录数
        std::string count_sql = "SELECT COUNT(*) FROM strategy_orders " + where_clause;
        SQLite::Statement count_query(*_db, count_sql);

        // 绑定参数
        for (const auto &bind : string_binds)
        {
            count_query.bind(bind.first, bind.second);
        }
        for (const auto &bind : int_binds)
        {
            count_query.bind(bind.first, bind.second);
        }

        if (count_query.executeStep())
        {
            result.total_count = count_query.getColumn(0).getInt();
        }

        // 2. 查询数据
        std::string data_sql = R"(
            SELECT id, pending_order_id, strategy_id, pending_order_info, pending_submit_time,
                   login_id, cust_id, fund_account_id, account_id, security_code, order_id, exchange_order_id, 
                   batch_id, side, sign, order_type, status, status_msg, price, qty, leaves_qty, cum_qty, 
                   avg_price, cum_amt, order_time, update_time, create_time, error_code, error_msg 
            FROM strategy_orders 
        )" + where_clause;

        // 添加排序
        std::string valid_order_fields[] = {"id", "create_time", "update_time", "security_code", "status"};
        std::string order_field = query_params.order_by;
        if (std::find(std::begin(valid_order_fields), std::end(valid_order_fields), order_field) == std::end(valid_order_fields))
        {
            order_field = "create_time"; // 默认按创建时间排序
        }

        data_sql += " ORDER BY " + order_field + (query_params.desc ? " DESC" : " ASC");
        data_sql += " LIMIT ? OFFSET ?";

        SQLite::Statement data_query(*_db, data_sql);

        // 重新绑定参数
        bind_index = 1;
        for (const auto &bind : string_binds)
        {
            data_query.bind(bind_index++, bind.second);
        }
        for (const auto &bind : int_binds)
        {
            data_query.bind(bind_index++, bind.second);
        }
        data_query.bind(bind_index++, query_params.limit);
        data_query.bind(bind_index++, query_params.offset);

        while (data_query.executeStep())
        {
            result.records.push_back(row_to_strategy_order(data_query));
        }

        result.returned_count = static_cast<int>(result.records.size());
        result.has_more = (query_params.offset + result.returned_count < result.total_count);

        LOG_INFO("Batch query strategy orders completed: total={}, returned={}, has_more={}",
                 result.total_count, result.returned_count, result.has_more);

        return result;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to batch query strategy orders: " + std::string(e.what()));
        return result;
    }
}

// ===== 联表查询 =====

std::vector<StrategyPendingOrderWithOrders> DatabaseManager::query_strategy_pending_orders_with_orders(const std::string &strategy_id)
{
    std::vector<StrategyPendingOrderWithOrders> results;

    if (!_initialized)
    {
        set_error("Database not initialized");
        return results;
    }

    try
    {
        // 首先查询策略挂单
        std::vector<StrategyPendingOrder> pending_orders;
        if (strategy_id.empty())
        {
            // 查询所有策略挂单
            const std::string sql = "SELECT id, strategy_id, pending_order_info, status, create_time, update_time FROM strategy_pending_orders ORDER BY create_time DESC";
            SQLite::Statement query(*_db, sql);

            while (query.executeStep())
            {
                pending_orders.push_back(row_to_strategy_pending_order(query));
            }
        }
        else
        {
            // 查询指定策略的挂单
            pending_orders = query_strategy_pending_orders_by_strategy_id(strategy_id);
        }

        // 为每个策略挂单查询关联的订单
        for (const auto &pending_order : pending_orders)
        {
            StrategyPendingOrderWithOrders result;
            result.pending_order = pending_order;
            result.orders = query_strategy_orders_by_pending_order_id(pending_order.id);
            results.push_back(std::move(result));
        }

        LOG_INFO("Query strategy pending orders with orders completed: {} pending orders found", results.size());
        return results;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to query strategy pending orders with orders: " + std::string(e.what()));
        return results;
    }
}

// ===== 事务操作 =====

bool DatabaseManager::begin_transaction()
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        _db->exec("BEGIN TRANSACTION");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to begin transaction: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::commit_transaction()
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        _db->exec("COMMIT");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to commit transaction: " + std::string(e.what()));
        return false;
    }
}

bool DatabaseManager::rollback_transaction()
{
    if (!_initialized)
    {
        set_error("Database not initialized");
        return false;
    }

    try
    {
        _db->exec("ROLLBACK");
        return true;
    }
    catch (const std::exception &e)
    {
        set_error("Failed to rollback transaction: " + std::string(e.what()));
        return false;
    }
}

// ===== 工具函数 =====

std::string DatabaseManager::get_last_error() const
{
    return _last_error;
}

void DatabaseManager::set_error(const std::string &error) const
{
    _last_error = error;
    LOG_ERROR("DatabaseManager Error: {}", error);
}

StrategyPendingOrder DatabaseManager::row_to_strategy_pending_order(SQLite::Statement &query)
{
    StrategyPendingOrder order;
    order.id = query.getColumn(0).getInt64();
    order.strategy_id = query.getColumn(1).getText();
    order.pending_order_info = query.getColumn(2).getText();
    order.status = static_cast<PendingOrderStatus>(query.getColumn(3).getInt());
    order.create_time = query.getColumn(4).getInt64();
    order.update_time = query.getColumn(5).getInt64();
    return order;
}

StrategyOrder DatabaseManager::row_to_strategy_order(SQLite::Statement &query)
{
    StrategyOrder order;
    order.id = query.getColumn(0).getInt64();
    order.pending_order_id = query.getColumn(1).getInt64();
    order.strategy_id = query.getColumn(2).getText();
    order.pending_order_info = query.getColumn(3).getText();
    order.pending_submit_time = query.getColumn(4).getInt64();
    order.login_id = query.getColumn(5).getText();
    order.cust_id = query.getColumn(6).getText();
    order.fund_account_id = query.getColumn(7).getText();
    order.account_id = query.getColumn(8).getText();
    order.security_code = query.getColumn(9).getText();
    order.order_id = query.getColumn(10).getInt64();
    order.exchange_order_id = query.getColumn(11).getText();
    order.batch_id = query.getColumn(12).getInt64();
    order.side = static_cast<OrderSide>(query.getColumn(13).getInt());
    order.sign = static_cast<OrderSign>(query.getColumn(14).getInt());
    order.order_type = static_cast<OrderType>(query.getColumn(15).getInt());
    order.status = static_cast<OrderStatus>(query.getColumn(16).getInt());
    order.status_msg = query.getColumn(17).getText();
    order.price = query.getColumn(18).getDouble();
    order.qty = query.getColumn(19).getInt64();
    order.leaves_qty = query.getColumn(20).getInt64();
    order.cum_qty = query.getColumn(21).getInt64();
    order.avg_price = query.getColumn(22).getDouble();
    order.cum_amt = query.getColumn(23).getDouble();
    order.order_time = query.getColumn(24).getInt64();
    order.update_time = query.getColumn(25).getInt64();
    order.create_time = query.getColumn(26).getInt64();
    order.error_code = query.getColumn(27).getInt();
    order.error_msg = query.getColumn(28).getText();
    return order;
}

uint64_t DatabaseManager::get_current_timestamp()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ===== 批量操作实现 =====

BatchOperationResult DatabaseManager::batch_insert_strategy_pending_orders(
    const std::vector<StrategyPendingOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders, 
        [this](const StrategyPendingOrder& order) -> bool {
            return insert_strategy_pending_order(order);
        }, options, "batch_insert_strategy_pending_orders");
}

BatchOperationResult DatabaseManager::batch_update_strategy_pending_orders(
    const std::vector<StrategyPendingOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders,
        [this](const StrategyPendingOrder& order) -> bool {
            if (order.id <= 0) {
                set_error("Invalid order ID for update: " + std::to_string(order.id));
                return false;
            }
            return update_strategy_pending_order(order);
        }, options, "batch_update_strategy_pending_orders");
}

BatchOperationResult DatabaseManager::batch_insert_strategy_orders(
    const std::vector<StrategyOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders,
        [this](const StrategyOrder& order) -> bool {
            return insert_strategy_order(order);
        }, options, "batch_insert_strategy_orders");
}

BatchOperationResult DatabaseManager::batch_update_strategy_orders(
    const std::vector<StrategyOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders,
        [this](const StrategyOrder& order) -> bool {
            if (order.id <= 0) {
                set_error("Invalid order ID for update: " + std::to_string(order.id));
                return false;
            }
            return update_strategy_order(order);
        }, options, "batch_update_strategy_orders");
}

BatchOperationResult DatabaseManager::batch_update_strategy_pending_order_status(
    const std::vector<int64_t>& ids,
    PendingOrderStatus status,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(ids.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (ids.empty()) {
        return result;
    }

    return execute_batch_operation(ids,
        [this, status](int64_t id) -> bool {
            return update_strategy_pending_order_status(id, status);
        }, options, "batch_update_strategy_pending_order_status");
}

BatchOperationResult DatabaseManager::batch_update_strategy_orders_from_trade_data(
    const std::vector<std::pair<int64_t, TradeOrderData>>& updates,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(updates.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (updates.empty()) {
        return result;
    }

    return execute_batch_operation(updates,
        [this](const std::pair<int64_t, TradeOrderData>& update) -> bool {
            return update_strategy_order_from_trade_data(update.first, update.second);
        }, options, "batch_update_strategy_orders_from_trade_data");
}

BatchOperationResult DatabaseManager::batch_delete_strategy_pending_orders(
    const std::vector<int64_t>& ids,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(ids.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (ids.empty()) {
        return result;
    }

    return execute_batch_operation(ids,
        [this](int64_t id) -> bool {
            return delete_strategy_pending_order(id);
        }, options, "batch_delete_strategy_pending_orders");
}

BatchOperationResult DatabaseManager::batch_delete_strategy_orders(
    const std::vector<int64_t>& ids,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(ids.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (ids.empty()) {
        return result;
    }

    return execute_batch_operation(ids,
        [this](int64_t id) -> bool {
            return delete_strategy_order(id);
        }, options, "batch_delete_strategy_orders");
}

BatchOperationResult DatabaseManager::batch_upsert_strategy_pending_orders(
    const std::vector<StrategyPendingOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders,
        [this](const StrategyPendingOrder& order) -> bool {
            if (order.id > 0) {
                // 更新现有记录
                return update_strategy_pending_order(order);
            } else {
                // 插入新记录
                return insert_strategy_pending_order(order);
            }
        }, options, "batch_upsert_strategy_pending_orders");
}

BatchOperationResult DatabaseManager::batch_upsert_strategy_orders(
    const std::vector<StrategyOrder>& orders,
    const BatchUpdateOptions& options)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(orders.size());

    if (!_initialized) {
        result.errors.push_back("Database not initialized");
        result.transaction_success = false;
        return result;
    }

    if (orders.empty()) {
        return result;
    }

    return execute_batch_operation(orders,
        [this](const StrategyOrder& order) -> bool {
            if (order.id > 0) {
                // 更新现有记录
                return update_strategy_order(order);
            } else {
                // 插入新记录
                return insert_strategy_order(order);
            }
        }, options, "batch_upsert_strategy_orders");
}

// ===== 批量操作辅助方法实现 =====

template<typename T, typename OperationFunc>
BatchOperationResult DatabaseManager::execute_batch_operation(
    const std::vector<T>& items,
    OperationFunc operation,
    const BatchUpdateOptions& options,
    const std::string& operation_name)
{
    BatchOperationResult result;
    result.total_count = static_cast<int>(items.size());

    try {
        bool transaction_started = false;
        
        // 开始事务（如果启用）
        if (options.use_transaction) {
            if (begin_transaction()) {
                transaction_started = true;
                LOG_INFO("Started transaction for {}", operation_name);
            } else {
                result.errors.push_back("Failed to start transaction");
                result.transaction_success = false;
                return result;
            }
        }

        // 如果数据量大，分批处理
        if (items.size() > static_cast<size_t>(options.batch_size)) {
            result = process_in_batches(
                static_cast<int>(items.size()),
                options.batch_size,
                [&](int start_idx, int end_idx) -> BatchOperationResult {
                    BatchOperationResult batch_result;
                    
                    for (int i = start_idx; i < end_idx; ++i) {
                        try {
                            bool success = operation(items[i]);
                            if (success) {
                                batch_result.success_count++;
                            } else {
                                batch_result.failed_count++;
                                batch_result.errors.push_back(
                                    "Operation failed at index " + std::to_string(i) + ": " + get_last_error());
                                
                                if (options.stop_on_first_error) {
                                    break;
                                }
                            }
                        } catch (const std::exception& e) {
                            batch_result.failed_count++;
                            batch_result.errors.push_back(
                                "Exception at index " + std::to_string(i) + ": " + e.what());
                            
                            if (options.stop_on_first_error) {
                                break;
                            }
                        }
                    }
                    
                    batch_result.total_count = end_idx - start_idx;
                    return batch_result;
                });
        } else {
            // 小数据量直接处理
            for (size_t i = 0; i < items.size(); ++i) {
                try {
                    bool success = operation(items[i]);
                    if (success) {
                        result.success_count++;
                    } else {
                        result.failed_count++;
                        result.errors.push_back(
                            "Operation failed at index " + std::to_string(i) + ": " + get_last_error());
                        
                        if (options.stop_on_first_error) {
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    result.failed_count++;
                    result.errors.push_back(
                        "Exception at index " + std::to_string(i) + ": " + e.what());
                    
                    if (options.stop_on_first_error) {
                        break;
                    }
                }
            }
        }

        // 提交或回滚事务
        if (transaction_started) {
            if (result.failed_count == 0 || !options.stop_on_first_error) {
                if (commit_transaction()) {
                    result.transaction_success = true;
                    LOG_INFO("Transaction committed successfully for {}", operation_name);
                } else {
                    result.transaction_success = false;
                    result.errors.push_back("Failed to commit transaction");
                    LOG_ERROR("Failed to commit transaction for {}", operation_name);
                }
            } else {
                if (rollback_transaction()) {
                    result.transaction_success = false;
                    LOG_INFO("Transaction rolled back for {}", operation_name);
                } else {
                    result.errors.push_back("Failed to rollback transaction");
                    LOG_ERROR("Failed to rollback transaction for {}", operation_name);
                }
            }
        } else {
            result.transaction_success = true; // 没有使用事务，认为成功
        }

        LOG_INFO("{} completed: total={}, success={}, failed={}, success_rate={:.2f}%",
                 operation_name, result.total_count, result.success_count, 
                 result.failed_count, result.get_success_rate() * 100.0);

    } catch (const std::exception& e) {
        result.errors.push_back("Batch operation exception: " + std::string(e.what()));
        result.transaction_success = false;
        
        if (options.use_transaction) {
            try {
                rollback_transaction();
            } catch (...) {
                // 忽略回滚异常
            }
        }
        
        LOG_ERROR("Exception in {}: {}", operation_name, e.what());
    }

    return result;
}

template<typename ProcessorFunc>
BatchOperationResult DatabaseManager::process_in_batches(
    int total_size,
    int batch_size,
    ProcessorFunc processor)
{
    BatchOperationResult total_result;
    total_result.total_count = total_size;

    for (int start = 0; start < total_size; start += batch_size) {
        int end = std::min(start + batch_size, total_size);
        
        try {
            BatchOperationResult batch_result = processor(start, end);
            
            total_result.success_count += batch_result.success_count;
            total_result.failed_count += batch_result.failed_count;
            
            // 合并错误信息
            total_result.errors.insert(
                total_result.errors.end(),
                batch_result.errors.begin(),
                batch_result.errors.end());
                
            if (!batch_result.transaction_success) {
                total_result.transaction_success = false;
            }
            
            LOG_DEBUG("Processed batch {}-{}: success={}, failed={}",
                     start, end-1, batch_result.success_count, batch_result.failed_count);
                     
        } catch (const std::exception& e) {
            total_result.failed_count += (end - start);
            total_result.errors.push_back(
                "Batch " + std::to_string(start) + "-" + std::to_string(end-1) + 
                " failed: " + e.what());
            total_result.transaction_success = false;
        }
    }

    return total_result;
}

NS_FT_END