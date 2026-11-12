#pragma once

#include <string>
#include <unordered_map>
#include "core/macros.h"

NS_FT_BEGIN

namespace network {

/**
 * @brief 业务错误码定义 - Common Errors (Category A)
 * 
 * 错误码格式：[Letter][Module][Sequence]
 * 示例：A0101
 * - Letter: 模块标识符 (1字符) - A表示Common errors
 * - Module: 分类标识符 (2位数字) - 00-10表示不同分类
 * - Sequence: 错误序号 (2位数字) - 01-99表示该分类下的具体错误
 * 
 * 模块分类：
 * - 00: System level (系统级错误)
 * - 01: Authentication & Authorization (认证授权)
 * - 02: Input/Parameter validation (输入参数验证)
 * - 03: Resource related (资源相关)
 * - 04: Operation related (操作相关)
 * - 05: Rate limiting & Quota (频率限制和配额)
 * - 06: External service/integration (外部服务集成)
 * - 07: Data processing & validation (数据处理验证)
 * - 08: Configuration related (配置相关)
 * - 09: Security related (安全相关)
 * - 10: Business logic related (业务逻辑相关)
 */
class ErrorCode {
public:
    // ===== 成功码 =====
    static constexpr const char* SUCCESS = "0";

    // ===== System level errors (A00xx) =====
    static constexpr const char* INTERNAL_SERVER_ERROR = "A0001";         ///< 内部服务器错误
    static constexpr const char* SERVICE_UNAVAILABLE = "A0002";           ///< 服务不可用
    static constexpr const char* SYSTEM_TIMEOUT = "A0003";                ///< 系统超时

    // ===== Authentication & Authorization (A01xx) =====
    static constexpr const char* UNAUTHORIZED = "A0101";                  ///< 未授权
    static constexpr const char* PERMISSION_DENIED = "A0102";             ///< 权限不足

    // ===== Input/Parameter validation (A02xx) =====
    static constexpr const char* INVALID_ARGUMENT = "A0201";              ///< 无效参数
    static constexpr const char* INVALID_FORMAT = "A0202";                ///< 无效格式
    static constexpr const char* MISSING_REQUIRED_FIELD = "A0203";        ///< 缺少必需字段
    static constexpr const char* PARAMETER_OUT_OF_RANGE = "A0204";        ///< 参数超出范围
    static constexpr const char* INVALID_JSON_FORMAT = "A0205";           ///< 无效JSON格式
    static constexpr const char* INVALID_REQUEST_METHOD = "A0206";        ///< 无效请求方法
    static constexpr const char* INVALID_CONTENT_TYPE = "A0207";          ///< 无效内容类型

    // ===== Resource related (A03xx) =====
    static constexpr const char* RESOURCE_NOT_FOUND = "A0301";            ///< 资源不存在
    static constexpr const char* DATABASE_ERROR = "A0302";                ///< 数据库错误

    // ===== Operation related (A04xx) =====
    static constexpr const char* OPERATION_NOT_ALLOWED = "A0401";          ///< 操作不允许


    /**
     * @brief 获取错误码对应的错误消息
     * 
     * @param code 错误码字符串
     * @return 错误消息字符串，未找到时返回"Unknown error"
     * @note 线程安全
     */
    static std::string get_message(const std::string& code);

    /**
     * @brief 判断是否为成功码
     * 
     * @param code 错误码字符串
     * @return true 成功码，false 错误码
     */
    static bool is_success(const std::string& code);

private:
    /// 错误码到错误消息的映射表
    static const std::unordered_map<std::string, std::string> _error_messages;
};

} // namespace network

NS_FT_END