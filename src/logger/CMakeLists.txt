# Logger module - 基于官方示例的 header-only 配置

find_package(Threads REQUIRED)

# =============================================================================
# 强制禁用系统spdlog查找，使用本地版本
# =============================================================================

set(CMAKE_DISABLE_FIND_PACKAGE_spdlog ON)

# 修正：现在spdlog在include目录下
set(SPDLOG_LOCAL_ROOT ${CMAKE_SOURCE_DIR}/include)  # 改为include目录

message(STATUS "✓ 使用本地 spdlog (header-only): ${SPDLOG_LOCAL_ROOT}/spdlog")

# =============================================================================
# 创建本地 spdlog header-only 目标（仿照官方示例）
# =============================================================================

# 创建 header-only 接口库
add_library(spdlog_local_header_only INTERFACE)

# 修正：指向include目录
target_include_directories(spdlog_local_header_only INTERFACE
    $<BUILD_INTERFACE:${SPDLOG_LOCAL_ROOT}>
    $<INSTALL_INTERFACE:include>
)


target_compile_definitions(spdlog_local_header_only INTERFACE
    SPDLOG_HEADER_ONLY
)

# 链接线程库
target_link_libraries(spdlog_local_header_only INTERFACE Threads::Threads)

# 创建别名
add_library(spdlog::spdlog_header_only ALIAS spdlog_local_header_only)

# =============================================================================
# fmt 处理 (使用spdlog内置的bundled fmt)
# =============================================================================

add_library(fmt_bundled INTERFACE)
target_include_directories(fmt_bundled INTERFACE 
    ${SPDLOG_LOCAL_ROOT}  # 也改为include目录
)
target_compile_definitions(fmt_bundled INTERFACE 
    FMT_HEADER_ONLY=1
)
add_library(fmt::fmt ALIAS fmt_bundled)

# =============================================================================
# Logger库
# =============================================================================

set(LOGGER_SOURCES logger.cpp)
set(LOGGER_HEADERS ${CMAKE_SOURCE_DIR}/include/logger/logger.hpp)

add_library(ft_logger STATIC ${LOGGER_SOURCES})

# 设置包含路径
target_include_directories(ft_logger PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# 关键：使用 header-only 目标
target_link_libraries(ft_logger PUBLIC 
    spdlog::spdlog_header_only
)

target_compile_definitions(ft_logger PUBLIC
    SPDLOG_HEADER_ONLY
)

setup_target_compile_options(ft_logger)

set_target_properties(ft_logger PROPERTIES
    OUTPUT_NAME "ft_logger"
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "=== Logger配置完成 ===")
message(STATUS "使用 spdlog::spdlog_header_only (header-only 模式)")