#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "migration/migration_manager.h"

using namespace ft;

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] COMMAND [ARGS]\n";
    std::cout << "\nCommands:\n";
    std::cout << "  init                          Initialize migration system\n";
    std::cout << "  create <name>                 Create a new migration file\n";
    std::cout << "  upgrade [version]             Apply pending migrations (or to specific version)\n";
    std::cout << "  downgrade <version>           Downgrade to specific version\n";
    std::cout << "  status                        Show migration status\n";
    std::cout << "  version                       Show current database version\n";
    std::cout << "\nOptions:\n";

    std::cout << "  -h, --help                    Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " init\n";
    std::cout << "  " << program_name << " create add_user_table\n";
    std::cout << "  " << program_name << " upgrade\n";
    std::cout << "  " << program_name << " downgrade 20240101_001\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command;
    std::vector<std::string> args;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (command.empty()) {
            command = arg;
        } else {
            args.push_back(arg);
        }
    }

    if (command.empty()) {
        std::cerr << "Error: No command specified\n";
        print_usage(argv[0]);
        return 1;
    }

    try {
        // 创建MigrationManager实例
        MigrationManager manager;
        
        if (command == "init") {
            // init命令不需要现有数据库，只需要初始化系统
            if (manager.initialize()) {
                std::cout << "Migration system initialized successfully\n";
                return 0;
            } else {
                std::cerr << "Failed to initialize migration system\n";
                return 1;
            }
        }
        
        // 对于其他命令，需要先初始化manager
        if (!manager.initialize()) {
            std::cerr << "Failed to initialize migration manager\n";
            return 1;
        }

        if (command == "create") {
            if (args.empty()) {
                std::cerr << "Error: create command requires migration name\n";
                return 1;
            }
            
            if (manager.create_migration_script(args[0], "Auto-generated migration")) {
                std::cout << "Migration created successfully: " << args[0] << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to create migration\n";
                return 1;
            }
        }
        else if (command == "upgrade") {
            MigrationResult result;
            if (args.empty()) {
                // 升级到最新版本
                result = manager.upgrade_to_latest();
            } else {
                // 升级到指定版本
                result = manager.upgrade_to_version(args[0]);
            }
            
            if (result.success) {
                std::cout << "Upgrade completed successfully\n";
                if (!result.message.empty()) {
                    std::cout << "Message: " << result.message << std::endl;
                }
                return 0;
            } else {
                std::cerr << "Upgrade failed: " << result.message << std::endl;
                return 1;
            }
        }
        else if (command == "downgrade") {
            if (args.empty()) {
                std::cerr << "Error: downgrade command requires target version\n";
                return 1;
            }
            
            auto result = manager.downgrade_to_version(args[0]);
            if (result.success) {
                std::cout << "Downgrade completed successfully\n";
                if (!result.message.empty()) {
                    std::cout << "Message: " << result.message << std::endl;
                }
                return 0;
            } else {
                std::cerr << "Downgrade failed: " << result.message << std::endl;
                return 1;
            }
        }
        else if (command == "status") {
            auto pending = manager.get_pending_migrations();
            auto current = manager.get_current_version();
            
            std::cout << "Current database version: " << (current.empty() ? "(none)" : current) << std::endl;
            std::cout << "Pending migrations: " << pending.size() << std::endl;
            
            if (!pending.empty()) {
                std::cout << "\nPending migrations:\n";
                for (const auto& migration : pending) {
                    std::cout << "  - " << migration.version << ": " << migration.name << std::endl;
                }
            }
            return 0;
        }
        else if (command == "version") {
            auto current = manager.get_current_version();
            std::cout << (current.empty() ? "(none)" : current) << std::endl;
            return 0;
        }
        else {
            std::cerr << "Error: Unknown command '" << command << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown error occurred\n";
        return 1;
    }

    return 0;
}