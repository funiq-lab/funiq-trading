#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "shared_memory.h"
#include "types/base_types.h"
#include "types/quote_types.h"
#include "types/trade_types.h"
#include "core/macros.h"
#include "utils/file.hpp"
#include <stdint.h>
#include <stddef.h>
#include <fstream>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <shared_mutex> 
#include <unordered_set>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <future>
#include <atomic>
#include <thread>

NS_FT_BEGIN

// 前向声明
class IDataSource;
using DataSourcePtr = std::shared_ptr<IDataSource>;

// 数据源类型枚举
typedef enum {
    DATA_SOURCE_SHARED_MEMORY = 0,
    DATA_SOURCE_FILE = 1,
    DATA_SOURCE_NETWORK = 2,
    DATA_SOURCE_DATABASE = 3,
    DATA_SOURCE_CUSTOM = 99
} DataSourceType;

// 记录头结构，包含魔数用于验证完整性
struct RecordHeader {
    uint32_t magic_start;      // 魔数开始标记 0xDEADBEEF
    uint64_t timestamp;
    FTDataType data_type;
    uint32_t data_size;
    uint32_t checksum;         // 数据校验和
    uint32_t magic_end;        // 魔数结束标记 0xBEEFDEAD
} __attribute__((packed));

constexpr uint32_t RECORD_MAGIC_START = 0xDEADBEEF;
constexpr uint32_t RECORD_MAGIC_END = 0xBEEFDEAD;

/**
 * 数据源接口基类
 */
class IDataSource {
public:
    virtual ~IDataSource() = default;
    
    // 基本操作
    virtual bool initialize(const std::string& config) = 0;
    virtual void cleanup() = 0;
    virtual bool is_available() const = 0;
    
    // 数据操作
    virtual ssize_t write(FTDataType data_type, const void* data, size_t size) = 0;
    virtual ssize_t read(FTDataType data_type, void* buffer, size_t buffer_size) = 0;
    virtual int read_batch(FTDataType data_type, ShmDataPacket* packets, int max_count) = 0;
    
    // 统计信息
    virtual void get_stats(FTDataType data_type, uint64_t* total_written, uint64_t* total_read, uint64_t* available_space) const = 0;
    virtual void reset_stats() = 0;
    
    // 属性
    virtual DataSourceType get_type() const = 0;
    virtual const std::string& get_name() const = 0;
    virtual const std::string& get_last_error() const = 0;
    
    // 数据类型管理
    virtual bool enable_data_types(const std::vector<FTDataType>& data_types, size_t size_per_type) = 0;
    virtual std::vector<FTDataType> get_enabled_data_types() const = 0;
    
    virtual void stop() = 0;  // 设置停止标志，让读写操作主动退出
    virtual bool is_stopped() const = 0;  // 检查是否已停止
};

/**
 * 类型化共享内存数据源实现
 */
class TypedSharedMemoryDataSource : public IDataSource {
public:
    explicit TypedSharedMemoryDataSource(const std::string& name);
    ~TypedSharedMemoryDataSource() override;
    
    // IDataSource 接口实现
    bool initialize(const std::string& config) override;
    void cleanup() override;
    bool is_available() const override;
    
    ssize_t write(FTDataType data_type, const void* data, size_t size) override;
    ssize_t read(FTDataType data_type, void* buffer, size_t buffer_size) override;
    int read_batch(FTDataType data_type, ShmDataPacket* packets, int max_count) override;
    
    void get_stats(FTDataType data_type, uint64_t* total_written, uint64_t* total_read, uint64_t* available_space) const override;
    void reset_stats() override;
    
    DataSourceType get_type() const override { return DATA_SOURCE_SHARED_MEMORY; }
    const std::string& get_name() const override { return _name; }
    const std::string& get_last_error() const override { return _last_error; }
    
    // 数据类型管理
    bool enable_data_types(const std::vector<FTDataType>& data_types, size_t size_per_type) override;
    std::vector<FTDataType> get_enabled_data_types() const override;
    
    // 优雅停止方法
    void stop() override;
    bool is_stopped() const override;
    
private:
    std::string _name;
    std::string _last_error;
    std::string _base_shm_name;     // 基础共享内存名称
    size_t _size_per_type;          // 每种数据类型的共享内存大小
    bool _is_creator;               // 是否为创建者
    
    std::unordered_set<FTDataType> _enabled_data_types;  // 启用的数据类型
    mutable std::shared_mutex _mutex;
    
    std::atomic<bool> _stopped;
    
    void _set_error(const std::string& error);
    bool _is_data_type_enabled(FTDataType data_type) const;
    bool _initialize_typed_shared_memories();
    void _cleanup_typed_shared_memories();
};

/**
 * 文件数据源实现 - 按数据类型分离存储，使用无锁记录边界保护
 */
class TypedFileDataSource : public IDataSource {
public:
    explicit TypedFileDataSource(const std::string& name);
    ~TypedFileDataSource() override;
    
    // IDataSource 接口实现
    bool initialize(const std::string& config) override;
    void cleanup() override;
    bool is_available() const override;
    
    ssize_t write(FTDataType data_type, const void* data, size_t size) override;
    ssize_t read(FTDataType data_type, void* buffer, size_t buffer_size) override;
    int read_batch(FTDataType data_type, ShmDataPacket* packets, int max_count) override;
    
    void get_stats(FTDataType data_type, uint64_t* total_written, uint64_t* total_read, uint64_t* available_space) const override;
    void reset_stats() override;
    
    DataSourceType get_type() const override { return DATA_SOURCE_FILE; }
    const std::string& get_name() const override { return _name; }
    const std::string& get_last_error() const override { return _last_error; }
    
    // 数据类型管理
    bool enable_data_types(const std::vector<FTDataType>& data_types, size_t size_per_type) override;
    std::vector<FTDataType> get_enabled_data_types() const override;
    
    // 优雅停止方法
    void stop() override;
    bool is_stopped() const override;
    
private:
    std::string _name;
    mutable std::string _last_error;
    std::string _base_file_path;
    bool _append_mode;
    bool _auto_flush;
    
    std::unordered_set<FTDataType> _enabled_data_types;
    
    // 分离读写流
    std::unordered_map<FTDataType, std::unique_ptr<std::ofstream>> _write_streams;
    mutable std::unordered_map<FTDataType, std::unique_ptr<std::ifstream>> _read_streams;
    
    mutable std::shared_mutex _mutex;
    
    std::atomic<bool> _stopped;
    
    void _set_error(const std::string& error) const;
    bool _is_data_type_enabled(FTDataType data_type) const;
    std::string _get_file_path_for_type(FTDataType data_type) const;
    bool _initialize_typed_files();
    void _cleanup_typed_files();
    
    // 无锁原子写入方法
    bool _atomic_write_record(FTDataType data_type, const void* record_data, size_t record_size);
    
    // 安全的读取方法
    bool _read_binary_data(FTDataType data_type, void* buffer, size_t buffer_size, ssize_t* bytes_read) const;
    int _read_binary_batch(FTDataType data_type, ShmDataPacket* packets, int max_count) const;
    
    // 读取流管理（延迟初始化）
    std::ifstream* _get_read_stream(FTDataType data_type) const;
    
    // 记录完整性验证和恢复
    bool _seek_to_next_valid_record(std::ifstream* stream, FTDataType data_type) const;
    uint32_t _calculate_checksum(const void* data, size_t size) const;
    
    bool _should_refresh_read_stream(std::ifstream* stream, FTDataType data_type) const;
};

/**
 * 数据源工厂
 */
class DataSourceFactory {
public:
    static DataSourcePtr create_data_source(DataSourceType type, const std::string& name);
    static bool register_data_source_creator(DataSourceType type, 
        std::function<DataSourcePtr(const std::string&)> creator);
    
private:
    static std::unordered_map<DataSourceType, std::function<DataSourcePtr(const std::string&)>> _creators;
    static std::shared_mutex _creators_mutex;
};

/**
 * 简化的数据管理器类 - 只支持数据类型分离
 */
class DataManager {
public:
    explicit DataManager(const std::string& name);
    ~DataManager();
    
    // 数据源管理
    bool register_data_source(const DataConfig& config, const std::vector<FTDataType>& data_types, size_t size_per_type);
    bool release_data_source();
    DataSourceType get_current_source_type() const;
    
    // 数据操作 - 全部按数据类型分离
    ssize_t write(FTDataType data_type, const void* data, size_t size);
    ssize_t read(FTDataType data_type, void* buffer, size_t buffer_size);
    int read_batch(FTDataType data_type, ShmDataPacket* packets, int max_count);
    
    // 便捷接口 - 行情数据
    ssize_t write_index_data(const IndexData& data);
    ssize_t write_depth_data(const DepthData& data);
    ssize_t write_entrust_data(const EntrustData& data);
    ssize_t write_trade_data(const TradeData& data);
    ssize_t write_depth_fifty_data(const DepthFiftyData& data);
    
    // 便捷接口 - 按类型批量读取
    int read_index_batch(ShmDataPacket* packets, int max_count);
    int read_depth_batch(ShmDataPacket* packets, int max_count);
    int read_entrust_batch(ShmDataPacket* packets, int max_count);
    int read_trade_batch(ShmDataPacket* packets, int max_count);
    int read_depth_fifty_batch(ShmDataPacket* packets, int max_count);

    
    // 便捷接口 - 交易数据
    ssize_t write_trade_order(const TradeOrderData& data);
    ssize_t write_trade_fill(const TradeFillData& data);
    ssize_t write_position_data(const PositionData& data);
    ssize_t write_account_data(const AccountData& data);
    ssize_t write_bank_securities_transfer(const BankSecuritiesTransferData& data);
    ssize_t write_fund_transfer(const FundTransferData& data);
    
    // 便捷接口 - 交易响应数据
    ssize_t write_order_rsp(const TradeOrderRspData& data);
    ssize_t write_cancel_rsp(const TradeOrderCancelRsqData& data);
    
    // 便捷接口 - 按类型批量读取
    int read_trade_order_batch(ShmDataPacket* packets, int max_count);
    int read_trade_fill_batch(ShmDataPacket* packets, int max_count);
    int read_position_batch(ShmDataPacket* packets, int max_count);
    int read_account_batch(ShmDataPacket* packets, int max_count);
    int read_bank_securities_transfer_batch(ShmDataPacket* packets, int max_count);
    int read_fund_transfer_batch(ShmDataPacket* packets, int max_count);

    
    // 状态查询
    const std::string& get_name() const { return _name; }
    const std::string& get_last_error() const { return _last_error; }
    bool is_available() const;
    std::vector<FTDataType> get_enabled_data_types() const;
    
    // 统计信息
    void get_stats(FTDataType data_type, uint64_t* total_written, uint64_t* total_read, uint64_t* available_space) const;
    void reset_stats();
    
    // 优雅停止方法
    void stop();
    bool is_stopped() const;
    
private:
    std::string _name;
    std::string _last_error;
    
    // 当前数据源
    DataSourcePtr _current_source;
    
    // 线程安全
    mutable std::shared_mutex _mutex;
    
    std::atomic<bool> _stopped;
    
    void _set_error(const std::string& error);
};

NS_FT_END

#endif // DATA_MANAGER_H