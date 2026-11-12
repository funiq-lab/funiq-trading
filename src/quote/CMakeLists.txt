# Quote module CMakeLists.txt - 使用全局依赖管理

# =============================================================================
# 第三方依赖管理 - 使用全局函数
# =============================================================================

find_package(Threads REQUIRED)

# 使用全局依赖处理函数（如果存在）
if(COMMAND setup_boost_dependency)
    setup_boost_dependency(system)
else()
    find_package(Boost REQUIRED COMPONENTS system)
    message(WARNING "使用本地 Boost 配置，建议使用全局依赖管理")
endif()

if(COMMAND setup_nlohmann_json_dependency)
    setup_nlohmann_json_dependency()
else()
    find_package(nlohmann_json REQUIRED)
    message(WARNING "使用本地 nlohmann_json 配置，建议使用全局依赖管理")
endif()

if(COMMAND setup_tinyxml2_dependency)
    setup_tinyxml2_dependency()
else()
    message(FATAL_ERROR "tinyxml2 依赖处理函数未找到，请确保全局依赖管理已配置")
endif()

# =============================================================================
# 基础解析器库
# =============================================================================

set(BASE_PARSER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/base_quote_parser.cpp
)

set(BASE_PARSER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/quote/base_quote_parser.h
    ${CMAKE_SOURCE_DIR}/include/quote/quote_parser.h
    ${CMAKE_SOURCE_DIR}/include/quote/quote_interface.h
)

# Create base parser static library
add_library(ft_base_quote_parser STATIC ${BASE_PARSER_SOURCES} ${BASE_PARSER_HEADERS})

target_include_directories(ft_base_quote_parser PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_base_quote_parser PUBLIC
    ft_utils
    ft_logger
    ft_data
    Boost::system
    Threads::Threads
)

setup_target_compile_options(ft_base_quote_parser)

set_target_properties(ft_base_quote_parser PROPERTIES
    OUTPUT_NAME "ft_base_quote_parser"
)

# Add ksqmd quote parser as subdirectory
add_subdirectory(ksqmd_quote_parser)

# =============================================================================
# Quote 库
# =============================================================================

set(QUOTE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/quote_parser_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/quote_service.cpp
)

set(QUOTE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/quote/quote_parser_adapter.h
    ${CMAKE_SOURCE_DIR}/include/quote/quote_service.h
)

# Create static library for internal use
add_library(ft_quote STATIC ${QUOTE_SOURCES} ${QUOTE_HEADERS})

target_include_directories(ft_quote PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ft_quote PUBLIC
    ft_base_quote_parser
    ft_ksqmd_quote_parser
    Boost::system
    Threads::Threads
    nlohmann_json::nlohmann_json
    tinyxml2::tinyxml2
    ft_logger
    ft_data
    ft_utils
)

if(UNIX)
    target_link_libraries(ft_quote PUBLIC dl)
endif()

setup_target_compile_options(ft_quote)

set_target_properties(ft_quote PROPERTIES
    OUTPUT_NAME "ft_quote"
)

# =============================================================================
# 生产部署的共享库
# =============================================================================

# Create shared library for external use (Python/other languages)
add_library(quote_service SHARED ${QUOTE_SOURCES} ${QUOTE_HEADERS})

target_include_directories(quote_service PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

if(BUILD_PRODUCTION)
    # 生产模式：为了避免 PIC 问题，使用动态链接系统库
    target_link_libraries(quote_service PRIVATE
        ft_base_quote_parser
        ft_ksqmd_quote_parser
        ft_logger
        ft_data
        ft_utils
        
        # 使用动态 Boost 库以避免 PIC 问题
        Boost::system
        
        $<$<TARGET_EXISTS:nlohmann_json::nlohmann_json>:nlohmann_json::nlohmann_json>
        $<$<TARGET_EXISTS:tinyxml2::tinyxml2>:tinyxml2::tinyxml2>
        
        Threads::Threads
    )
    
    message(STATUS "生产模式：quote_service.so 使用动态系统库以避免 PIC 问题")
else()
    # 开发模式：正常链接
    target_link_libraries(quote_service PUBLIC
        ft_base_quote_parser
        ft_ksqmd_quote_parser
        Boost::system
        Threads::Threads
        nlohmann_json::nlohmann_json
        tinyxml2::tinyxml2
        ft_logger
        ft_data
        ft_utils
    )
endif()
# Platform-specific libraries
if(UNIX)
    target_link_libraries(quote_service PRIVATE dl pthread)
endif()

setup_target_compile_options(quote_service)

# 设置 RPATH 以解决运行时依赖
if(NOT WIN32)
    set_target_properties(quote_service PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
    )
    
    message(STATUS "✓ quote_service RPATH 设置：优先当前目录查找")
endif()

set_target_properties(quote_service PROPERTIES
    OUTPUT_NAME "quote_service"
    POSITION_INDEPENDENT_CODE ON
)

# 构建后检查
add_custom_command(TARGET quote_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "✅ quote_service 构建完成"
    COMMAND ${CMAKE_COMMAND} -E echo "📁 输出位置: ${LIBRARY_OUTPUT_PATH}/libquote_service.so"
    COMMENT "构建完成检查"
)

# =============================================================================
# 可执行文件
# =============================================================================

add_executable(quote_service_exe ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(quote_service_exe PRIVATE
    ft_quote
    ft_data
    ft_logger
    ft_utils
    Boost::system
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(UNIX)
    target_link_libraries(quote_service_exe PRIVATE dl)
endif()

target_include_directories(quote_service_exe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

setup_target_compile_options(quote_service_exe)

set_target_properties(quote_service_exe PROPERTIES
    OUTPUT_NAME "quote_service"
)

if(UNIX)
    add_custom_command(TARGET quote_service_exe POST_BUILD
        COMMAND chmod 755 $<TARGET_FILE:quote_service_exe>
        COMMENT "Setting executable permissions for quote_service"
    )
endif()

install(TARGETS quote_service_exe
    RUNTIME DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
               GROUP_READ GROUP_EXECUTE
               WORLD_READ WORLD_EXECUTE
)

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== Quote 模块构建配置 ===")
message(STATUS "静态库: ft_quote")
message(STATUS "共享库: quote_service")
message(STATUS "基础解析器: ft_base_quote_parser")
if(BUILD_PRODUCTION)
    message(STATUS "生产模式: 使用静态依赖库")
else()
    message(STATUS "开发模式: 使用动态依赖库")
endif()
message(STATUS "================================") 