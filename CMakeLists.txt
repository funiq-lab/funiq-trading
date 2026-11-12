cmake_minimum_required(VERSION 3.16)
project(FuniqTrading VERSION 0.0.2 LANGUAGES CXX)

# 使用C++17而不是C++20，与老版本gcc兼容
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for better IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 生产部署选项 - Release 模式默认启用生产配置
option(BUILD_PRODUCTION "Build for production deployment with embedded dependencies" OFF)
option(ENABLE_STATIC_LINKING "Enable static linking of third-party dependencies" OFF)

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 构建类型选项
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")

# Release 模式自动启用生产配置（除非明确禁用）
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT DEFINED BUILD_PRODUCTION)
    set(BUILD_PRODUCTION ON)
    message(STATUS "Release 模式：自动启用生产部署配置")
endif()

# 生产部署配置
if(BUILD_PRODUCTION)
    message(STATUS "=== 生产部署模式 ===")
    message(STATUS "启用静态链接以创建自包含的共享库")
    
    set(ENABLE_STATIC_LINKING ON)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(BUILD_SHARED_LIBS OFF)
    
    # 设置静态链接标志
    if(NOT WIN32)
        # 静态链接 libgcc 和 libstdc++，但不静态链接 glibc
        # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        # 隐藏所有符号，只导出必要的API
        # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
    endif()
endif()

# 编译器特定的调试和优化标志
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Debug flags
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG -Wall -Wextra -fno-omit-frame-pointer")
    
    # Release flags - 生产模式优化
    if(BUILD_PRODUCTION)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -flto")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
    endif()
    
    # Release with debug info
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
    
    # Minimum size release
    set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
    
    # 添加调试工具支持
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # AddressSanitizer for memory error detection
        option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
        if(ENABLE_ASAN)
            set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-optimize-sibling-calls")
            set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
            message(STATUS "AddressSanitizer enabled")
        endif()
        
        # UndefinedBehaviorSanitizer
        option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
        if(ENABLE_UBSAN)
            set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
            set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=undefined")
            message(STATUS "UndefinedBehaviorSanitizer enabled")
        endif()
        
        # ThreadSanitizer (mutually exclusive with AddressSanitizer)
        option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
        if(ENABLE_TSAN AND NOT ENABLE_ASAN)
            set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
            set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=thread")
            message(STATUS "ThreadSanitizer enabled")
        endif()
    endif()
endif()

# 通用编译选项设置函数
function(setup_target_compile_options target_name)
    # Platform-specific definitions
    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _WIN32_WINNT=0x0601
            NOMINMAX
        )
        
        # Compiler-specific flags
        target_compile_options(${target_name} PRIVATE
            /W4 /permissive-
            $<$<CONFIG:Release>:/O2 /DNDEBUG>
            $<$<CONFIG:Debug>:/Od /Zi /RTC1>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi /DNDEBUG>
            $<$<CONFIG:MinSizeRel>:/Os /DNDEBUG>
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<CONFIG:Release>:-O3 -DNDEBUG -s>
            $<$<CONFIG:Debug>:-g -O0 -DDEBUG -fno-omit-frame-pointer>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
            $<$<CONFIG:MinSizeRel>:-Os -DNDEBUG>
        )
        
        # Debug模式下启用额外的调试信息
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target_name} PRIVATE
                -fno-optimize-sibling-calls
                -fstack-protector-all
            )
        endif()
    endif()
    
    # 生产模式特殊设置
    if(BUILD_PRODUCTION)
        target_compile_definitions(${target_name} PRIVATE FT_PRODUCTION_BUILD=1)
        if(NOT WIN32)
            # 默认隐藏符号，只导出标记的API
            target_compile_options(${target_name} PRIVATE -fvisibility=hidden)
        endif()
    endif()
    
    # Set common properties
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()

# Platform and compiler detection
if(WIN32)
    message(STATUS "Operation System is Windows")
    if(MSVC)
        message(STATUS "Compiler is MSVC")
        if(CMAKE_CL_64)
            message(STATUS "Platform is x64")
            set(PLATFORM x64)
        else()
            message(STATUS "Platform is x86")  
            set(PLATFORM x86)
        endif()
        
        # MSVC specific definitions
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7 and later
        add_definitions(-DNOMINMAX)  # Prevent min/max macro conflicts
        
        # Use environment variable for dependencies
        if(DEFINED ENV{VCPKG_ROOT})
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
            message(STATUS "Using vcpkg from $ENV{VCPKG_ROOT}")
        endif()
        
    else()
        message(STATUS "Compiler is MinGW/GCC on Windows")
        set(PLATFORM x64)
    endif()
elseif(UNIX)
    message(STATUS "Operation System is UNIX-like OS")
    message(STATUS "Platform is x64")
    set(PLATFORM x64)
    message(STATUS "Generator is ${CMAKE_GENERATOR}")
    
    # GCC版本信息
    message(STATUS "GCC version: ${CMAKE_CXX_COMPILER_VERSION}")
    
    # 对于老版本GCC的特殊处理
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8.0")
        message(STATUS "Using compatibility flags for older GCC")
        # 添加兼容性编译标志
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    endif()
endif()

# Set output directories based on build type
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/build_${PLATFORM}/${CMAKE_BUILD_TYPE}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/build_${PLATFORM}/${CMAKE_BUILD_TYPE}/lib)

# Version information
find_package(Git REQUIRED)
if(GIT_FOUND)
    # Get the current git branch
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # Get the latest git commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --pretty=format:%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_BRANCH "N/A")
    set(GIT_COMMIT_HASH "N/A")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/include/core/version.h.in"
    "${CMAKE_BINARY_DIR}/include/core/version.h"
)


# Global include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)

# 在 add_subdirectory 之前添加全局依赖处理函数
# =============================================================================
# 全局依赖处理函数
# =============================================================================

# 修改全局 Boost 依赖处理函数，默认包含常用组件
function(setup_boost_dependency)
    # 解析参数 - 支持指定需要的组件
    set(REQUIRED_COMPONENTS ${ARGV})
    if(NOT REQUIRED_COMPONENTS)
        # 默认包含项目中需要的所有组件
        set(REQUIRED_COMPONENTS system thread)
    endif()
    
    if(BUILD_PRODUCTION OR ENABLE_STATIC_LINKING)
        # 生产模式：静态链接
        set(Boost_USE_STATIC_LIBS ON PARENT_SCOPE)
        set(Boost_USE_MULTITHREADED ON PARENT_SCOPE)
        set(Boost_USE_STATIC_RUNTIME OFF PARENT_SCOPE)
        
        # 优先查找静态库
        find_package(Boost QUIET COMPONENTS ${REQUIRED_COMPONENTS})
        
        if(NOT Boost_FOUND)
            message(STATUS "系统静态 Boost 未找到，使用动态版本...")
            set(Boost_USE_STATIC_LIBS OFF PARENT_SCOPE)
            find_package(Boost REQUIRED COMPONENTS ${REQUIRED_COMPONENTS})
            message(STATUS "✓ 使用动态 Boost 库（组件: ${REQUIRED_COMPONENTS}）")
        else()
            message(STATUS "✓ 找到静态 Boost 库（组件: ${REQUIRED_COMPONENTS}）")
        endif()
    else()
        # 开发模式：使用动态库
        find_package(Boost REQUIRED COMPONENTS ${REQUIRED_COMPONENTS})
        message(STATUS "✓ 使用动态 Boost 库（组件: ${REQUIRED_COMPONENTS}）")
    endif()
endfunction()

# nlohmann_json 依赖处理
function(setup_nlohmann_json_dependency)
    if(BUILD_PRODUCTION OR ENABLE_STATIC_LINKING)
        # 生产模式：使用头文件库或静态构建
        find_package(nlohmann_json QUIET)
        
        if(NOT nlohmann_json_FOUND)
            message(STATUS "系统 nlohmann_json 未找到，使用源码构建...")
            include(FetchContent)
            FetchContent_Declare(
                nlohmann_json
                GIT_REPOSITORY https://github.com/nlohmann/json.git
                GIT_TAG v3.11.2
            )
            
            set(JSON_BuildTests OFF CACHE BOOL "")
            set(JSON_Install OFF CACHE BOOL "")
            
            FetchContent_MakeAvailable(nlohmann_json)
        endif()
    else()
        find_package(nlohmann_json REQUIRED)
    endif()
endfunction()

# tinyxml2 依赖处理
function(setup_tinyxml2_dependency)
    # 检查本地源码文件
    set(TINYXML2_LOCAL_DIR ${CMAKE_SOURCE_DIR}/lib/tinyxml2)
    
    if(EXISTS ${TINYXML2_LOCAL_DIR}/tinyxml2.h AND EXISTS ${TINYXML2_LOCAL_DIR}/tinyxml2.cpp)
        message(STATUS "✓ 使用内置 tinyxml2 源码: ${TINYXML2_LOCAL_DIR}")
        
        # 检查是否已经创建
        if(NOT TARGET tinyxml2_builtin)
            # 创建内置 tinyxml2 库
            add_library(tinyxml2_builtin STATIC 
                ${TINYXML2_LOCAL_DIR}/tinyxml2.cpp
            )
            
            target_include_directories(tinyxml2_builtin PUBLIC
                ${TINYXML2_LOCAL_DIR}
            )
            
            # 创建标准别名
            if(NOT TARGET tinyxml2::tinyxml2)
                add_library(tinyxml2::tinyxml2 ALIAS tinyxml2_builtin)
            endif()
            
            # 设置编译选项
            if(MSVC)
                target_compile_definitions(tinyxml2_builtin PRIVATE
                    _CRT_SECURE_NO_WARNINGS
                )
            endif()
            
            # 静态库不需要导出符号
            set_target_properties(tinyxml2_builtin PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                OUTPUT_NAME "tinyxml2"
            )
        endif()
        
        return()
    endif()
    
    # 后备方案：使用系统库
    find_package(tinyxml2 REQUIRED)
endfunction()

# 在 add_subdirectory 之前调用全局依赖设置
# 设置项目需要的所有Boost组件
setup_boost_dependency(system thread)  # 明确指定需要的组件
setup_nlohmann_json_dependency()
setup_tinyxml2_dependency()

# Add third-party libraries
add_subdirectory(lib)

# Add all source modules (each manages its own dependencies)
include(FetchContent)

add_subdirectory(src/utils)
add_subdirectory(src/logger)
add_subdirectory(src/algorithm)
add_subdirectory(src/data)
add_subdirectory(src/quote)
add_subdirectory(src/trade)
add_subdirectory(src/strategy)
add_subdirectory(src/database)
add_subdirectory(src/network)

# 添加主程序（最后添加，因为它依赖其他所有模块）
add_subdirectory(src)

# Create necessary directories
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH}/logs)
file(MAKE_DIRECTORY ${LIBRARY_OUTPUT_PATH})

# Copy shell scripts to bin and chmod +x
set(SCRIPTS_DIR ${CMAKE_SOURCE_DIR}/scripts)
if(EXISTS ${SCRIPTS_DIR})
    file(GLOB FT_SCRIPTS "${SCRIPTS_DIR}/*.sh")
    set(COPIED_SCRIPTS)
    foreach(script ${FT_SCRIPTS})
        get_filename_component(name ${script} NAME)
        add_custom_command(
            OUTPUT ${EXECUTABLE_OUTPUT_PATH}/${name}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${script} ${EXECUTABLE_OUTPUT_PATH}/${name}
            COMMAND /bin/sh -c "chmod +x ${EXECUTABLE_OUTPUT_PATH}/${name}"
            DEPENDS ${script}
            VERBATIM
        )
        list(APPEND COPIED_SCRIPTS ${EXECUTABLE_OUTPUT_PATH}/${name})
    endforeach()
    if(COPIED_SCRIPTS)
        add_custom_target(copy_scripts ALL DEPENDS ${COPIED_SCRIPTS})
    endif()
endif()

# Print build information
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Platform: ${PLATFORM}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Production Build: ${BUILD_PRODUCTION}")
message(STATUS "Static Linking: ${ENABLE_STATIC_LINKING}")
message(STATUS "Executable output path: ${EXECUTABLE_OUTPUT_PATH}")
message(STATUS "Library output path: ${LIBRARY_OUTPUT_PATH}")
message(STATUS "Available executables will be built in respective modules:")
message(STATUS "  - quote_service (in src/quote)")

# Debug build specific information
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build enabled with the following tools:")
    if(ENABLE_ASAN)
        message(STATUS "  - AddressSanitizer: ON")
    endif()
    if(ENABLE_UBSAN)
        message(STATUS "  - UndefinedBehaviorSanitizer: ON")
    endif()
    if(ENABLE_TSAN)
        message(STATUS "  - ThreadSanitizer: ON")
    endif()
    message(STATUS "Debug tools available: gdb, valgrind, strace")
endif()

# 生产模式提示
if(BUILD_PRODUCTION)
    message(STATUS "=== 生产模式配置 ===")
    message(STATUS "生成的 libquote_service.so 将包含所有必需依赖")
    message(STATUS "可直接部署到目标系统而无需安装额外依赖")
    message(STATUS "使用示例: cp ${LIBRARY_OUTPUT_PATH}/libquote_service.so /target/location/")
    message(STATUS "")
    message(STATUS "部署特性:")
    message(STATUS "  ✓ 静态链接第三方库 (boost, tinyxml2, etc.)")
    message(STATUS "  ✓ 静态链接 libgcc/libstdc++")
    message(STATUS "  ✓ 符号可见性优化")
    message(STATUS "  ✓ LTO 优化 (Link Time Optimization)")
    message(STATUS "  ✓ 自包含部署包")
endif()