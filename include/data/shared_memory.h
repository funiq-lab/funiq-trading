#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "types/base_types.h"
#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include "core/macros.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <condition_variable>

NS_FT_BEGIN
// 共享内存头结构
struct SharedMemoryHeader {
    std::atomic<uint64_t> write_index;     // 写指针
    std::atomic<uint64_t> read_index;      // 读指针
    std::atomic<uint32_t> total_size;      // 总大小
    std::atomic<uint32_t> data_size;       // 数据区大小
    boost::interprocess::interprocess_mutex mutex;  // 主互斥锁
    
    // 溢出重置相关
    std::atomic<uint64_t> reset_counter;   // 重置计数器
    std::atomic<bool> reset_in_progress;   // 重置中标志
    
    // 进程间条件变量（用于重置等待）
    boost::interprocess::interprocess_mutex reset_mutex;
    boost::interprocess::interprocess_condition reset_condition;
    
    SharedMemoryHeader() : write_index(0), read_index(0), total_size(0), 
                          data_size(0), reset_counter(0), reset_in_progress(false) {}
};

// 内部句柄结构
struct SharedMemoryHandle {
    std::string name;
    boost::interprocess::shared_memory_object* shm_obj;
    boost::interprocess::mapped_region* region;
    SharedMemoryHeader* header;
    void* data_ptr;
    size_t total_size;      // 添加缺少的成员：总大小的本地副本
    
    SharedMemoryHandle() : shm_obj(nullptr), region(nullptr), 
                          header(nullptr), data_ptr(nullptr), total_size(0) {}
    ~SharedMemoryHandle();
};

class SharedMemoryManager {
private:
    static SharedMemoryManager* _instance;
    static std::mutex _instance_mutex;
    
    mutable std::mutex _handles_mutex;
    std::unordered_map<std::string, SharedMemoryHandle*> _name_to_handle;
    std::unordered_map<std::string, size_t> _reference_count;
    
    // 溢出检测阈值配置
    static constexpr uint64_t OVERFLOW_THRESHOLD = UINT64_MAX / 2;  // 50%阈值触发重置
    static constexpr uint64_t SAFE_RESET_POINT = UINT64_MAX / 4;    // 重置后的起始点
    
    // 数据类型到共享内存名称的映射
    static const std::unordered_map<FTDataType, std::string> DATA_TYPE_SHM_NAMES;
    
    SharedMemoryManager() = default;
    ~SharedMemoryManager();
    
    // 内部辅助方法
    SharedMemoryHandle* _get_handle(const std::string& name) const;
    ssize_t _write_data_to_handle(SharedMemoryHandle* handle, FTDataType data_type, 
                                  const void* data, size_t size);
    ssize_t _read_data_from_handle(SharedMemoryHandle* handle, FTDataType* data_type, 
                                   void* buffer, size_t buffer_size);
    int _read_batch_from_handle(SharedMemoryHandle* handle, ShmDataPacket* packets, int max_count);
    
    // 引用计数方法
    void _remove_reference(const std::string& name);
    
    // 溢出检测和重置方法
    bool _check_and_reset_overflow(SharedMemoryHandle* handle);
    bool _safe_reset_indices(SharedMemoryHandle* handle);
    bool _wait_for_reset_completion(SharedMemoryHandle* handle);
    uint64_t _normalize_index(uint64_t index, uint32_t data_size) const;
    bool _is_overflow_risk(uint64_t index) const;
    
public:
    static SharedMemoryManager& get_instance();
    
    // 根据数据类型获取共享内存名称
    std::string get_shm_name_for_type(const std::string& base_name, FTDataType data_type) const;
    // 禁用拷贝和赋值
    SharedMemoryManager(const SharedMemoryManager&) = delete;
    SharedMemoryManager& operator=(const SharedMemoryManager&) = delete;
    
    // 核心接口
    bool create_shared_memory(const std::string& name, size_t size);
    bool open_shared_memory(const std::string& name);
    bool close_shared_memory(const std::string& name);
    bool remove_shared_memory(const std::string& name);
    bool safe_remove_shared_memory(const std::string& name);
    
    // 数据操作接口
    ssize_t write_data(const std::string& name, FTDataType data_type, const void* data, size_t size);
    ssize_t read_data(const std::string& name, FTDataType* data_type, void* buffer, size_t buffer_size);
    int read_batch(const std::string& name, ShmDataPacket* packets, int max_count);
    
    // 状态查询接口
    bool is_open(const std::string& name) const;
    size_t get_reference_count(const std::string& name) const;
    std::vector<std::string> get_open_memory_names() const;
    bool get_stats(const std::string& name, uint64_t* total_written, uint64_t* total_read, 
                  uint64_t* available_space) const;
    
    // 溢出状态查询
    bool get_overflow_stats(const std::string& name, uint64_t* reset_count, bool* reset_in_progress) const;
    bool is_overflow_risk(const std::string& name) const;
    
    // 创建特定数据类型的共享内存
    bool create_typed_shared_memory(const std::string& base_name, FTDataType data_type, size_t size);
    
    // 打开特定数据类型的共享内存
    bool open_typed_shared_memory(const std::string& base_name, FTDataType data_type);
    
    // 关闭特定数据类型的共享内存
    bool close_typed_shared_memory(const std::string& base_name, FTDataType data_type);
    
    // 写入特定类型的数据
    ssize_t write_typed_data(const std::string& base_name, FTDataType data_type, 
                            const void* data, size_t size);
    
    // 读取特定类型的数据
    ssize_t read_typed_data(const std::string& base_name, FTDataType data_type, 
                           void* buffer, size_t buffer_size);
    
    // 批量读取特定类型的数据
    int read_typed_batch(const std::string& base_name, FTDataType data_type, 
                        ShmDataPacket* packets, int max_count);
    
    // 检查数据类型是否支持分离存储
    bool is_data_type_supported(FTDataType data_type) const;
    
    // 获取所有支持的数据类型
    std::vector<FTDataType> get_supported_data_types() const;
};

NS_FT_END

#endif // SHARED_MEMORY_H