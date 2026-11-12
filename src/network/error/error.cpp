#include "network/error/error.h"

NS_FT_BEGIN

namespace network {

// 错误码到错误消息的映射表
const std::unordered_map<std::string, std::string> ErrorCode::_error_messages = {
    // 成功码
    {"0", "Success"},

    // System level errors (A00xx)
    {"A0001", "Internal Server Error"},
    {"A0002", "Service Unavailable"},
    {"A0003", "System Timeout"},

    // Authentication & Authorization (A01xx)
    {"A0101", "Unauthorized"},
    {"A0102", "Permission Denied"},

    // Input/Parameter validation (A02xx)
    {"A0201", "Invalid Argument"},
    {"A0202", "Invalid Format"},
    {"A0203", "Missing Required Field"},
    {"A0204", "Parameter Out of Range"},
    {"A0205", "Invalid JSON Format"},
    {"A0206", "Invalid Request Method"},
    {"A0207", "Invalid Content Type"},

    // Resource related (A03xx)
    {"A0301", "Resource not found"},
    {"A0302", "Database error"},

    // Operation related (A04xx)
    {"A0401", "Operation not allowed"},
};

std::string ErrorCode::get_message(const std::string& code) {
    auto it = _error_messages.find(code);
    if (it != _error_messages.end()) {
        return it->second;
    }
    return "Unknown error";
}

bool ErrorCode::is_success(const std::string& code) {
    return code == SUCCESS;
}

} // namespace network

NS_FT_END
