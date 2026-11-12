#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <cstddef>
#include <cstring>
#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 安全地复制符号字符串，确保目标字符串以null结尾
 * @param dest 目标缓冲区
 * @param dest_size 目标缓冲区大小
 * @param src 源字符串
 * @param src_size 源字符串大小
 */
inline void safe_copy_code(char* dest, size_t dest_size, const char* src, size_t src_size) {
    if (!dest || !src || dest_size == 0) {
        return;
    }
    
    size_t copy_len = std::min(dest_size - 1, src_size);
    std::memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

/**
 * 安全地复制字符串，确保目标字符串以null结尾
 * @param dest 目标缓冲区
 * @param dest_size 目标缓冲区大小
 * @param src 源字符串
 */
inline void safe_copy_string(char* dest, size_t dest_size, const char* src) {
    if (!dest || !src || dest_size == 0) {
        return;
    }
    
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif // STRING_UTILS_H