# Trade module CMakeLists.txt

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
# 基础交易处理器库
# =============================================================================

set(BASE_HANDLER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/base_trade_handler.cpp
)

set(BASE_HANDLER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/trade/base_trade_handler.h
    ${CMAKE_SOURCE_DIR}/include/trade/trade_handler.h
    ${CMAKE_SOURCE_DIR}/include/trade/trade_interface.h
)

# Create base trade handler static library
add_library(ft_base_trade_handler STATIC ${BASE_HANDLER_SOURCES} ${BASE_HANDLER_HEADERS})

target_include_directories(ft_base_trade_handler PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_base_trade_handler PUBLIC
    ft_utils
    ft_logger
    ft_data
    ft_database_manager
    Boost::system
    Threads::Threads
)

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_base_trade_handler)
else()
    target_compile_features(ft_base_trade_handler PUBLIC cxx_std_17)
endif()

set_target_properties(ft_base_trade_handler PROPERTIES
    OUTPUT_NAME "ft_base_trade_handler"
    POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(atp_trade_handler)

# =============================================================================
# Trade 库
# =============================================================================

set(TRADE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/trade_handler_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/trade_service.cpp
)

set(TRADE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/trade/trade_handler_adapter.h
    ${CMAKE_SOURCE_DIR}/include/trade/trade_service.h
)

# Create static library for internal use
add_library(ft_trade STATIC ${TRADE_SOURCES} ${TRADE_HEADERS})

target_include_directories(ft_trade PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_trade PUBLIC
    ft_base_trade_handler
    ft_atp_trade_handler
    ft_database_manager
    Boost::system
    Threads::Threads
    nlohmann_json::nlohmann_json
    ft_logger
    ft_data
    ft_utils
)

if(UNIX)
    target_link_libraries(ft_trade PUBLIC dl)
endif()

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_trade)
else()
    target_compile_features(ft_trade PUBLIC cxx_std_17)
endif()

set_target_properties(ft_trade PROPERTIES
    OUTPUT_NAME "ft_trade"
    POSITION_INDEPENDENT_CODE ON
)

# =============================================================================
# 生产部署的共享库 - 供Python等外部语言调用
# =============================================================================

# Create shared library for external use (Python/other languages)
add_library(trade_service SHARED ${TRADE_SOURCES} ${TRADE_HEADERS})

target_include_directories(trade_service PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

if(BUILD_PRODUCTION)
    # 生产模式：使用动态链接系统库以避免PIC问题
    target_link_libraries(trade_service PRIVATE
        ft_base_trade_handler
        ft_atp_trade_handler
        ft_logger
        ft_data
        ft_utils
        ft_database_manager
        
        # 使用动态 Boost 库以避免 PIC 问题
        Boost::system
        
        $<$<TARGET_EXISTS:nlohmann_json::nlohmann_json>:nlohmann_json::nlohmann_json>
        
        Threads::Threads
    )
    
    message(STATUS "生产模式：trade_service.so 使用动态系统库以避免 PIC 问题")
else()
    # 开发模式：正常链接
    target_link_libraries(trade_service PRIVATE
        ft_base_trade_handler
        ft_atp_trade_handler
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
    target_link_libraries(trade_service PRIVATE dl pthread)
endif()

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(trade_service)
else()
    target_compile_features(trade_service PUBLIC cxx_std_17)
endif()

# 设置 RPATH 以解决运行时依赖 - 优先当前目录
if(NOT WIN32)
    set_target_properties(trade_service PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
    )
    
    message(STATUS "✓ trade_service RPATH 设置：优先当前目录查找")
endif()

set_target_properties(trade_service PROPERTIES
    OUTPUT_NAME "trade_service"
    POSITION_INDEPENDENT_CODE ON
)

# 构建后检查
add_custom_command(TARGET trade_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "✅ trade_service 构建完成"
    COMMAND ${CMAKE_COMMAND} -E echo "📁 输出位置: ${LIBRARY_OUTPUT_PATH}/libtrade_service.so"
    COMMENT "构建完成检查"
)

# =============================================================================
# 可执行文件 - 独立的交易服务程序
# =============================================================================

add_executable(trade_service_exe ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(trade_service_exe PRIVATE
    ft_trade
    ft_data
    ft_logger
    ft_utils
    ft_database_manager
    Boost::system
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(UNIX)
    target_link_libraries(trade_service_exe PRIVATE dl)
endif()

target_include_directories(trade_service_exe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(trade_service_exe)
else()
    target_compile_features(trade_service_exe PUBLIC cxx_std_17)
endif()

set_target_properties(trade_service_exe PROPERTIES
    OUTPUT_NAME "trade_service"
)

if(UNIX)
    add_custom_command(TARGET trade_service_exe POST_BUILD
        COMMAND chmod 755 $<TARGET_FILE:trade_service_exe>
        COMMENT "Setting executable permissions for trade_service"
    )
endif()

install(TARGETS trade_service_exe
    RUNTIME DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
               GROUP_READ GROUP_EXECUTE
               WORLD_READ WORLD_EXECUTE
)

# =============================================================================
# 构建信息
# =============================================================================

if(BUILD_PRODUCTION)
    message(STATUS "=== Trade 模块生产配置 ===")
    message(STATUS "目标文件: ${LIBRARY_OUTPUT_PATH}/libtrade_service.so")
    message(STATUS "部署说明: 该 .so 文件包含大部分必需依赖，可直接部署")
    message(STATUS "系统要求: 目标系统需要兼容的 glibc 和基础系统库")
endif()

# 输出构建信息
message(STATUS "=== Trade 模块构建配置 ===")
message(STATUS "静态库: ft_trade")
message(STATUS "共享库: trade_service")
message(STATUS "基础处理器: ft_base_trade_handler")
message(STATUS "数据库支持: ft_database_manager")
# message(STATUS "具体实现: TODO - ATP, CTP等")
message(STATUS "================================")