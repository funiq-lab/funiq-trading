# Strategy module CMakeLists.txt

find_package(Threads REQUIRED)

# 复用已有的依赖设置函数
if(DEFINED setup_boost_dependency)
    setup_boost_dependency()
else()
    find_package(Boost REQUIRED COMPONENTS system)
endif()

if(DEFINED setup_nlohmann_json_dependency)
    setup_nlohmann_json_dependency()
else()
    find_package(nlohmann_json REQUIRED)
endif()

# =============================================================================
# 基础策略库
# =============================================================================

set(BASE_STRATEGY_HEADERS
    ${CMAKE_SOURCE_DIR}/include/strategy/base_strategy.h
    ${CMAKE_SOURCE_DIR}/include/strategy/strategy_interface.h
)

# Create base strategy header-only library
add_library(ft_base_strategy INTERFACE)

target_include_directories(ft_base_strategy INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_base_strategy INTERFACE
    ft_utils
    ft_logger
    ft_data
    ft_database_manager
    Boost::system
    Threads::Threads
)

# 对于 INTERFACE 库，只设置 INTERFACE 属性
target_compile_features(ft_base_strategy INTERFACE cxx_std_17)

# 添加策略实现子目录
add_subdirectory(strategies)

# =============================================================================
# Strategy 库
# =============================================================================

set(STRATEGY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/strategy_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/strategy_service.cpp
)

set(STRATEGY_HEADERS
    ${CMAKE_SOURCE_DIR}/include/strategy/strategy_adapter.h
    ${CMAKE_SOURCE_DIR}/include/strategy/strategy_service.h
)

# Create static library for internal use
add_library(ft_strategy STATIC ${STRATEGY_SOURCES} ${STRATEGY_HEADERS})

target_include_directories(ft_strategy PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_strategy PUBLIC
    ft_base_strategy
    ft_stop_profit_loss_strategy
    ft_database_manager
    Boost::system
    Threads::Threads
    nlohmann_json::nlohmann_json
    ft_logger
    ft_data
    ft_utils
)

if(UNIX)
    target_link_libraries(ft_strategy PUBLIC dl)
endif()

# 对于普通库，可以使用 setup_target_compile_options
if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_strategy)
else()
    target_compile_features(ft_strategy PUBLIC cxx_std_17)
    target_compile_options(ft_strategy PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    
    set_target_properties(ft_strategy PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
endif()

set_target_properties(ft_strategy PROPERTIES
    OUTPUT_NAME "ft_strategy"
)

# =============================================================================
# 生产部署的共享库 - 供Python等外部语言调用
# =============================================================================

# Create shared library for external use (Python/other languages)
add_library(strategy_service SHARED ${STRATEGY_SOURCES} ${STRATEGY_HEADERS})

target_include_directories(strategy_service PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

if(BUILD_PRODUCTION)
    # 生产模式：使用动态链接系统库以避免PIC问题
    target_link_libraries(strategy_service PRIVATE
        ft_base_strategy
        ft_stop_profit_loss_strategy
        ft_database_manager
        ft_logger
        ft_data
        ft_utils
        
        # 使用动态 Boost 库以避免 PIC 问题
        Boost::system
        
        $<$<TARGET_EXISTS:nlohmann_json::nlohmann_json>:nlohmann_json::nlohmann_json>
        
        Threads::Threads
    )
    
    message(STATUS "生产模式：strategy_service.so 使用动态系统库以避免 PIC 问题")
else()
    # 开发模式：正常链接
    target_link_libraries(strategy_service PUBLIC
        ft_base_strategy
        ft_stop_profit_loss_strategy
        ft_database_manager
        Boost::system
        Threads::Threads
        nlohmann_json::nlohmann_json
        ft_logger
        ft_data
        ft_utils
    )
endif()

# Platform-specific libraries
if(UNIX)
    target_link_libraries(strategy_service PRIVATE dl pthread)
endif()

# 对于共享库，也可以使用 setup_target_compile_options
if(COMMAND setup_target_compile_options)
    setup_target_compile_options(strategy_service)
else()
    target_compile_features(strategy_service PUBLIC cxx_std_17)
    target_compile_options(strategy_service PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    
    set_target_properties(strategy_service PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# 设置 RPATH 以解决运行时依赖 - 优先当前目录
if(NOT WIN32)
    set_target_properties(strategy_service PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
    )
    
    message(STATUS "✓ strategy_service RPATH 设置：优先当前目录查找")
endif()

set_target_properties(strategy_service PROPERTIES
    OUTPUT_NAME "strategy_service"
)

# 构建后检查
add_custom_command(TARGET strategy_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "✅ strategy_service 构建完成"
    COMMAND ${CMAKE_COMMAND} -E echo "📁 输出位置: ${LIBRARY_OUTPUT_PATH}/libstrategy_service.so"
    COMMENT "构建完成检查"
)

# =============================================================================
# 构建信息
# =============================================================================

if(BUILD_PRODUCTION)
    message(STATUS "=== Strategy 模块生产配置 ===")
    message(STATUS "目标文件: ${LIBRARY_OUTPUT_PATH}/libstrategy_service.so")
    message(STATUS "部署说明: 该 .so 文件包含大部分必需依赖，可直接部署")
    message(STATUS "系统要求: 目标系统需要兼容的 glibc 和基础系统库")
endif()

# 输出构建信息
message(STATUS "=== Strategy 模块构建配置 ===")
message(STATUS "静态库: ft_strategy")
message(STATUS "共享库: strategy_service")
message(STATUS "基础策略: ft_base_strategy")
message(STATUS "数据库支持: ft_database_manager")
message(STATUS "====================================")