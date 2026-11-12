#include "utils/time.h"
#include <chrono>


uint64_t get_current_timestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}