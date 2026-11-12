#ifndef UTILS_TIME_H
#define UTILS_TIME_H

#include "core/macros.h"
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * 获取当前时间戳(微秒)
 * @return 时间戳
 */
EXPORT_FLAG uint64_t get_current_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif // UTILS_TIME_H