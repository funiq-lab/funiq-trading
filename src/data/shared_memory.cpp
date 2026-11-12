#include "data/shared_memory.h"
#include "core/macros.h"
#include "logger/logger.hpp"
#include <cstring>
#include <chrono>
#include <thread>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <algorithm>
#include <unordered_map>

using namespace boost::interprocess;

NS_FT_BEGIN

// SharedMemoryHandle 析构函数
SharedMemoryHandle::~SharedMemoryHandle() {
    if (region) {
        delete region;
        region = nullptr;
    }
    if (shm_obj) {
        delete shm_obj;
        shm_obj = nullptr;
    }
}

// SharedMemoryManager 实现
SharedMemoryManager& SharedMemoryManager::get_instance() {
    static SharedMemoryManager instance;
    return instance;
}

SharedMemoryManager::~SharedMemoryManager() {
    // 清理所有句柄
    std::lock_guard<std::mutex> lock(_handles_mutex);
    for (auto& pair : _name_to_handle) {
        delete pair.second;
    }
    _name_to_handle.clear();
    _reference_count.clear();
}

SharedMemoryHandle* SharedMemoryManager::_get_handle(const std::string& name) const {
    std::lock_guard<std::mutex> lock(_handles_mutex);
    auto it = _name_to_handle.find(name);
    return (it != _name_to_handle.end()) ? it->second : nullptr;
}

void SharedMemoryManager::_remove_reference(const std::string& name) {
    std::lock_guard<std::mutex> lock(_handles_mutex);
    auto it = _reference_count.find(name);
    if (it != _reference_count.end()) {
        it->second--;
        
        if (it->second <= 0) {
            // 引用计数归零，清理句柄
            auto handle_it = _name_to_handle.find(name);
            if (handle_it != _name_to_handle.end()) {
                delete handle_it->second;
                _name_to_handle.erase(handle_it);
            }
            _reference_count.erase(it);
        }
    }
}

bool SharedMemoryManager::create_shared_memory(const std::string& name, size_t size) {
    if (name.empty() || size == 0) {
        LOG_ERROR("Invalid parameters for create_shared_memory: name='{}', size={}", name, size);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(_handles_mutex);
    
    // 检查是否已经存在
    if (_name_to_handle.find(name) != _name_to_handle.end()) {
        LOG_WARN("Shared memory '{}' already exists", name);
        _reference_count[name]++;
        return true;
    }
    
    try {
        // 先删除可能存在的同名共享内存
        shared_memory_object::remove(name.c_str());
        
        auto handle = new SharedMemoryHandle();
        handle->name = name;
        handle->total_size = size;
        
        handle->shm_obj = new shared_memory_object(create_only, name.c_str(), read_write);
        handle->shm_obj->truncate(size);
        handle->region = new mapped_region(*handle->shm_obj, read_write);
        
        handle->header = static_cast<SharedMemoryHeader*>(handle->region->get_address());
        new (handle->header) SharedMemoryHeader();
        
        handle->header->write_index.store(0);
        handle->header->read_index.store(0);
        handle->header->total_size.store(static_cast<uint32_t>(size));
        handle->header->data_size.store(static_cast<uint32_t>(size - sizeof(SharedMemoryHeader)));
        
        handle->data_ptr = static_cast<char*>(handle->region->get_address()) + sizeof(SharedMemoryHeader);
        
        // 添加到映射中
        _name_to_handle[name] = handle;
        _reference_count[name] = 1;
        
        LOG_INFO("Created shared memory: {}, size: {} bytes", name, size);
        return true;
        
    } catch (const interprocess_exception& e) {
        LOG_ERROR("Failed to create shared memory {}: {}", name, e.what());
        return false;
    }
}

bool SharedMemoryManager::open_shared_memory(const std::string& name) {
    if (name.empty()) {
        LOG_ERROR("Empty name for open_shared_memory");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(_handles_mutex);
    
    // 检查是否已经打开
    if (_name_to_handle.find(name) != _name_to_handle.end()) {
        _reference_count[name]++;
        return true;
    }
    
    try {
        auto handle = new SharedMemoryHandle();
        handle->name = name;
        
        handle->shm_obj = new shared_memory_object(open_only, name.c_str(), read_write);
        handle->region = new mapped_region(*handle->shm_obj, read_write);
        
        handle->header = static_cast<SharedMemoryHeader*>(handle->region->get_address());
        handle->total_size = handle->header->total_size.load();
        handle->data_ptr = static_cast<char*>(handle->region->get_address()) + sizeof(SharedMemoryHeader);
        
        // 添加到映射中
        _name_to_handle[name] = handle;
        _reference_count[name] = 1;
        
        LOG_INFO("Opened shared memory: {}", name);
        return true;
        
    } catch (const interprocess_exception& e) {
        LOG_ERROR("Failed to open shared memory {}: {}", name, e.what());
        return false;
    }
}

bool SharedMemoryManager::close_shared_memory(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    
    _remove_reference(name);
    return true;
}

bool SharedMemoryManager::remove_shared_memory(const std::string& name) {
    size_t ref_count = get_reference_count(name);
    
    if (ref_count > 0) {
        LOG_INFO("Found {} references for '{}', automatically using safe removal", 
                 ref_count, name);
        return safe_remove_shared_memory(name);
    }
    
    // 没有引用，直接删除
    bool result = shared_memory_object::remove(name.c_str());
    
    if (result) {
        LOG_INFO("Removed shared memory '{}'", name);
    } else {
        LOG_ERROR("Failed to remove shared memory '{}'", name);
    }
    
    return result;
}

bool SharedMemoryManager::safe_remove_shared_memory(const std::string& name) {
    size_t ref_count = get_reference_count(name);
    
    if (ref_count > 0) {
        LOG_INFO("Force closing shared memory '{}' with {} references", name, ref_count);
        
        // 强制清理
        std::lock_guard<std::mutex> lock(_handles_mutex);
        auto handle_it = _name_to_handle.find(name);
        if (handle_it != _name_to_handle.end()) {
            delete handle_it->second;
            _name_to_handle.erase(handle_it);
        }
        _reference_count.erase(name);
    }
    
    // 删除共享内存
    bool result = shared_memory_object::remove(name.c_str());
    
    if (result) {
        LOG_INFO("Successfully removed shared memory '{}' after force cleanup", name);
    } else {
        LOG_ERROR("Failed to remove shared memory '{}' after force cleanup", name);
    }
    
    return result;
}

ssize_t SharedMemoryManager::write_data(const std::string& name, FTDataType data_type, 
                                       const void* data, size_t size) {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        LOG_ERROR("Shared memory '{}' not found for write operation", name);
        return -1;
    }
    
    return _write_data_to_handle(handle, data_type, data, size);
}

ssize_t SharedMemoryManager::read_data(const std::string& name, FTDataType* data_type, 
                                      void* buffer, size_t buffer_size) {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        LOG_ERROR("Shared memory '{}' not found for read operation", name);
        return -1;
    }
    
    return _read_data_from_handle(handle, data_type, buffer, buffer_size);
}

int SharedMemoryManager::read_batch(const std::string& name, ShmDataPacket* packets, int max_count) {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        LOG_ERROR("Shared memory '{}' not found for batch read operation", name);
        return -1;
    }
    
    return _read_batch_from_handle(handle, packets, max_count);
}

bool SharedMemoryManager::is_open(const std::string& name) const {
    std::lock_guard<std::mutex> lock(_handles_mutex);
    return _name_to_handle.find(name) != _name_to_handle.end();
}

size_t SharedMemoryManager::get_reference_count(const std::string& name) const {
    std::lock_guard<std::mutex> lock(_handles_mutex);
    auto it = _reference_count.find(name);
    return (it != _reference_count.end()) ? it->second : 0;
}

std::vector<std::string> SharedMemoryManager::get_open_memory_names() const {
    std::lock_guard<std::mutex> lock(_handles_mutex);
    std::vector<std::string> names;
    names.reserve(_name_to_handle.size());
    
    for (const auto& pair : _name_to_handle) {
        names.push_back(pair.first);
    }
    
    return names;
}

bool SharedMemoryManager::get_stats(const std::string& name, uint64_t* total_written, 
                                   uint64_t* total_read, uint64_t* available_space) const {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        return false;
    }
    
    if (total_written) {
        *total_written = handle->header->write_index.load();
    }
    
    if (total_read) {
        *total_read = handle->header->read_index.load();
    }
    
    if (available_space) {
        uint64_t write_idx = handle->header->write_index.load();
        uint64_t read_idx = handle->header->read_index.load();
        uint32_t data_size = handle->header->data_size.load();
        
        *available_space = (read_idx <= write_idx) ? 
            (data_size - (write_idx - read_idx)) : (read_idx - write_idx);
    }
    
    return true;
}

// 内部数据操作方法实现
ssize_t SharedMemoryManager::_write_data_to_handle(SharedMemoryHandle* handle, 
                                                    FTDataType data_type, 
                                                    const void* data, size_t size) {
    if (!handle || !data || size == 0) {
        LOG_ERROR("Invalid parameters for write: handle={}, data={}, size={}", 
                  static_cast<void*>(handle), static_cast<const void*>(data), size);
        return -1;
    }
    
    // 溢出检测和处理
    if (!_check_and_reset_overflow(handle)) {
        LOG_ERROR("Failed to handle potential overflow for shared memory '{}'", handle->name);
        return -1;
    }
    
    size_t packet_size = sizeof(ShmDataPacket) + size - 1;  // -1 因为ShmDataPacket已包含data[1]
    uint32_t data_size = handle->header->data_size.load();
    
    if (packet_size > data_size) {
        LOG_ERROR("Packet size {} exceeds available data size {}", packet_size, data_size);
        return -1;
    }
    
    scoped_lock<interprocess_mutex> lock(handle->header->mutex);
    
    uint64_t write_idx = handle->header->write_index.load();
    uint64_t read_idx = handle->header->read_index.load();
    
    // 使用取模运算进行环形缓冲区计算
    uint64_t write_pos = _normalize_index(write_idx, data_size);
    
    // 计算可用空间（基于实际位置）
    uint64_t available_space;
    if (write_idx >= read_idx) {
        // 正常情况：写指针在读指针后面
        uint64_t used_space = write_idx - read_idx;
        available_space = data_size - used_space;
    } else {
        // 异常情况：应该不会发生，但保险起见
        LOG_ERROR("Inconsistent index state: write_idx={}, read_idx={}", write_idx, read_idx);
        return -1;
    }
    
    if (packet_size > available_space) {
        LOG_WARN("Insufficient space: need {}, available {} for '{}'", 
                 packet_size, available_space, handle->name);
        return -1;
    }
    
    char* write_ptr = static_cast<char*>(handle->data_ptr) + write_pos;
    
    ShmDataPacket packet;
    packet.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    packet.data_type = data_type;
    packet.data_size = static_cast<uint32_t>(size);
    
    // 处理环形缓冲区跨边界写入
    size_t remaining_space = data_size - write_pos;
    size_t header_size = sizeof(ShmDataPacket) - 1;  // 减去data[1]的大小
    
    if (packet_size <= remaining_space) {
        // 一次性写入
        memcpy(write_ptr, &packet, header_size);
        memcpy(write_ptr + header_size, data, size);
    } else {
        // 分段写入
        if (remaining_space >= header_size) {
            // 包头能完整写入
            memcpy(write_ptr, &packet, header_size);
            size_t data_first_part = remaining_space - header_size;
            memcpy(write_ptr + header_size, data, data_first_part);
            memcpy(handle->data_ptr, static_cast<const char*>(data) + data_first_part, 
                   size - data_first_part);
        } else {
            // 包头跨边界
            memcpy(write_ptr, &packet, remaining_space);
            memcpy(handle->data_ptr, reinterpret_cast<const char*>(&packet) + remaining_space, 
                   header_size - remaining_space);
            memcpy(static_cast<char*>(handle->data_ptr) + header_size - remaining_space, 
                   data, size);
        }
    }
    
    handle->header->write_index.store(write_idx + packet_size);
    
    return static_cast<ssize_t>(size);
}

ssize_t SharedMemoryManager::_read_data_from_handle(SharedMemoryHandle* handle, 
                                                     FTDataType* data_type, 
                                                     void* buffer, size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) {
        return -1;
    }
    
    // 溢出检测和处理
    if (!_check_and_reset_overflow(handle)) {
        LOG_ERROR("Failed to handle potential overflow for shared memory '{}'", handle->name);
        return -1;
    }
    
    scoped_lock<interprocess_mutex> lock(handle->header->mutex);
    
    uint64_t write_idx = handle->header->write_index.load();
    uint64_t read_idx = handle->header->read_index.load();

    
    if (read_idx >= write_idx) {
        return 0; // 无数据
    }
    
    uint32_t data_size = handle->header->data_size.load();
    uint64_t read_pos = _normalize_index(read_idx, data_size);
    char* read_ptr = static_cast<char*>(handle->data_ptr) + read_pos;
    
    ShmDataPacket packet;
    size_t remaining_space = data_size - read_pos;
    size_t header_size = sizeof(ShmDataPacket) - 1;  // 减去data[1]的大小
    
    // 读取包头（处理跨边界情况）
    if (remaining_space >= header_size) {
        memcpy(&packet, read_ptr, header_size);
    } else {
        // 跨边界读取包头
        memcpy(&packet, read_ptr, remaining_space);
        memcpy(reinterpret_cast<char*>(&packet) + remaining_space, handle->data_ptr, 
               header_size - remaining_space);
    }
    
    if (buffer_size < packet.data_size) {
        LOG_ERROR("Buffer size {} insufficient for data size {}", buffer_size, packet.data_size);
        return -1;
    }
    
    if (data_type) {
        *data_type = packet.data_type;
    }
    
    // 读取数据（处理跨边界情况）
    size_t packet_size = header_size + packet.data_size;
    if (remaining_space >= packet_size) {
        // 一次性读取
        memcpy(buffer, read_ptr + header_size, packet.data_size);
    } else {
        // 分段读取
        if (remaining_space > header_size) {
            size_t data_first_part = remaining_space - header_size;
            memcpy(buffer, read_ptr + header_size, data_first_part);
            memcpy(static_cast<char*>(buffer) + data_first_part, handle->data_ptr, 
                   packet.data_size - data_first_part);
        } else {
            size_t data_offset = header_size - remaining_space;
            memcpy(buffer, static_cast<char*>(handle->data_ptr) + data_offset, packet.data_size);
        }
    }
    
    handle->header->read_index.store(read_idx + packet_size);
    return static_cast<ssize_t>(packet.data_size);
}

int SharedMemoryManager::_read_batch_from_handle(SharedMemoryHandle* handle, 
                                                  ShmDataPacket* packets, int max_count) {
    if (!handle || !packets || max_count <= 0) {
        return -1;
    }
    
    if (!_check_and_reset_overflow(handle)) {
        LOG_ERROR("Failed to handle potential overflow for shared memory '{}'", handle->name);
        return -1;
    }
    
    scoped_lock<interprocess_mutex> lock(handle->header->mutex);
    
    int count = 0;
    size_t header_size = sizeof(ShmDataPacket) - 1;  // 减去data[1]的大小
    
    while (count < max_count) {
        uint64_t write_idx = handle->header->write_index.load();
        uint64_t read_idx = handle->header->read_index.load();
        
        if (read_idx >= write_idx) {
            break; // 无更多数据
        }
        
        uint32_t data_size = handle->header->data_size.load();
        uint64_t read_pos = _normalize_index(read_idx, data_size);
        char* read_ptr = static_cast<char*>(handle->data_ptr) + read_pos;
        
        ShmDataPacket packet_header;
        size_t remaining_space = data_size - read_pos;
        
        // 读取包头
        if (remaining_space >= header_size) {
            memcpy(&packet_header, read_ptr, header_size);
        } else {
            memcpy(&packet_header, read_ptr, remaining_space);
            memcpy(reinterpret_cast<char*>(&packet_header) + remaining_space, handle->data_ptr, 
                   header_size - remaining_space);
        }
        
        // 检查数据包大小是否合理
        if (packet_header.data_size == 0 || packet_header.data_size > data_size) {
            LOG_ERROR("Invalid packet data size {} in batch read for '{}'", 
                     packet_header.data_size, handle->name);
            break;
        }
        
        // 复制包头信息
        packets[count].timestamp = packet_header.timestamp;
        packets[count].data_type = packet_header.data_type;
        packets[count].data_size = packet_header.data_size;
        
        // 读取实际数据
        size_t packet_size = header_size + packet_header.data_size;
        if (remaining_space >= packet_size) {
            // 一次性读取数据
            memcpy(packets[count].data, read_ptr + header_size, packet_header.data_size);
        } else {
            // 分段读取数据
            if (remaining_space > header_size) {
                size_t data_first_part = remaining_space - header_size;
                memcpy(packets[count].data, read_ptr + header_size, data_first_part);
                memcpy(packets[count].data + data_first_part, handle->data_ptr, 
                       packet_header.data_size - data_first_part);
            } else {
                size_t data_offset = header_size - remaining_space;
                memcpy(packets[count].data, static_cast<char*>(handle->data_ptr) + data_offset, 
                       packet_header.data_size);
            }
        }
        
        // 更新读指针
        handle->header->read_index.store(read_idx + packet_size);
        count++;
    }
    
    return count;
}

// 溢出检测和重置方法实现
bool SharedMemoryManager::_check_and_reset_overflow(SharedMemoryHandle* handle) {
    if (!handle) return false;
    
    uint64_t write_idx = handle->header->write_index.load();
    uint64_t read_idx = handle->header->read_index.load();
    
    // 检查是否需要重置
    if (!_is_overflow_risk(write_idx) && !_is_overflow_risk(read_idx)) {
        return true;  // 无风险，直接返回
    }
    
    // 检查是否已经在重置中
    if (handle->header->reset_in_progress.load()) {
        return _wait_for_reset_completion(handle);
    }
    
    // 尝试进行重置
    return _safe_reset_indices(handle);
}

bool SharedMemoryManager::_wait_for_reset_completion(SharedMemoryHandle* handle) {
    const int timeout_ms = 100;  // 100ms超时
    
    try {
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
            lock(handle->header->reset_mutex);
        
        auto deadline = boost::posix_time::microsec_clock::universal_time() + 
                       boost::posix_time::milliseconds(timeout_ms);
        
        while (handle->header->reset_in_progress.load()) {
            bool wait_result = handle->header->reset_condition.timed_wait(lock, deadline);
            
            if (!wait_result) {
                LOG_WARN("Timeout waiting for reset completion in '{}' ({}ms)", 
                           handle->name, timeout_ms);
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while waiting for reset in '{}': {}", handle->name, e.what());
        return false;
    }
}

bool SharedMemoryManager::_safe_reset_indices(SharedMemoryHandle* handle) {
    if (!handle) return false;
    
    // 设置重置标志
    bool expected = false;
    if (!handle->header->reset_in_progress.compare_exchange_strong(expected, true)) {
        // 其他进程/线程正在重置，等待完成
        return _wait_for_reset_completion(handle);
    }
    
    bool reset_success = false;
    
    try {
        // 获取主数据锁
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
            data_lock(handle->header->mutex);
        
        uint64_t write_idx = handle->header->write_index.load();
        uint64_t read_idx = handle->header->read_index.load();
        uint32_t data_size = handle->header->data_size.load();
        
        // 计算相对偏移
        uint64_t data_in_buffer = write_idx - read_idx;
        
        // 验证缓冲区数据量是否合理
        if (data_in_buffer > data_size) {
            LOG_ERROR("Invalid buffer state for '{}': data_in_buffer={}, data_size={}", 
                     handle->name, data_in_buffer, data_size);
            throw std::runtime_error("Invalid buffer state");
        }
        
        // 重置到安全点
        uint64_t new_read_idx = SAFE_RESET_POINT;
        uint64_t new_write_idx = new_read_idx + data_in_buffer;
        
        // 原子更新索引
        handle->header->read_index.store(new_read_idx);
        handle->header->write_index.store(new_write_idx);
        
        // 增加重置计数器
        handle->header->reset_counter.fetch_add(1);
        
        LOG_INFO("Reset overflow indices for '{}': read {} -> {}, write {} -> {}, reset_count={}", 
                 handle->name, read_idx, new_read_idx, write_idx, new_write_idx,
                 handle->header->reset_counter.load());
        
        reset_success = true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to reset indices for '{}': {}", handle->name, e.what());
        reset_success = false;
    }
    
    // 清除重置标志并通知所有等待者
    handle->header->reset_in_progress.store(false);
    
    try {
        // 获取重置锁并通知
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
            reset_lock(handle->header->reset_mutex);
        handle->header->reset_condition.notify_all();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to notify after reset for '{}': {}", handle->name, e.what());
    }
    
    return reset_success;
}

uint64_t SharedMemoryManager::_normalize_index(uint64_t index, uint32_t data_size) const {
    return index % data_size;
}

bool SharedMemoryManager::_is_overflow_risk(uint64_t index) const {
    return index > OVERFLOW_THRESHOLD;
}

bool SharedMemoryManager::get_overflow_stats(const std::string& name, uint64_t* reset_count, 
                                           bool* reset_in_progress) const {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        return false;
    }
    
    if (reset_count) {
        *reset_count = handle->header->reset_counter.load();
    }
    
    if (reset_in_progress) {
        *reset_in_progress = handle->header->reset_in_progress.load();
    }
    
    return true;
}

bool SharedMemoryManager::is_overflow_risk(const std::string& name) const {
    SharedMemoryHandle* handle = _get_handle(name);
    if (!handle) {
        return false;
    }
    
    uint64_t write_idx = handle->header->write_index.load();
    uint64_t read_idx = handle->header->read_index.load();
    
    return _is_overflow_risk(write_idx) || _is_overflow_risk(read_idx);
}

// 数据类型到共享内存后缀的映射 - 补充完整映射
const std::unordered_map<FTDataType, std::string> SharedMemoryManager::DATA_TYPE_SHM_NAMES = {
    // 行情数据类型
    {DATA_TYPE_INDEX, "_index"},           
    {DATA_TYPE_DEPTH, "_depth"},           
    {DATA_TYPE_DEPTH_FIFTY, "_depth50"},   
    {DATA_TYPE_ENTRUST, "_entrust"},       
    {DATA_TYPE_TRADE, "_trade"},           
    
    // 交易数据类型
    {DATA_TYPE_TRADE_ORDER_RTN, "_trade_order"},
    {DATA_TYPE_TRADE_FILL, "_trade_fill"},
    {DATA_TYPE_TRADE_CANCEL, "_trade_cancel"},
    {DATA_TYPE_TRADE_POSITION, "_trade_position"},
    {DATA_TYPE_TRADE_ACCOUNT, "_trade_account"},
    {DATA_TYPE_BANK_SECURITIES_TRANSFER, "_bank_transfer"},
    {DATA_TYPE_FUND_TRANSFER, "_fund_transfer"},
    
    // 交易响应类型
    {DATA_TYPE_ORDER_RSP, "_order_rsp"},
    {DATA_TYPE_CANCEL_RSP, "_cancel_rsp"},
    {DATA_TYPE_BANK_SECURITIES_TRANSFER_RSP, "_bank_transfer_rsp"},
    {DATA_TYPE_FUND_TRANSFER_RSP, "_fund_transfer_rsp"},
    
    // 系统数据类型
    {DATA_TYPE_SYSTEM_STATUS, "_system_status"},
    {DATA_TYPE_SYSTEM_LOG, "_system_log"},
    {DATA_TYPE_SYSTEM_CONFIG, "_system_config"}
};

std::string SharedMemoryManager::get_shm_name_for_type(const std::string& base_name, 
                                                        FTDataType data_type) const {
    auto it = DATA_TYPE_SHM_NAMES.find(data_type);
    if (it != DATA_TYPE_SHM_NAMES.end()) {
        return base_name + it->second;
    }
    return base_name + "_unknown";
}

bool SharedMemoryManager::create_typed_shared_memory(const std::string& base_name, 
                                                     FTDataType data_type, size_t size) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    return create_shared_memory(typed_name, size);
}

bool SharedMemoryManager::open_typed_shared_memory(const std::string& base_name, FTDataType data_type) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    return open_shared_memory(typed_name);
}

bool SharedMemoryManager::close_typed_shared_memory(const std::string& base_name, FTDataType data_type) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    return close_shared_memory(typed_name);
}

ssize_t SharedMemoryManager::write_typed_data(const std::string& base_name, FTDataType data_type,
                                              const void* data, size_t size) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    return write_data(typed_name, data_type, data, size);
}

ssize_t SharedMemoryManager::read_typed_data(const std::string& base_name, FTDataType data_type,
                                             void* buffer, size_t buffer_size) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    FTDataType read_type;
    return read_data(typed_name, &read_type, buffer, buffer_size);
}

int SharedMemoryManager::read_typed_batch(const std::string& base_name, FTDataType data_type,
                                          ShmDataPacket* packets, int max_count) {
    std::string typed_name = get_shm_name_for_type(base_name, data_type);
    return read_batch(typed_name, packets, max_count);
}

bool SharedMemoryManager::is_data_type_supported(FTDataType data_type) const {
    return DATA_TYPE_SHM_NAMES.find(data_type) != DATA_TYPE_SHM_NAMES.end();
}

std::vector<FTDataType> SharedMemoryManager::get_supported_data_types() const {
    std::vector<FTDataType> types;
    types.reserve(DATA_TYPE_SHM_NAMES.size());
    
    for (const auto& pair : DATA_TYPE_SHM_NAMES) {
        types.push_back(pair.first);
    }
    
    return types;
}
NS_FT_END