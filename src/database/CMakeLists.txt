# Database module CMakeLists.txt

# 使用FetchContent下载SQLiteCpp
include(FetchContent)

find_package(Threads REQUIRED)

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

# 下载SQLiteCpp
FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG 3.0.0  # 使用最新稳定版本
)

# 配置SQLiteCpp选项 - 启用内部SQLite以确保静态编译
set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# SQLite配置选项，确保功能完整
set(SQLITE_ENABLE_COLUMN_METADATA ON CACHE BOOL "" FORCE)
set(SQLITE_ENABLE_JSON1 ON CACHE BOOL "" FORCE)
set(SQLITE_ENABLE_RTREE ON CACHE BOOL "" FORCE)
set(SQLITE_ENABLE_FTS4 ON CACHE BOOL "" FORCE)
set(SQLITE_ENABLE_FTS5 ON CACHE BOOL "" FORCE)

# 关键：在编译SQLite之前限制文件偏移量为32位，避免fcntl64
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=32")

FetchContent_MakeAvailable(SQLiteCpp)

# =============================================================================
# 数据库管理器库
# =============================================================================

set(DATABASE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/database_manager.cpp
)

set(DATABASE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/database/database_manager.hpp
)

# 创建数据库管理器静态库（与其他模块命名保持一致）
add_library(ft_database_manager STATIC ${DATABASE_SOURCES} ${DATABASE_HEADERS})

# 设置目标属性
set_target_properties(ft_database_manager PROPERTIES
    OUTPUT_NAME "ft_database_manager"
    POSITION_INDEPENDENT_CODE ON
)

# 设置头文件包含路径
target_include_directories(ft_database_manager PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# 设置数据库管理器的依赖
target_link_libraries(ft_database_manager PUBLIC
    SQLiteCpp
    ft_logger
    ft_utils  # 添加utils依赖，因为可能用到一些工具函数
    Threads::Threads
)

# 设置数据库管理器的编译选项
if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_database_manager)
else()
    target_compile_features(ft_database_manager PUBLIC cxx_std_17)
    target_compile_options(ft_database_manager PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    
    set_target_properties(ft_database_manager PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# 如果是生产模式，启用静态链接
if(BUILD_PRODUCTION)
    target_compile_definitions(ft_database_manager PRIVATE FT_PRODUCTION_BUILD=1)
    if(NOT WIN32)
        target_compile_options(ft_database_manager PRIVATE -fvisibility=hidden)
    endif()
    message(STATUS "生产模式：ft_db_manager 使用静态链接")
else()
    message(STATUS "开发模式：ft_db_manager 正常编译")
endif()

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== Database 模块构建配置 ===")
message(STATUS "静态库: ft_database_manager")
message(STATUS "SQLiteCpp: 内部版本")
message(STATUS "输出名称: ft_database_manager")
message(STATUS "包含路径: ${CMAKE_SOURCE_DIR}/include")
message(STATUS "====================================")

# 构建后检查
add_custom_command(TARGET ft_database_manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "✅ ft_database_manager 构建完成"
    COMMAND ${CMAKE_COMMAND} -E echo "📁 输出位置: ${LIBRARY_OUTPUT_PATH}/libft_db_manager.a"
    COMMENT "数据库模块构建完成检查"
)

# =============================================================================
# 安装配置（可选）
# =============================================================================

# 导出库
set_target_properties(ft_database_manager PROPERTIES
    EXPORT_NAME DatabaseManager
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 安装库文件（如果需要）
if(INSTALL_LIBRARIES)
    install(TARGETS ft_database_manager
        EXPORT FuniqTradingTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )

    # 安装头文件
    install(FILES
        ${CMAKE_SOURCE_DIR}/include/database/database_manager.hpp
        DESTINATION include/database
    )
endif()