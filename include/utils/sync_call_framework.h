#ifndef SYNC_CALL_FRAMEWORK_H
#define SYNC_CALL_FRAMEWORK_H

#include "core/macros.h"
#include <future>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <cstring>
#include <vector>

NS_FT_BEGIN

// 通用同步调用结果
template<typename T>
struct SyncResult {
    int32_t error_code = 0;
    T data;
    char error_msg[256] = {0};
    
    bool is_success() const { return error_code == 0; }
    
    // 设置成功结果
    void set_success(const T& result_data) {
        error_code = 0;
        data = result_data;
        error_msg[0] = '\0';
    }
    
    // 设置错误结果
    void set_error(int32_t code, const char* msg) {
        error_code = code;
        if (msg) {
            strncpy(error_msg, msg, sizeof(error_msg) - 1);
            error_msg[sizeof(error_msg) - 1] = '\0';
        } else {
            error_msg[0] = '\0';
        }
        // data 保持默认值
    }
};

// 完整的响应信息（包含错误码）
template<typename T>
struct CompleteResponse {
    int32_t error_code;
    T data;
    std::string error_msg;
    
    CompleteResponse() : error_code(0) {}
    CompleteResponse(const T& d) : error_code(0), data(d) {}
    CompleteResponse(int32_t code, const std::string& msg) : error_code(code), error_msg(msg) {}
};

// 改进的同步调用管理器
template<typename ResponseType>
class SyncCallManager {
private:
    std::unordered_map<int64_t, std::promise<CompleteResponse<ResponseType>>> promises_;
    std::mutex mutex_;

public:
    std::future<CompleteResponse<ResponseType>> create_future(int64_t request_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& promise = promises_[request_id];
        return promise.get_future();
    }
    
    // 设置成功结果
    void set_success(int64_t request_id, const ResponseType& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = promises_.find(request_id);
        if (it != promises_.end()) {
            try {
                it->second.set_value(CompleteResponse<ResponseType>(result));
            } catch (const std::exception& e) {
                // Promise already satisfied, ignore
            }
        }
    }
    
    // 设置错误结果
    void set_error(int64_t request_id, int32_t error_code, const std::string& error_msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = promises_.find(request_id);
        if (it != promises_.end()) {
            try {
                it->second.set_value(CompleteResponse<ResponseType>(error_code, error_msg));
            } catch (const std::exception& e) {
                // Promise already satisfied, ignore
            }
        }
    }
    
    // 设置异常（用于超时等系统级错误）
    void set_exception(int64_t request_id, const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = promises_.find(request_id);
        if (it != promises_.end()) {
            try {
                it->second.set_exception(std::make_exception_ptr(e));
            } catch (const std::exception& ex) {
                // Promise already satisfied, ignore
            }
        }
    }
    
    void cleanup(int64_t request_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        promises_.erase(request_id);
    }
};

/**
 * 同步调用数组管理器
 * 用于管理批量查询结果的累积，直到is_last为true时完成同步
 */
template<typename T>
class SyncArrayManager {
private:
    struct ArrayAccumulator {
        std::vector<T> items;
        bool completed;
        
        ArrayAccumulator() : completed(false) {}
    };
    
    std::unordered_map<int64_t, ArrayAccumulator> _accumulators;
    std::unordered_map<int64_t, std::promise<CompleteResponse<T>>> _promises;
    std::mutex _mutex;
    
public:
    SyncArrayManager() = default;
    
    /**
     * 创建future用于等待同步结果
     * @param request_id 请求ID
     * @return future对象
     */
    std::future<CompleteResponse<T>> create_future(int64_t request_id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto& promise = _promises[request_id];
        return promise.get_future();
    }
    
    /**
     * 添加单个查询结果项
     * @param request_id 请求ID
     * @param item 查询结果项
     * @param is_last 是否为最后一项
     */
    void add_item(int64_t request_id, const T& item, bool is_last) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto& accumulator = _accumulators[request_id];
        accumulator.items.push_back(item);
        
        if (is_last) {
            accumulator.completed = true;
            
            // 完成同步调用
            auto promise_it = _promises.find(request_id);
            if (promise_it != _promises.end()) {
                try {
                    // 返回最后一个结果（通常包含汇总信息）
                    if (!accumulator.items.empty()) {
                        promise_it->second.set_value(CompleteResponse<T>(accumulator.items.back()));
                    } else {
                        promise_it->second.set_value(CompleteResponse<T>(-1, "No data received"));
                    }
                } catch (const std::exception& e) {
                    // Promise already satisfied, ignore
                }
            }
        }
    }
    
    /**
     * 设置错误
     * @param request_id 请求ID
     * @param error_code 错误代码
     * @param error_msg 错误消息
     */
    void set_error(int64_t request_id, int32_t error_code, const std::string& error_msg) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto promise_it = _promises.find(request_id);
        if (promise_it != _promises.end()) {
            try {
                promise_it->second.set_value(CompleteResponse<T>(error_code, error_msg));
            } catch (const std::exception& e) {
                // Promise already satisfied, ignore
            }
        }
    }
    
    /**
     * 清理资源
     * @param request_id 请求ID
     */
    void cleanup(int64_t request_id) {
        std::lock_guard<std::mutex> lock(_mutex);
        _accumulators.erase(request_id);
        _promises.erase(request_id);
    }
    
    /**
     * 获取累积的数组（用于调试或其他用途）
     * @param request_id 请求ID
     * @return 累积的数组
     */
    std::vector<T> get_accumulated_items(int64_t request_id) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _accumulators.find(request_id);
        if (it != _accumulators.end()) {
            return it->second.items;
        }
        return std::vector<T>();
    }
};

NS_FT_END

#endif // SYNC_CALL_FRAMEWORK_H