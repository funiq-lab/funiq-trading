#include "data/data_manager.h"
#include "logger/logger.hpp"
#include <cstring>
#include <sstream>
#include <functional>
#include <sys/stat.h>
#include <future>
#include <atomic>

NS_FT_BEGIN

// ===== TypedSharedMemoryDataSource 实现 =====

TypedSharedMemoryDataSource::TypedSharedMemoryDataSource(const std::string &name)
    : _name(name), _size_per_type(0), _is_creator(false), _stopped(false)
{
}

TypedSharedMemoryDataSource::~TypedSharedMemoryDataSource()
{
    if (!_stopped.load())
    {
        stop();
    }
    cleanup();
}

void TypedSharedMemoryDataSource::stop()
{
    LOG_INFO("TypedSharedMemoryDataSource {} stopping...", _name);

    // 先设置停止标志
    _stopped.store(true);

    // 等待所有正在进行的读写操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 获取锁进行清理
    std::unique_lock<std::shared_mutex> lock(_mutex);

    LOG_INFO("TypedSharedMemoryDataSource {} stopped", _name);
}

bool TypedSharedMemoryDataSource::is_stopped() const
{
    return _stopped.load();
}

bool TypedSharedMemoryDataSource::initialize(const std::string &config)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    // 解析配置 (格式: "base_shm_name:create_flag")
    std::istringstream iss(config);
    std::string base_name;
    bool create_if_not_exist = true;

    if (std::getline(iss, base_name, ':'))
    {
        std::string create_str;
        if (std::getline(iss, create_str))
        {
            create_if_not_exist = (create_str == "true" || create_str == "1");
        }
    }

    if (base_name.empty())
    {
        _set_error("Invalid shared memory configuration: " + config);
        return false;
    }

    _base_shm_name = base_name;
    _is_creator = create_if_not_exist;

    LOG_INFO("TypedSharedMemoryDataSource {} initialized with base name: {}", _name, base_name);
    return true;
}

void TypedSharedMemoryDataSource::cleanup()
{
    if (!_stopped.load())
    {
        LOG_WARN("TypedSharedMemoryDataSource {} cleanup called without stop", _name);
        stop();
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);
    _cleanup_typed_shared_memories();
    _base_shm_name.clear();
    _enabled_data_types.clear();
    _size_per_type = 0;

    LOG_INFO("TypedSharedMemoryDataSource {} cleanup completed", _name);
}

bool TypedSharedMemoryDataSource::is_available() const
{
    if (_stopped.load())
    {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || _base_shm_name.empty() || _enabled_data_types.empty())
    {
        return false;
    }

    auto &shm_manager = SharedMemoryManager::get_instance();

    // 检查至少一个数据类型的共享内存是否可用
    for (FTDataType data_type : _enabled_data_types)
    {
        std::string typed_name = shm_manager.get_shm_name_for_type(_base_shm_name, data_type);
        if (shm_manager.is_open(typed_name))
        {
            return true;
        }
    }

    return false;
}

bool TypedSharedMemoryDataSource::enable_data_types(const std::vector<FTDataType> &data_types, size_t size_per_type)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    if (data_types.empty() || size_per_type == 0)
    {
        _set_error("Invalid parameters for enable_data_types");
        return false;
    }

    if (_base_shm_name.empty())
    {
        _set_error("Base shared memory name not set");
        return false;
    }

    // 检查所有数据类型是否支持
    auto &shm_manager = SharedMemoryManager::get_instance();
    for (FTDataType data_type : data_types)
    {
        if (!shm_manager.is_data_type_supported(data_type))
        {
            _set_error("Unsupported data type: " + std::to_string(static_cast<int>(data_type)));
            return false;
        }
    }

    // 清理现有的数据类型
    _cleanup_typed_shared_memories();

    // 保存新配置
    _enabled_data_types.clear();
    for (FTDataType data_type : data_types)
    {
        _enabled_data_types.insert(data_type);
    }
    _size_per_type = size_per_type;

    // 初始化类型化共享内存
    if (!_initialize_typed_shared_memories())
    {
        _enabled_data_types.clear();
        _size_per_type = 0;
        return false;
    }

    LOG_INFO("TypedSharedMemoryDataSource {} enabled {} data types, {} bytes per type",
             _name, data_types.size(), size_per_type);

    return true;
}

ssize_t TypedSharedMemoryDataSource::write(FTDataType data_type, const void *data, size_t size)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    auto &shm_manager = SharedMemoryManager::get_instance();
    ssize_t written = shm_manager.write_typed_data(_base_shm_name, data_type, data, size);

    if (written <= 0)
    {
        _set_error("Failed to write typed data");
    }

    return written;
}

ssize_t TypedSharedMemoryDataSource::read(FTDataType data_type, void *buffer, size_t buffer_size)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    if (!buffer || buffer_size == 0)
    {
        _set_error("Invalid read parameters");
        return -1;
    }

    auto &shm_manager = SharedMemoryManager::get_instance();
    ssize_t read_bytes = shm_manager.read_typed_data(_base_shm_name, data_type, buffer, buffer_size);

    if (read_bytes < 0)
    {
        _set_error("Failed to read typed data");
        return -1;
    }

    return read_bytes;
}

int TypedSharedMemoryDataSource::read_batch(FTDataType data_type, ShmDataPacket *packets, int max_count)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    if (!packets || max_count <= 0)
    {
        _set_error("Invalid read_batch parameters");
        return -1;
    }

    auto &shm_manager = SharedMemoryManager::get_instance();
    int count = shm_manager.read_typed_batch(_base_shm_name, data_type, packets, max_count);

    if (count < 0)
    {
        _set_error("Failed to read typed batch data");
    }

    return count;
}

void TypedSharedMemoryDataSource::get_stats(FTDataType data_type, uint64_t *total_written,
                                            uint64_t *total_read, uint64_t *available_space) const
{
    if (_stopped.load())
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
        return;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || !_is_data_type_enabled(data_type))
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
        return;
    }

    auto &shm_manager = SharedMemoryManager::get_instance();
    std::string typed_name = shm_manager.get_shm_name_for_type(_base_shm_name, data_type);

    shm_manager.get_stats(typed_name, total_written, total_read, available_space);
}

void TypedSharedMemoryDataSource::reset_stats()
{
    // 对于共享内存，统计信息在每个共享内存对象中维护
    // 这里可以添加重置逻辑，或者留空
}

std::vector<FTDataType> TypedSharedMemoryDataSource::get_enabled_data_types() const
{
    if (_stopped.load())
    {
        return {};
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    std::vector<FTDataType> types;
    types.reserve(_enabled_data_types.size());

    for (FTDataType data_type : _enabled_data_types)
    {
        types.push_back(data_type);
    }

    return types;
}

// 内部辅助方法
bool TypedSharedMemoryDataSource::_is_data_type_enabled(FTDataType data_type) const
{
    return _enabled_data_types.find(data_type) != _enabled_data_types.end();
}

bool TypedSharedMemoryDataSource::_initialize_typed_shared_memories()
{
    auto &shm_manager = SharedMemoryManager::get_instance();

    // 为每种数据类型创建共享内存
    for (FTDataType data_type : _enabled_data_types)
    {
        if (_stopped.load())
        {
            LOG_WARN("TypedSharedMemoryDataSource {} stopped during initialization", _name);
            return false;
        }

        bool success = false;

        if (_is_creator)
        {
            success = shm_manager.create_typed_shared_memory(_base_shm_name, data_type, _size_per_type);
            if (!success)
            {
                // 创建失败，尝试打开
                success = shm_manager.open_typed_shared_memory(_base_shm_name, data_type);
            }
        }
        else
        {
            success = shm_manager.open_typed_shared_memory(_base_shm_name, data_type);
        }

        if (!success)
        {
            _set_error("Failed to initialize typed shared memory for data type: " +
                       std::to_string(static_cast<int>(data_type)));

            // 清理已创建的共享内存
            _cleanup_typed_shared_memories();
            return false;
        }
    }

    return true;
}

void TypedSharedMemoryDataSource::_cleanup_typed_shared_memories()
{
    auto &shm_manager = SharedMemoryManager::get_instance();

    for (FTDataType data_type : _enabled_data_types)
    {
        bool closed = shm_manager.close_typed_shared_memory(_base_shm_name, data_type);

        if (!closed)
        {
            LOG_WARN("TypedSharedMemoryDataSource {} failed to close shared memory for data type: {}",
                     _name, static_cast<int>(data_type));
        }
    }
}

void TypedSharedMemoryDataSource::_set_error(const std::string &error)
{
    _last_error = error;
}

// ===== TypedFileDataSource =====

TypedFileDataSource::TypedFileDataSource(const std::string &name)
    : _name(name), _append_mode(true), _auto_flush(true), _stopped(false)
{
}

TypedFileDataSource::~TypedFileDataSource()
{
    // 简化析构逻辑，避免死锁
    if (!_stopped.load())
    {
        stop();
    }

    // 使用超时清理，避免死锁
    auto cleanup_future = std::async(std::launch::async, [this]()
                                     {
        try {
            _cleanup_typed_files();
            return true;
        } catch (...) {
            return false;
        } });

    auto status = cleanup_future.wait_for(std::chrono::milliseconds(500));
    if (status == std::future_status::timeout)
    {
        LOG_WARN("TypedFileDataSource {} cleanup timeout during destruction", _name);
    }

    _base_file_path.clear();
    _enabled_data_types.clear();
}

void TypedFileDataSource::stop()
{
    LOG_INFO("TypedFileDataSource {} stopping...", _name);

    // 先设置停止标志
    _stopped.store(true);

    // 等待所有正在进行的读写操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 获取锁进行清理
    std::unique_lock<std::shared_mutex> lock(_mutex);

    LOG_INFO("TypedFileDataSource {} stopped", _name);
}

bool TypedFileDataSource::is_stopped() const
{
    return _stopped.load();
}

bool TypedFileDataSource::initialize(const std::string &config)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    // 解析配置 (格式: "base_file_path:append_mode:auto_flush")
    std::istringstream iss(config);
    std::string base_path;
    std::string append_str = "true";
    std::string flush_str = "true";

    if (std::getline(iss, base_path, ':'))
    {
        if (std::getline(iss, append_str, ':'))
        {
            std::getline(iss, flush_str);
        }
    }

    if (base_path.empty())
    {
        _set_error("Invalid file configuration: " + config);
        return false;
    }

    _base_file_path = base_path;
    _append_mode = (append_str != "false" && append_str != "0");
    _auto_flush = (flush_str != "false" && flush_str != "0");

    // 确保基础目录存在
    std::string parent_dir = get_parent_path(_base_file_path);
    if (!create_directories(parent_dir))
    {
        _set_error("Failed to create directory: " + parent_dir);
        return false;
    }

    LOG_INFO("TypedFileDataSource {} initialized with base path: {}, append: {}, auto_flush: {}",
             _name, base_path, _append_mode, _auto_flush);
    return true;
}

void TypedFileDataSource::cleanup()
{
    // 避免重复获取锁
    if (!_stopped.load())
    {
        LOG_WARN("TypedFileDataSource {} cleanup called without stop", _name);
        _stopped.store(true); // 直接设置标志，不调用stop()
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);
    _cleanup_typed_files();
    _base_file_path.clear();
    _enabled_data_types.clear();

    LOG_INFO("TypedFileDataSource {} cleanup completed", _name);
}

bool TypedFileDataSource::is_available() const
{
    if (_stopped.load())
    {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || _base_file_path.empty() || _enabled_data_types.empty())
    {
        return false;
    }

    // 检查至少一个数据类型的写入流是否可用
    for (FTDataType data_type : _enabled_data_types)
    {
        auto it = _write_streams.find(data_type);
        if (it != _write_streams.end() && it->second && it->second->is_open())
        {
            return true;
        }
    }

    return false;
}

bool TypedFileDataSource::enable_data_types(const std::vector<FTDataType> &data_types, size_t size_per_type)
{
    (void)size_per_type;
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    if (data_types.empty())
    {
        LOG_ERROR("No data types provided");
        _set_error("Invalid parameters for enable_data_types");
        return false;
    }

    if (_base_file_path.empty())
    {
        LOG_ERROR("Base file path not set");
        _set_error("Base file path not set");
        return false;
    }

    _cleanup_typed_files();

    // 保存新配置
    _enabled_data_types.clear();
    for (FTDataType data_type : data_types)
    {
        _enabled_data_types.insert(data_type);
    }

    // 初始化类型化文件
    if (!_initialize_typed_files())
    {
        LOG_ERROR("Failed to initialize typed files");
        _enabled_data_types.clear();
        return false;
    }

    LOG_INFO("TypedFileDataSource {} enabled {} data types",
             _name, data_types.size());

    return true;
}

ssize_t TypedFileDataSource::write(FTDataType data_type, const void *data, size_t size)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        LOG_ERROR("Data type {} not enabled for writing", static_cast<int>(data_type));
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    if (!data || size == 0)
    {
        LOG_ERROR("Invalid write parameters - data: {}, size: {}", (void *)data, size);
        _set_error("Invalid write parameters");
        return -1;
    }

    // 构建完整记录（原子写入）
    RecordHeader header;
    header.magic_start = RECORD_MAGIC_START;
    header.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count());
    header.data_type = data_type;
    header.data_size = static_cast<uint32_t>(size);
    header.checksum = _calculate_checksum(data, size);
    header.magic_end = RECORD_MAGIC_END;

    // 构建完整的记录缓冲区
    std::vector<uint8_t> record_buffer;
    record_buffer.reserve(sizeof(RecordHeader) + size);

    // 添加记录头
    record_buffer.insert(record_buffer.end(),
                         reinterpret_cast<const uint8_t *>(&header),
                         reinterpret_cast<const uint8_t *>(&header) + sizeof(header));

    // 添加数据
    record_buffer.insert(record_buffer.end(),
                         static_cast<const uint8_t *>(data),
                         static_cast<const uint8_t *>(data) + size);

    // 原子写入整个记录
    if (!_atomic_write_record(data_type, record_buffer.data(), record_buffer.size()))
    {
        LOG_ERROR("Failed to write atomic record for data_type: {}", static_cast<int>(data_type));
        return -1;
    }

    return static_cast<ssize_t>(record_buffer.size());
}

bool TypedFileDataSource::_atomic_write_record(FTDataType data_type, const void *record_data, size_t record_size)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return false;
    }

    auto it = _write_streams.find(data_type);
    if (it == _write_streams.end() || !it->second || !it->second->is_open())
    {
        LOG_ERROR("Write stream not available for data_type: {}", static_cast<int>(data_type));
        _set_error("Write stream not available for data type: " + std::to_string(static_cast<int>(data_type)));
        return false;
    }

    // 确保流状态良好
    if (!it->second->good())
    {
        it->second->clear();
        if (_append_mode)
        {
            it->second->seekp(0, std::ios::end);
        }

        if (!it->second->good())
        {
            LOG_ERROR("Unable to fix write stream state for data_type: {}", static_cast<int>(data_type));
            _set_error("Write stream in bad state for data type: " + std::to_string(static_cast<int>(data_type)));
            return false;
        }
    }

    // 获取写入前的文件位置
    std::streampos pos_before = it->second->tellp();

    // 单次原子写入整个记录
    it->second->write(static_cast<const char *>(record_data), record_size);

    // 获取写入后的文件位置
    std::streampos pos_after = it->second->tellp();

    if (it->second->fail())
    {
        LOG_ERROR("Atomic write failed for data_type: {}, errno: {}, error: {}",
                  static_cast<int>(data_type), errno, strerror(errno));
        _set_error("Atomic write failed for data type: " + std::to_string(static_cast<int>(data_type)));
        return false;
    }

    // 验证写入的字节数
    ssize_t bytes_written = static_cast<ssize_t>(pos_after - pos_before);
    if (bytes_written != static_cast<ssize_t>(record_size))
    {
        LOG_ERROR("Partial write detected - expected: {}, actual: {}", record_size, bytes_written);
        _set_error("Partial write detected for data type: " + std::to_string(static_cast<int>(data_type)));
        return false;
    }

    // 强制刷新以确保数据立即可读
    if (!_stopped.load())
    {
        it->second->flush();
        if (it->second->fail())
        {
            LOG_WARN("Flush failed but write was successful");
            it->second->clear();
        }
    }

    return true;
}

ssize_t TypedFileDataSource::read(FTDataType data_type, void *buffer, size_t buffer_size)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        LOG_ERROR("Data type {} not enabled for reading", static_cast<int>(data_type));
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    if (!buffer || buffer_size == 0)
    {
        LOG_ERROR("Invalid read parameters - buffer: {}, buffer_size: {}", (void *)buffer, buffer_size);
        _set_error("Invalid read parameters");
        return -1;
    }

    ssize_t bytes_read = 0;
    if (!_read_binary_data(data_type, buffer, buffer_size, &bytes_read))
    {
        LOG_ERROR("Failed to read binary data for data_type: {}", static_cast<int>(data_type));
        return -1;
    }

    return bytes_read;
}

int TypedFileDataSource::read_batch(FTDataType data_type, ShmDataPacket *packets, int max_count)
{
    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        _set_error("Data source is stopped");
        return -1;
    }

    if (!_is_data_type_enabled(data_type))
    {
        LOG_ERROR("Data type {} not enabled for reading", static_cast<int>(data_type));
        _set_error("Data type not enabled: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    if (!packets || max_count <= 0)
    {
        LOG_ERROR("Invalid read_batch parameters - packets: {}, max_count: {}", (void *)packets, max_count);
        _set_error("Invalid read_batch parameters");
        return -1;
    }

    int result = _read_binary_batch(data_type, packets, max_count);
    return result;
}

// 改进读取流管理，支持动态文件大小变化
std::ifstream *TypedFileDataSource::_get_read_stream(FTDataType data_type) const
{
    if (_stopped.load())
    {
        return nullptr;
    }

    auto it = _read_streams.find(data_type);
    if (it != _read_streams.end() && it->second && it->second->is_open())
    {
        // 检查是否需要重新打开流（文件可能有新数据）
        if (_should_refresh_read_stream(it->second.get(), data_type))
        {
            it->second->close();
            it->second.reset();
            _read_streams.erase(it);
        }
        else
        {
            return it->second.get();
        }
    }

    // 创建新的读取流
    std::string file_path = _get_file_path_for_type(data_type);

    // 检查文件是否存在
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0)
    {
        return nullptr; // 文件不存在，返回nullptr而不是错误
    }


    auto read_stream = std::make_unique<std::ifstream>();
    read_stream->open(file_path, std::ios::binary);

    if (!read_stream->is_open())
    {
        LOG_ERROR("Failed to open read stream: {}, errno: {}, error: {}", file_path, errno, strerror(errno));
        return nullptr;
    }

    std::ifstream *stream_ptr = read_stream.get();
    _read_streams[data_type] = std::move(read_stream);

    return stream_ptr;
}

// 检查是否需要刷新读取流
bool TypedFileDataSource::_should_refresh_read_stream(std::ifstream *stream, FTDataType data_type) const
{
    if (!stream || !stream->is_open())
    {
        return true;
    }

    // 检查流是否在EOF状态，如果是，检查文件是否有新数据
    if (stream->eof())
    {
        std::string file_path = _get_file_path_for_type(data_type);
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0)
        {
            // 获取当前读取位置
            std::streampos current_pos = stream->tellg();
            if (current_pos >= 0 && static_cast<off_t>(current_pos) < st.st_size)
            {
                // 文件有新数据，需要刷新流
                return true;
            }
        }
        else
        {
            // 没有新数据
            return false;
        }
    }

    return false;
}

// 改进读取方法，支持动态等待新数据
bool TypedFileDataSource::_read_binary_data(FTDataType data_type, void *buffer, size_t buffer_size, ssize_t *bytes_read) const
{
    if (_stopped.load())
    {
        *bytes_read = 0;
        return true;
    }

    std::ifstream *stream = _get_read_stream(data_type);
    if (!stream)
    {
        // 文件不存在或无法打开，不是错误，只是暂时没有数据
        *bytes_read = 0;
        return true;
    }

    // 如果流在EOF状态，尝试刷新
    if (stream->eof())
    {
        stream->clear(); // 清除EOF标志

        // 检查文件是否有新数据
        std::string file_path = _get_file_path_for_type(data_type);
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0)
        {
            std::streampos current_pos = stream->tellg();
            if (current_pos < 0 || static_cast<off_t>(current_pos) >= st.st_size)
            {
                // 没有新数据
                *bytes_read = 0;
                return true;
            }
        }
        else
        {
            *bytes_read = 0;
            return true;
        }
    }

    // 读取记录头
    RecordHeader header;
    std::streampos record_start = stream->tellg();

    stream->read(reinterpret_cast<char *>(&header), sizeof(header));
    if (stream->gcount() != sizeof(header))
    {
        if (stream->eof())
        {
            *bytes_read = 0;
            return true; // 文件结尾，正常情况
        }
        else
        {
            LOG_ERROR("Failed to read record header");
            _set_error("Failed to read record header");
            return false;
        }
    }

    // 复制packed字段到局部变量
    uint32_t magic_start = header.magic_start;
    uint32_t magic_end = header.magic_end;
    uint32_t data_size = header.data_size;
    uint32_t checksum = header.checksum;

    // 验证记录完整性
    if (magic_start != RECORD_MAGIC_START || magic_end != RECORD_MAGIC_END)
    {
        LOG_WARN("Invalid record magic numbers at position {}", static_cast<long>(record_start));

        // 尝试寻找下一个有效记录
        if (!_seek_to_next_valid_record(stream, data_type))
        {
            *bytes_read = 0;
            return true;
        }
        // 递归调用尝试读取下一个有效记录
        return _read_binary_data(data_type, buffer, buffer_size, bytes_read);
    }

    // 验证数据类型
    if (header.data_type != data_type)
    {
        LOG_WARN("Data type mismatch - expected: {}, got: {}",
                 static_cast<int>(data_type), static_cast<int>(header.data_type));
        // 跳过这条记录
        stream->seekg(record_start + static_cast<std::streamoff>(sizeof(header) + data_size));
        return _read_binary_data(data_type, buffer, buffer_size, bytes_read);
    }

    // 检查缓冲区大小
    if (buffer_size < data_size)
    {
        LOG_ERROR("Buffer size {} insufficient for data size {}", buffer_size, data_size);
        _set_error("Buffer size insufficient for data");
        return false;
    }

    // 读取实际数据
    stream->read(static_cast<char *>(buffer), data_size);
    if (stream->gcount() != static_cast<std::streamsize>(data_size))
    {
        LOG_WARN("Incomplete data read, expected: {}, got: {}",
                 data_size, stream->gcount());
        *bytes_read = 0;
        return true;
    }

    // 验证数据校验和
    uint32_t calculated_checksum = _calculate_checksum(buffer, data_size);
    if (calculated_checksum != checksum)
    {
        LOG_WARN("Checksum mismatch - expected: 0x{:X}, calculated: 0x{:X}",
                 checksum, calculated_checksum);
        // 数据损坏，跳过这条记录
        return _read_binary_data(data_type, buffer, buffer_size, bytes_read);
    }

    *bytes_read = static_cast<ssize_t>(data_size);
    return true;
}

// 改进清理方法，避免长时间阻塞
void TypedFileDataSource::_cleanup_typed_files()
{
    // 清理写入流 - 使用更短的超时
    for (auto &pair : _write_streams)
    {
        if (pair.second && pair.second->is_open())
        {
            try
            {
                // 快速flush或直接关闭
                if (!_stopped.load() && _auto_flush)
                {
                    auto flush_future = std::async(std::launch::async, [&pair]()
                                                   {
                        try {
                            pair.second->flush();
                            return true;
                        } catch (...) {
                            return false;
                        } });

                    // 只等待100ms
                    auto status = flush_future.wait_for(std::chrono::milliseconds(100));
                    if (status == std::future_status::timeout)
                    {
                        LOG_WARN("Quick flush timeout for data_type: {}", static_cast<int>(pair.first));
                    }
                }

                pair.second->close();
            }
            catch (...)
            {
                LOG_ERROR("Exception while closing write stream for data_type: {}", static_cast<int>(pair.first));
            }
        }
    }
    _write_streams.clear();

    // 清理读取流
    for (auto &pair : _read_streams)
    {
        if (pair.second && pair.second->is_open())
        {
            try
            {
                pair.second->close();
            }
            catch (...)
            {
                LOG_ERROR("Exception while closing read stream for data_type: {}", static_cast<int>(pair.first));
            }
        }
    }
    _read_streams.clear();
}

// 寻找下一个有效记录
bool TypedFileDataSource::_seek_to_next_valid_record(std::ifstream *stream, FTDataType data_type) const
{
    if (_stopped.load())
    {
        return false;
    }

    const size_t SEARCH_BUFFER_SIZE = 1024;
    char buffer[SEARCH_BUFFER_SIZE];

    while (!stream->eof() && !_stopped.load())
    {
        std::streampos current_pos = stream->tellg();
        stream->read(buffer, SEARCH_BUFFER_SIZE);
        std::streamsize bytes_read = stream->gcount();

        if (bytes_read == 0)
        {
            break;
        }

        // 在缓冲区中搜索魔数
        std::streamsize search_limit = bytes_read - static_cast<std::streamsize>(sizeof(uint32_t));
        for (std::streamsize i = 0; i <= search_limit; ++i)
        {
            if (_stopped.load())
            {
                return false;
            }

            uint32_t *magic_ptr = reinterpret_cast<uint32_t *>(buffer + i);
            if (*magic_ptr == RECORD_MAGIC_START)
            {
                // 找到可能的记录开始，定位到该位置
                std::streampos found_pos = current_pos + static_cast<std::streamoff>(i);
                stream->seekg(found_pos);
                return true;
            }
        }

        // 回退一些字节以防魔数跨越缓冲区边界
        if (!stream->eof() && bytes_read >= static_cast<std::streamsize>(sizeof(uint32_t)))
        {
            std::streamoff backtrack_offset = static_cast<std::streamoff>(bytes_read - sizeof(uint32_t));
            stream->seekg(current_pos + backtrack_offset);
        }
    }

    return false;
}

// 简单的校验和计算
uint32_t TypedFileDataSource::_calculate_checksum(const void *data, size_t size) const
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t checksum = 0;

    for (size_t i = 0; i < size; ++i)
    {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ bytes[i];
    }

    return checksum;
}
void TypedFileDataSource::get_stats(FTDataType data_type, uint64_t *total_written,
                                    uint64_t *total_read, uint64_t *available_space) const
{

    if (_stopped.load())
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
        return;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || !_is_data_type_enabled(data_type))
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
        return;
    }

    std::string file_path = _get_file_path_for_type(data_type);

    // 获取文件大小作为写入统计
    struct stat st;
    if (stat(file_path.c_str(), &st) == 0)
    {
        if (total_written)
            *total_written = static_cast<uint64_t>(st.st_size);
    }
    else
    {
        if (total_written)
            *total_written = 0;
    }

    // 文件系统没有读取统计，设为0
    if (total_read)
        *total_read = 0;

    // 可用空间 - 简单返回一个大值
    if (available_space)
        *available_space = UINT64_MAX;
}

void TypedFileDataSource::reset_stats()
{
    // 文件数据源的统计信息基于文件系统，无法重置
    LOG_INFO("TypedFileDataSource {} reset_stats called (no-op for file source)", _name);
}

std::vector<FTDataType> TypedFileDataSource::get_enabled_data_types() const
{
    if (_stopped.load())
    {
        return {};
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    std::vector<FTDataType> types;
    types.reserve(_enabled_data_types.size());

    for (FTDataType data_type : _enabled_data_types)
    {
        types.push_back(data_type);
    }

    return types;
}

// 内部辅助方法实现
bool TypedFileDataSource::_is_data_type_enabled(FTDataType data_type) const
{
    return _enabled_data_types.find(data_type) != _enabled_data_types.end();
}

std::string TypedFileDataSource::_get_file_path_for_type(FTDataType data_type) const
{
    // 数据类型到文件后缀的映射
    static const std::unordered_map<FTDataType, std::string> type_suffixes = {
        // 行情数据类型
        {DATA_TYPE_INDEX, "_index.dat"},
        {DATA_TYPE_DEPTH, "_depth.dat"},
        {DATA_TYPE_DEPTH_FIFTY, "_depth50.dat"},
        {DATA_TYPE_ENTRUST, "_entrust.dat"},
        {DATA_TYPE_TRADE, "_trade.dat"},

        // 交易数据类型
        {DATA_TYPE_TRADE_ORDER_RTN, "_trade_order.dat"},
        {DATA_TYPE_TRADE_FILL, "_trade_fill.dat"},
        {DATA_TYPE_TRADE_CANCEL, "_trade_cancel.dat"},
        {DATA_TYPE_TRADE_POSITION, "_trade_position.dat"},
        {DATA_TYPE_TRADE_ACCOUNT, "_trade_account.dat"},
        {DATA_TYPE_BANK_SECURITIES_TRANSFER, "_bank_transfer.dat"},
        {DATA_TYPE_FUND_TRANSFER, "_fund_transfer.dat"},

        // 交易响应类型
        {DATA_TYPE_ORDER_RSP, "_order_rsp.dat"},
        {DATA_TYPE_CANCEL_RSP, "_cancel_rsp.dat"},
        {DATA_TYPE_BANK_SECURITIES_TRANSFER_RSP, "_bank_transfer_rsp.dat"},
        {DATA_TYPE_FUND_TRANSFER_RSP, "_fund_transfer_rsp.dat"},

        // 系统数据类型
        {DATA_TYPE_SYSTEM_STATUS, "_system_status.dat"},
        {DATA_TYPE_SYSTEM_LOG, "_system_log.dat"},
        {DATA_TYPE_SYSTEM_CONFIG, "_system_config.dat"},
    };

    auto it = type_suffixes.find(data_type);
    std::string file_path;
    if (it != type_suffixes.end())
    {
        file_path = _base_file_path + it->second;
    }
    else
    {
        // 未知数据类型，使用通用后缀
        file_path = _base_file_path + "_type" + std::to_string(static_cast<int>(data_type)) + ".dat";
    }

    return file_path;
}

bool TypedFileDataSource::_initialize_typed_files()
{
    for (FTDataType data_type : _enabled_data_types)
    {
        if (_stopped.load())
        {
            LOG_WARN("TypedFileDataSource {} stopped during initialization", _name);
            return false;
        }

        std::string file_path = _get_file_path_for_type(data_type);

        // 确保文件目录存在
        std::string parent_dir = get_parent_path(file_path);

        if (!create_directories(parent_dir))
        {
            LOG_ERROR("Failed to create directory: {}", parent_dir);
            _set_error("Failed to create directory for file: " + file_path);
            _cleanup_typed_files();
            return false;
        }

        // 初始化写入流
        auto write_stream = std::make_unique<std::ofstream>();

        std::ios::openmode mode = std::ios::binary;
        if (_append_mode)
        {
            mode |= std::ios::app;
        }
        else
        {
            mode |= std::ios::trunc;
        }

        write_stream->open(file_path, mode);

        if (!write_stream->is_open())
        {
            LOG_ERROR("Failed to open write stream: {}, errno: {}, error: {}",
                      file_path, errno, strerror(errno));
            _set_error("Failed to open write stream: " + file_path + " (" + strerror(errno) + ")");
            _cleanup_typed_files();
            return false;
        }

        _write_streams[data_type] = std::move(write_stream);

        LOG_INFO("TypedFileDataSource {} opened write stream for data type {}: {}",
                 _name, static_cast<int>(data_type), file_path);
    }

    return true;
}

int TypedFileDataSource::_read_binary_batch(FTDataType data_type, ShmDataPacket *packets, int max_count) const
{
    if (_stopped.load())
    {
        return 0; // 停止状态返回0，表示没有读取到数据，但不是错误
    }

    std::ifstream *stream = _get_read_stream(data_type);
    if (!stream)
    {
        LOG_ERROR("Failed to get read stream for data_type: {}", static_cast<int>(data_type));
        _set_error("Read stream not available for data type: " + std::to_string(static_cast<int>(data_type)));
        return -1;
    }

    int count = 0;
    int skipped_records = 0;

    while (count < max_count && !_stopped.load())
    {
        // 读取记录头
        RecordHeader header;
        std::streampos record_start = stream->tellg();

        stream->read(reinterpret_cast<char *>(&header), sizeof(header));

        if (stream->gcount() != sizeof(header))
        {
            break;
        }

        uint32_t header_magic_start = header.magic_start;
        uint32_t header_magic_end = header.magic_end;
        int header_data_type = static_cast<int>(header.data_type);
        uint32_t header_data_size = header.data_size;

        // 验证记录完整性
        if (header.magic_start != RECORD_MAGIC_START ||
            header.magic_end != RECORD_MAGIC_END)
        {
            LOG_WARN("Invalid record magic numbers at position {}, expected start: 0x{:X}, end: 0x{:X}, got start: 0x{:X}, end: 0x{:X}",
                     static_cast<long>(record_start), RECORD_MAGIC_START, RECORD_MAGIC_END,
                     header_magic_start, header_magic_end);

            skipped_records++;

            // 尝试寻找下一个有效记录
            if (!_seek_to_next_valid_record(stream, data_type))
            {
                break;
            }
            continue;
        }

        // 验证数据类型和大小
        if (header.data_type != data_type)
        {
            LOG_WARN("Data type mismatch - expected: {}, got: {}",
                     static_cast<int>(data_type), header_data_type);
            continue;
        }

        if (header.data_size == 0 || header.data_size > 4096)
        {
            LOG_WARN("Invalid data size: {}", header_data_size);
            continue;
        }

        // 检查是否在读取过程中被停止
        if (_stopped.load())
        {
            break;
        }

        // 读取实际数据
        stream->read(packets[count].data, header.data_size);
        if (stream->gcount() != static_cast<std::streamsize>(header.data_size))
        {
            LOG_WARN("Incomplete data read, expected: {}, got: {}",
                     header_data_size, stream->gcount());
            break;
        }

        // 验证数据校验和
        uint32_t calculated_checksum = _calculate_checksum(packets[count].data, header.data_size);
        uint32_t header_checksum = header.checksum;
        if (calculated_checksum != header.checksum)
        {
            LOG_WARN("Checksum mismatch - expected: 0x{:X}, calculated: 0x{:X}, data may be corrupted",
                     header_checksum, calculated_checksum);
            skipped_records++;
            continue; // 跳过损坏的记录
        }

        // 填充数据包
        packets[count].timestamp = header.timestamp;
        packets[count].data_type = header.data_type;
        packets[count].data_size = header.data_size;

        count++;
        uint64_t header_timestamp = header.timestamp;
    }

    if (skipped_records > 0)
    {
        LOG_WARN("Skipped {} corrupted records while reading data_type: {}",
                 skipped_records, static_cast<int>(data_type));
    }

    return count;
}

void TypedFileDataSource::_set_error(const std::string &error) const
{
    _last_error = error;
}

// ===== DataSourceFactory 实现 =====

std::unordered_map<DataSourceType, std::function<DataSourcePtr(const std::string &)>> DataSourceFactory::_creators;
std::shared_mutex DataSourceFactory::_creators_mutex;

DataSourcePtr DataSourceFactory::create_data_source(DataSourceType type, const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(_creators_mutex);

    // 内置数据源类型
    switch (type)
    {
    case DATA_SOURCE_SHARED_MEMORY:
        return std::make_shared<TypedSharedMemoryDataSource>(name);
    case DATA_SOURCE_FILE:
        return std::make_shared<TypedFileDataSource>(name);
    default:
        break;
    }

    // 查找注册的自定义数据源
    auto it = _creators.find(type);
    if (it != _creators.end())
    {
        return it->second(name);
    }

    LOG_ERROR("Unknown data source type: {}", static_cast<int>(type));
    return nullptr;
}

bool DataSourceFactory::register_data_source_creator(DataSourceType type,
                                                     std::function<DataSourcePtr(const std::string &)> creator)
{
    std::unique_lock<std::shared_mutex> lock(_creators_mutex);
    _creators[type] = creator;
    return true;
}

// ===== DataManager 实现 =====

DataManager::DataManager(const std::string &name)
    : _name(name), _stopped(false)
{
    LOG_INFO("DataManager {} created", _name);
}

DataManager::~DataManager()
{
    if (!_stopped.load())
    {
        stop();
    }

    // 使用超时机制释放数据源
    auto release_future = std::async(std::launch::async, [this]()
                                     { return this->release_data_source(); });

    // 等待最多1秒
    auto status = release_future.wait_for(std::chrono::seconds(1));
    if (status == std::future_status::timeout)
    {
        LOG_WARN("DataManager {} release timeout, forcing destruction", _name);
        // 强制重置，不等待清理完成
        std::unique_lock<std::shared_mutex> lock(_mutex);
        if (_current_source)
        {
            _current_source.reset();
        }
    }
    else
    {
        bool success = release_future.get();
    }

    LOG_INFO("DataManager {} destroyed", _name);
}

void DataManager::stop()
{
    LOG_INFO("DataManager {} stopping...", _name);

    // 先设置停止标志
    _stopped.store(true);

    // 等待所有正在进行的读写操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 停止当前数据源
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (_current_source)
    {
        _current_source->stop();
    }

    LOG_INFO("DataManager {} stopped", _name);
}

bool DataManager::is_stopped() const
{
    return _stopped.load();
}

bool DataManager::register_data_source(const DataConfig &config, const std::vector<FTDataType> &data_types, size_t size_per_type)
{
    if (_stopped.load())
    {
        // 停止状态下不能注册新的数据源，但这不是错误，只是忽略
        LOG_INFO("DataManager {} is stopped, ignoring register_data_source request", _name);
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load())
    {
        LOG_INFO("DataManager {} is stopped, ignoring register_data_source request", _name);
        return false;
    }

    // 先释放当前数据源
    if (_current_source)
    {
        _current_source->stop();
        _current_source->cleanup();
        _current_source.reset();
    }

    // 根据配置确定数据源类型和参数
    DataSourceType source_type;
    std::string source_name;
    std::string source_config;

    switch (config.output_type)
    {
    case DATA_OUTPUT_SHARED_MEMORY:
        source_type = DATA_SOURCE_SHARED_MEMORY;
        source_name = strlen(config.shm_name) > 0 ? config.shm_name : "quote_shm_default";
        source_config = source_name + ":true"; // base_name:create_flag
        break;

    case DATA_OUTPUT_FILE:
    {
        source_type = DATA_SOURCE_FILE;
        source_name = strlen(config.file_path) > 0 ? config.file_path : "./quote_data";
        source_config = source_name + ":true:true"; // base_path:append:auto_flush
        break;
    }

    case DATA_OUTPUT_NONE:
    default:
        _set_error("Invalid or unsupported data output type: " + std::to_string(static_cast<int>(config.output_type)));
        return false;
    }

    // 创建并初始化数据源
    auto data_source = DataSourceFactory::create_data_source(source_type, source_name);
    if (!data_source)
    {
        _set_error("Failed to create data source: " + source_name);
        return false;
    }

    if (!data_source->initialize(source_config))
    {
        _set_error("Failed to initialize data source: " + data_source->get_last_error());
        return false;
    }

    // 启用数据类型
    if (!data_source->enable_data_types(data_types, size_per_type))
    {
        _set_error("Failed to enable data types: " + data_source->get_last_error());
        return false;
    }

    _current_source = data_source;

    LOG_INFO("DataManager {} registered data source: {} (type: {}) with {} data types",
             _name, source_name, static_cast<int>(source_type), data_types.size());
    return true;
}

bool DataManager::release_data_source()
{
    // 先停止数据源（如果还没停止）
    if (!_stopped.load())
    {
        stop();
    }

    // 等待一小段时间确保所有操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 使用超时机制进行清理
    auto cleanup_future = std::async(std::launch::async, [this]()
                                     {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        
        if (_current_source) {
            _current_source->cleanup();
            _current_source.reset();
            return true;
        }
        return false; });

    // 等待最多3秒
    auto status = cleanup_future.wait_for(std::chrono::seconds(3));
    if (status == std::future_status::timeout)
    {
        LOG_WARN("DataManager {} cleanup timeout, forcing release", _name);
        // 强制重置
        std::unique_lock<std::shared_mutex> lock(_mutex);
        if (_current_source)
        {
            _current_source.reset();
        }
        LOG_INFO("DataManager {} forced release completed", _name);
        return false;
    }

    bool success = cleanup_future.get();
    LOG_INFO("DataManager {} release {}", _name, success ? "successful" : "failed");
    return success;
}

DataSourceType DataManager::get_current_source_type() const
{
    if (_stopped.load())
    {
        return static_cast<DataSourceType>(-1);
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_current_source)
    {
        return _current_source->get_type();
    }

    return static_cast<DataSourceType>(-1);
}

ssize_t DataManager::write(FTDataType data_type, const void *data, size_t size)
{
    if (_stopped.load())
    {
        return 0; // 停止状态返回0，表示没有写入，但不是错误
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || !_current_source)
    {
        return 0; // 停止状态或无数据源时返回0
    }

    ssize_t result = _current_source->write(data_type, data, size);
    if (result < 0)
    {
        _set_error(_current_source->get_last_error());
    }

    return result;
}

ssize_t DataManager::read(FTDataType data_type, void *buffer, size_t buffer_size)
{
    if (_stopped.load())
    {
        return 0; // 停止状态返回0，表示没有读取到数据
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || !_current_source)
    {
        return 0; // 停止状态或无数据源时返回0
    }

    ssize_t result = _current_source->read(data_type, buffer, buffer_size);
    if (result < 0)
    {
        _set_error(_current_source->get_last_error());
    }

    return result;
}

int DataManager::read_batch(FTDataType data_type, ShmDataPacket *packets, int max_count)
{
    if (_stopped.load())
    {
        return 0; // 停止状态返回0
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_stopped.load() || !_current_source)
    {
        return 0; // 停止状态或无数据源时返回0
    }

    int result = _current_source->read_batch(data_type, packets, max_count);
    if (result < 0)
    {
        _set_error(_current_source->get_last_error());
    }

    return result;
}

// 便捷接口实现
ssize_t DataManager::write_index_data(const IndexData &data)
{
    return write(DATA_TYPE_INDEX, &data, sizeof(data));
}

ssize_t DataManager::write_depth_data(const DepthData &data)
{
    return write(DATA_TYPE_DEPTH, &data, sizeof(data));
}

ssize_t DataManager::write_entrust_data(const EntrustData &data)
{
    return write(DATA_TYPE_ENTRUST, &data, sizeof(data));
}

ssize_t DataManager::write_trade_data(const TradeData &data)
{
    return write(DATA_TYPE_TRADE, &data, sizeof(data));
}

int DataManager::read_index_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_INDEX, packets, max_count);
}

int DataManager::read_depth_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_DEPTH, packets, max_count);
}

int DataManager::read_entrust_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_ENTRUST, packets, max_count);
}

int DataManager::read_trade_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_TRADE, packets, max_count);
}

ssize_t DataManager::write_trade_order(const TradeOrderData &data)
{
    return write(DATA_TYPE_TRADE_ORDER_RTN, &data, sizeof(data));
}

ssize_t DataManager::write_trade_fill(const TradeFillData &data)
{
    return write(DATA_TYPE_TRADE_FILL, &data, sizeof(data));
}

bool DataManager::is_available() const
{
    if (_stopped.load())
    {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);
    return _current_source && _current_source->is_available();
}

std::vector<FTDataType> DataManager::get_enabled_data_types() const
{
    if (_stopped.load())
    {
        return {};
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (!_current_source)
    {
        return {};
    }

    return _current_source->get_enabled_data_types();
}

void DataManager::get_stats(FTDataType data_type, uint64_t *total_written, uint64_t *total_read, uint64_t *available_space) const
{
    if (_stopped.load())
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
        return;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_current_source)
    {
        _current_source->get_stats(data_type, total_written, total_read, available_space);
    }
    else
    {
        if (total_written)
            *total_written = 0;
        if (total_read)
            *total_read = 0;
        if (available_space)
            *available_space = 0;
    }
}

void DataManager::reset_stats()
{
    if (_stopped.load())
    {
        return;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (_current_source)
    {
        _current_source->reset_stats();
    }
}

void DataManager::_set_error(const std::string &error)
{
    _last_error = error;
}

// 行情数据 - 五十档深度
ssize_t DataManager::write_depth_fifty_data(const DepthFiftyData &data)
{
    return write(DATA_TYPE_DEPTH_FIFTY, &data, sizeof(data));
}

int DataManager::read_depth_fifty_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_DEPTH_FIFTY, packets, max_count);
}

// 交易数据 - 持仓信息
ssize_t DataManager::write_position_data(const PositionData &data)
{
    return write(DATA_TYPE_TRADE_POSITION, &data, sizeof(data));
}

// 交易数据 - 账户信息
ssize_t DataManager::write_account_data(const AccountData &data)
{
    return write(DATA_TYPE_TRADE_ACCOUNT, &data, sizeof(data));
}

// 交易数据 - 银证转账
ssize_t DataManager::write_bank_securities_transfer(const BankSecuritiesTransferData &data)
{
    return write(DATA_TYPE_BANK_SECURITIES_TRANSFER, &data, sizeof(data));
}

// 交易数据 - 资金转账
ssize_t DataManager::write_fund_transfer(const FundTransferData &data)
{
    return write(DATA_TYPE_FUND_TRANSFER, &data, sizeof(data));
}

// 交易响应数据 - 委托响应
ssize_t DataManager::write_order_rsp(const TradeOrderRspData &data)
{
    return write(DATA_TYPE_ORDER_RSP, &data, sizeof(data));
}

// 交易响应数据 - 撤单响应
ssize_t DataManager::write_cancel_rsp(const TradeOrderCancelRsqData &data)
{
    return write(DATA_TYPE_CANCEL_RSP, &data, sizeof(data));
}

// 批量读取接口 - 交易数据
int DataManager::read_trade_order_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_TRADE_ORDER_RTN, packets, max_count);
}

int DataManager::read_trade_fill_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_TRADE_FILL, packets, max_count);
}

int DataManager::read_position_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_TRADE_POSITION, packets, max_count);
}

int DataManager::read_account_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_TRADE_ACCOUNT, packets, max_count);
}

int DataManager::read_bank_securities_transfer_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_BANK_SECURITIES_TRANSFER, packets, max_count);
}

int DataManager::read_fund_transfer_batch(ShmDataPacket *packets, int max_count)
{
    return read_batch(DATA_TYPE_FUND_TRANSFER, packets, max_count);
}

NS_FT_END