#pragma once

#include <string>
#include <vector>
#include <memory>
#include <SQLiteCpp/SQLiteCpp.h>
#include "core/macros.h"

NS_FT_BEGIN

/**
 * 迁移信息结构体
 */
struct MigrationInfo {
    std::string version;       // 12位随机版本号
    std::string down_revision; // 上一个版本号
    std::string name;          // 迁移名称
    std::string description;   // 迁移描述
    std::string timestamp;     // 创建时间戳
    std::string file_path;     // 迁移脚本文件路径
    bool applied;              // 是否已应用
    std::string applied_at;    // 应用时间
};

/**
 * 迁移操作结果
 */
struct MigrationResult {
    bool success;
    std::string message;
    std::string version;
    std::string error_message;
};

/**
 * 数据库迁移管理器
 */
class MigrationManager {
public:
    explicit MigrationManager();
    ~MigrationManager();

    // 初始化迁移系统
    bool initialize();
    
    // 版本管理
    std::string get_current_version();
    std::vector<MigrationInfo> get_pending_migrations();
    std::vector<MigrationInfo> get_applied_migrations();
    
    // 迁移操作
    MigrationResult upgrade_to_latest();
    MigrationResult upgrade_to_version(const std::string& target_version);
    MigrationResult downgrade_to_version(const std::string& target_version);
    
    // 迁移脚本管理
    std::string generate_version();
    bool create_migration_script(const std::string& name, const std::string& description);
    
    // 备份管理
    std::string create_backup(const std::string& reason = "");
    bool restore_backup(const std::string& backup_file);
    std::vector<std::string> list_backups();
    
    // 验证和检查
    bool validate_migration_script(const std::string& script_path);
    bool check_database_consistency();
    
    // 工具函数
    void set_config_path(const std::string& config_path);
    void set_auto_backup(bool enabled);
    
private:
    std::string _db_path;
    std::string _config_path;
    std::string _migration_scripts_dir;
    std::string _backup_dir;
    bool _auto_backup_enabled;
    std::unique_ptr<SQLite::Database> _db;
    
    // 内部方法
    bool create_migration_table();
    bool load_config();
    std::vector<MigrationInfo> scan_migration_scripts();
    MigrationResult apply_migration(const MigrationInfo& migration, bool is_upgrade = true);
    bool record_migration(const MigrationInfo& migration);
    bool remove_migration_record(const std::string& version);
    std::string parse_migration_section(const std::string& content, const std::string& section);
    bool execute_sql_statements(const std::string& sql);
    std::string get_timestamp();
    bool backup_database(const std::string& backup_path);
};

NS_FT_END