#include "migration/migration_manager.h"
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <regex>
#include <ctime>
#include <iostream>
#include <set>
#include <cstdio>
#include "laserpants/dotenv/dotenv.h"
#include "utils/INI.h"
#include "utils/file.hpp"
#include "logger/logger.hpp"
#include <ctime>

NS_FT_BEGIN

MigrationManager::MigrationManager()
    : _auto_backup_enabled(true) {

    dotenv::init();
    _db_path = dotenv::getenv("FT_DB_PATH", "data/trading.db");
    _config_path = dotenv::getenv("FT_DB_MIGRATION_CONFIG_PATH", "migrations/migration.config.ini");
}

MigrationManager::~MigrationManager() {
    // SQLiteCpp Database destructor handles cleanup automatically
}

bool MigrationManager::initialize() {
    try {
        // 加载配置
        if (!load_config()) {
            LOG_ERROR("Failed to load migration config");
            return false;
        }
        
        // 创建必要的目录
        create_directories(_migration_scripts_dir);
        create_directories(_backup_dir);
        
        // 打开数据库连接
        try {
            _db = std::make_unique<SQLite::Database>(_db_path,
                                                     SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            
            // 开启外键约束
            _db->exec("PRAGMA foreign_keys = ON");
            
            // 设置WAL模式以提高并发性能
            _db->exec("PRAGMA journal_mode = WAL");
            
            // 设置同步模式
            _db->exec("PRAGMA synchronous = NORMAL");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to open database: {}", e.what());
            return false;
        }
        
        // 创建迁移元数据表
        if (!create_migration_table()) {
            LOG_ERROR("Failed to create migration table");
            return false;
        }
        
        LOG_INFO("Migration manager initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Migration manager initialization failed: {}", e.what());
        return false;
    }
}

bool MigrationManager::create_migration_table() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version TEXT PRIMARY KEY
        );
    )";
    
    try {
        _db->exec(sql);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create migration table: {}", e.what());
        return false;
    }
}

bool MigrationManager::load_config() {
    // 检查配置文件是否存在
    if (!file_exists(_config_path)) {
        LOG_WARN("Config file not found, using defaults: {}", _config_path.c_str());
        return true; // 使用默认配置
    }
    
    try {
        // 使用INI解析器加载配置文件
        INI ini(_config_path, true);
        
        // 从INI文件读取配置值
        _migration_scripts_dir = ini.get("migration", "MIGRATION_SCRIPTS_DIR", "migrations/scripts");
        _backup_dir = ini.get("database", "BACKUP_DIR", "data/backup");
        _auto_backup_enabled = (ini.get("backup", "AUTO_BACKUP_ENABLED", "true") == "true");
        
        LOG_INFO("Loaded migration config from: {}", _config_path);
        LOG_INFO("Migration scripts dir: {}", _migration_scripts_dir);
        LOG_INFO("Backup dir: {}", _backup_dir);
        LOG_INFO("Auto backup enabled: {}", _auto_backup_enabled ? "true" : "false");
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse INI config file {}: {}", _config_path, e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Failed to parse INI config file {}: Unknown error", _config_path);
        return false;
    }
}

std::string MigrationManager::get_current_version() {
    // 只查询当前版本号（应该只有一条记录）
    const char* sql = "SELECT version FROM schema_migrations LIMIT 1";
    
    try {
        SQLite::Statement query(*_db, sql);
        
        if (query.executeStep()) {
            return query.getColumn(0).getText();
        }
        
        return "";
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get current version: {}", e.what());
        return "";
    }
}

std::vector<MigrationInfo> MigrationManager::get_pending_migrations() {
    std::string current_version = get_current_version();
    auto all_migrations = scan_migration_scripts();
    std::vector<MigrationInfo> pending;
    
    // 如果没有当前版本，所有迁移都是待应用的
    if (current_version.empty()) {
        for (auto& migration : all_migrations) {
            migration.applied = false;
            pending.push_back(migration);
        }
        return pending;
    }
    
    // 找到以当前版本为down_revision的迁移（即下一个要应用的迁移）
    for (auto& migration : all_migrations) {
        if (migration.down_revision == current_version) {
            migration.applied = false;
            pending.push_back(migration);
            
            // 递归找到后续的迁移链
            std::string next_version = migration.version;
            while (!next_version.empty()) {
                bool found_next = false;
                for (auto& next_migration : all_migrations) {
                    if (next_migration.down_revision == next_version) {
                        next_migration.applied = false;
                        pending.push_back(next_migration);
                        next_version = next_migration.version;
                        found_next = true;
                        break;
                    }
                }
                if (!found_next) {
                    break;
                }
            }
            break;
        }
    }
    
    return pending;
}

std::vector<MigrationInfo> MigrationManager::get_applied_migrations() {
    std::string current_version = get_current_version();
    if (current_version.empty()) {
        return {};
    }
    
    // 从文件系统获取所有迁移信息
    auto all_migrations = scan_migration_scripts();
    
    // 通过版本链构建已应用的迁移路径
    std::vector<MigrationInfo> applied_migrations;
    std::string version = current_version;
    
    // 从当前版本开始，向前追溯整个版本链
    while (!version.empty() && version != "null") {
        // 找到对应的迁移信息
        for (auto& migration : all_migrations) {
            if (migration.version == version) {
                migration.applied = true;
                applied_migrations.insert(applied_migrations.begin(), migration);
                version = migration.down_revision;
                break;
            }
        }
        
        // 如果找不到对应的迁移文件，停止追溯
        bool found = false;
        for (const auto& migration : all_migrations) {
            if (migration.version == version) {
                found = true;
                break;
            }
        }
        if (!found && !version.empty() && version != "null") {
            LOG_ERROR("Migration file not found for version: {}", version.c_str());
            break;
        }
    }
    
    return applied_migrations;
}

std::vector<MigrationInfo> MigrationManager::scan_migration_scripts() {
    std::vector<MigrationInfo> migrations;
    
    if (!file_exists(_migration_scripts_dir)) {
        return migrations;
    }
    
    std::vector<std::string> files = list_directory(_migration_scripts_dir);
    for (const std::string& file_path : files) {
        if (is_regular_file(file_path) && get_extension(file_path) == ".sql") {
            std::string filename = get_filename(file_path);
            
            // 解析文件名格式: VERSION_name.sql
            size_t underscore_pos = filename.find('_');
            if (underscore_pos == std::string::npos || underscore_pos != 12) {
                continue; // 跳过格式不正确的文件
            }
            
            MigrationInfo info;
            info.version = filename.substr(0, 12);
            info.name = filename.substr(13, filename.length() - 17); // 去掉.sql
            info.file_path = file_path;
            info.applied = false;
            
            // 从文件中读取版本信息和描述
            std::ifstream file(file_path);
            std::string line;
            while (std::getline(file, line)) {
                if (line.find("-- revision:") == 0) {
                    // 解析 revision: 版本号
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string revision = line.substr(colon_pos + 1);
                        // 去掉空格
                        revision.erase(0, revision.find_first_not_of(" \t"));
                        revision.erase(revision.find_last_not_of(" \t") + 1);
                        info.version = revision;
                    }
                } else if (line.find("-- down_revision:") == 0) {
                    // 解析 down_revision: 上一个版本号
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string down_rev = line.substr(colon_pos + 1);
                        // 去掉空格
                        down_rev.erase(0, down_rev.find_first_not_of(" \t"));
                        down_rev.erase(down_rev.find_last_not_of(" \t") + 1);
                        if (down_rev != "null") {
                            info.down_revision = down_rev;
                        }
                    }
                } else if (line.find("-- Description:") == 0) {
                    info.description = line.substr(15);
                }
            }
            
            migrations.push_back(info);
        }
    }
    
    return migrations;
}

std::string MigrationManager::generate_version() {
    // 生成12位随机版本号，类似Alembic的格式
    const std::string charset = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset.size() - 1);
    
    std::string version;
    for (int i = 0; i < 12; ++i) {
        version += charset[dis(gen)];
    }
    
    return version;
}

std::string MigrationManager::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

MigrationResult MigrationManager::upgrade_to_latest() {
    auto pending = get_pending_migrations();
    
    if (pending.empty()) {
        return {true, "Database is already up to date", get_current_version(), ""};
    }
    
    LOG_INFO("Applying {} pending migrations", pending.size());
    
    for (const auto& migration : pending) {
        auto result = apply_migration(migration, true);
        if (!result.success) {
            return result;
        }
    }
    
    return {true, "Successfully upgraded to latest version", get_current_version(), ""};
}

MigrationResult MigrationManager::apply_migration(const MigrationInfo& migration, bool is_upgrade) {
    LOG_INFO("Applying migration: {} ({})", migration.version.c_str(), migration.name.c_str());
    
    try {
        // 如果启用了自动备份，在执行迁移前创建备份
        std::string backup_path;
        if (_auto_backup_enabled) {
            std::string reason = is_upgrade ? "before_upgrade_" + migration.version : "before_downgrade_" + migration.version;
            backup_path = create_backup(reason);
            if (backup_path.empty()) {
                LOG_WARN("Failed to create backup before migration, continuing anyway");
            } else {
                LOG_INFO("Created backup before migration: {}", backup_path.c_str());
            }
        }
        
        // 读取迁移脚本
        std::ifstream file(migration.file_path);
        if (!file.is_open()) {
            return {false, "Failed to open migration file: " + migration.file_path, migration.version, "Failed to open migration file: " + migration.file_path};
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // 解析对应的SQL段
        std::string section = is_upgrade ? "upgrade" : "downgrade";
        std::string sql = parse_migration_section(content, section);
        
        if (sql.empty()) {
            return {false, "No " + section + " section found in migration", migration.version, "No " + section + " section found in migration"};
        }
        
        // 开始事务
        if (!execute_sql_statements("BEGIN TRANSACTION;")) {
            return {false, "Failed to begin transaction", migration.version, "Failed to begin transaction"};
        }
        
        // 执行迁移SQL
        // Timing variables for potential future use
        (void)std::chrono::high_resolution_clock::now(); // start_time
        bool success = execute_sql_statements(sql);
        (void)std::chrono::high_resolution_clock::now(); // end_time
        
        if (!success) {
            execute_sql_statements("ROLLBACK;");
            return {false, "Failed to execute migration SQL", migration.version, "Failed to execute migration SQL"};
        }
        
        // 更新迁移记录
        if (is_upgrade) {
            try {
                // 先清除所有现有记录，确保只有一个当前版本
                _db->exec("DELETE FROM schema_migrations");
                
                // 插入新的当前版本
                SQLite::Statement insert(*_db, "INSERT INTO schema_migrations (version) VALUES (?)");
                insert.bind(1, migration.version);
                insert.exec();
            } catch (const std::exception& e) {
                execute_sql_statements("ROLLBACK;");
                return {false, "Failed to record migration: " + std::string(e.what()), migration.version, "Failed to record migration: " + std::string(e.what())};
            }
        } else {
            try {
                // 降级时，设置为down_revision版本
                _db->exec("DELETE FROM schema_migrations");
                
                // 如果有down_revision，插入它作为当前版本
                if (!migration.down_revision.empty() && migration.down_revision != "null") {
                    SQLite::Statement insert(*_db, "INSERT INTO schema_migrations (version) VALUES (?)");
                    insert.bind(1, migration.down_revision);
                    insert.exec();
                }
            } catch (const std::exception& e) {
                execute_sql_statements("ROLLBACK;");
                return {false, "Failed to set down revision: " + std::string(e.what()), migration.version, "Failed to set down revision: " + std::string(e.what())};
            }
        }
        
        // 提交事务
        if (!execute_sql_statements("COMMIT;")) {
            return {false, "Failed to commit transaction", migration.version, "Failed to commit transaction"};
        }
        
        LOG_INFO("Migration {} applied successfully", migration.version.c_str());
        return {true, "Migration applied successfully", migration.version, ""};
        
    } catch (const std::exception& e) {
        execute_sql_statements("ROLLBACK;");
        return {false, "Exception during migration: " + std::string(e.what()), migration.version, "Exception during migration: " + std::string(e.what())};
    }
}

std::string MigrationManager::parse_migration_section(const std::string& content, const std::string& section) {
    std::string begin_marker = "-- @" + section + "_begin";
    std::string end_marker = "-- @" + section + "_end";
    
    size_t begin_pos = content.find(begin_marker);
    if (begin_pos == std::string::npos) {
        return "";
    }
    
    begin_pos = content.find('\n', begin_pos) + 1;
    
    size_t end_pos = content.find(end_marker, begin_pos);
    if (end_pos == std::string::npos) {
        return "";
    }
    
    return content.substr(begin_pos, end_pos - begin_pos);
}

bool MigrationManager::execute_sql_statements(const std::string& sql) {
    try {
        _db->exec(sql);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SQL execution failed: {}", e.what());
        return false;
    }
}

std::string MigrationManager::create_backup(const std::string& reason) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << _backup_dir << "/backup_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    if (!reason.empty()) {
        ss << "_" << reason;
    }
    ss << ".db";
    
    std::string backup_path = ss.str();
    
    if (backup_database(backup_path)) {
        LOG_INFO("Database backup created: {}", backup_path.c_str());
        return backup_path;
    }
    
    return "";
}

bool MigrationManager::backup_database(const std::string& backup_path) {
    try {
        // 使用 SQLiteCpp 的 backup 方法
        _db->backup(backup_path.c_str(), SQLite::Database::Save);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Backup failed: {}", e.what());
        remove_file(backup_path);
        return false;
    }
}

bool MigrationManager::create_migration_script(const std::string& name, const std::string& description) {
    std::string version = generate_version();
    std::string filename = version + "_" + name + ".sql";
    std::string filepath = _migration_scripts_dir + "/" + filename;
    
    // 获取上一个版本号
    std::string down_revision = get_current_version();
    if (down_revision.empty()) {
        // 如果数据库中没有版本，尝试从文件系统扫描
        auto migrations = scan_migration_scripts();
        if (!migrations.empty()) {
            // 按版本号排序，获取最新的
            std::sort(migrations.begin(), migrations.end(), 
                [](const MigrationInfo& a, const MigrationInfo& b) {
                    return a.version > b.version;
                });
            down_revision = migrations[0].version;
        }
    }
    
    // 读取模板
    std::ifstream template_file("migrations/templates/migration_template.sql");
    if (!template_file.is_open()) {
        LOG_ERROR("Failed to open migration template");
        return false;
    }
    
    std::stringstream buffer;
    buffer << template_file.rdbuf();
    std::string content = buffer.str();
    
    // 替换占位符
    std::regex version_regex("\\{VERSION\\}");
    std::regex name_regex("\\{MIGRATION_NAME\\}");
    std::regex desc_regex("\\{DESCRIPTION\\}");
    std::regex timestamp_regex("\\{TIMESTAMP\\}");
    std::regex down_revision_regex("\\{DOWN_REVISION\\}");
    
    content = std::regex_replace(content, version_regex, version);
    content = std::regex_replace(content, name_regex, name);
    content = std::regex_replace(content, desc_regex, description);
    content = std::regex_replace(content, timestamp_regex, get_timestamp());
    content = std::regex_replace(content, down_revision_regex, down_revision.empty() ? "null" : down_revision);
    
    // 写入文件
    std::ofstream output_file(filepath);
    if (!output_file.is_open()) {
        LOG_ERROR("Failed to create migration file: {}", filepath.c_str());
        return false;
    }
    
    output_file << content;
    output_file.close();
    
    LOG_INFO("Migration script created: {}", filepath.c_str());
    return true;
}

void MigrationManager::set_config_path(const std::string& config_path) {
    _config_path = config_path;
}

void MigrationManager::set_auto_backup(bool enabled) {
    _auto_backup_enabled = enabled;
}

bool MigrationManager::remove_migration_record(const std::string& /* version */) {
    // 清除所有迁移记录，因为我们只维护单一的当前版本
    const char* sql = "DELETE FROM schema_migrations";
    
    try {
        _db->exec(sql);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to remove migration record: {}", e.what());
        return false;
    }
}

MigrationResult MigrationManager::upgrade_to_version(const std::string& target_version) {
    MigrationResult result;
    result.success = false;
    
    try {
        if (!initialize()) {
            result.error_message = "Failed to initialize migration manager";
            return result;
        }
        
        std::string current_version = get_current_version();
        if (current_version == target_version) {
            result.success = true;
            result.message = "Already at target version: " + target_version;
            return result;
        }
        
        auto all_migrations = scan_migration_scripts();
        
        // 找到目标版本的迁移
        auto target_it = std::find_if(all_migrations.begin(), all_migrations.end(),
            [&target_version](const MigrationInfo& m) { return m.version == target_version; });
        
        if (target_it == all_migrations.end()) {
            result.error_message = "Target version not found: " + target_version;
            return result;
        }
        
        // 应用从当前版本到目标版本的所有迁移
        for (auto it = all_migrations.begin(); it <= target_it; ++it) {
            if (it->version > current_version) {
                MigrationResult migration_result = apply_migration(*it, true);
                if (!migration_result.success) {
                    result.error_message = "Failed to apply migration " + it->version + ": " + migration_result.message;
                    return result;
                }
            }
        }
        
        result.success = true;
        result.message = "Successfully upgraded to version: " + target_version;
        return result;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Exception during upgrade: ") + e.what();
        return result;
    }
}

MigrationResult MigrationManager::downgrade_to_version(const std::string& target_version) {
    MigrationResult result;
    result.success = false;
    
    try {
        if (!initialize()) {
            result.error_message = "Failed to initialize migration manager";
            return result;
        }
        
        std::string current_version = get_current_version();
        if (current_version == target_version) {
            result.success = true;
            result.message = "Already at target version: " + target_version;
            return result;
        }
        
        auto all_migrations = scan_migration_scripts();
        
        // 找到当前版本和目标版本的位置
        auto current_it = std::find_if(all_migrations.begin(), all_migrations.end(),
            [&current_version](const MigrationInfo& m) { return m.version == current_version; });
        auto target_it = std::find_if(all_migrations.begin(), all_migrations.end(),
            [&target_version](const MigrationInfo& m) { return m.version == target_version; });
        
        if (current_it == all_migrations.end()) {
            result.error_message = "Current version not found: " + current_version;
            return result;
        }
        
        if (target_it == all_migrations.end()) {
            result.error_message = "Target version not found: " + target_version;
            return result;
        }
        
        // 从当前版本向下回滚到目标版本
        for (auto it = current_it; it > target_it; --it) {
            MigrationResult migration_result = apply_migration(*it, false);
            if (!migration_result.success) {
                result.error_message = "Failed to downgrade migration " + it->version + ": " + migration_result.message;
                return result;
            }
        }
        
        result.success = true;
        result.message = "Successfully downgraded to version: " + target_version;
        return result;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Exception during downgrade: ") + e.what();
        return result;
    }
}

NS_FT_END