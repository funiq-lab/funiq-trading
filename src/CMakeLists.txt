# Main CMakeLists.txt - 主可执行文件

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
# 可执行文件 - 集成的交易主程序
# =============================================================================

add_executable(ft_trading_client 
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(ft_trading_client PRIVATE
    ft_network
    ft_algorithm
    quote_service
    trade_service
    strategy_service
    ft_data
    ft_logger
    ft_utils
    ft_database_manager
    Boost::system
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(UNIX)
    target_link_libraries(ft_trading_client PRIVATE dl)
    if(NOT BUILD_PRODUCTION)
        set_target_properties(ft_trading_client PROPERTIES
            LINK_FLAGS "-ldl -lpthread -Wl,--no-as-needed"
        )
    endif()
endif()

target_include_directories(ft_trading_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_trading_client)
else()
    target_compile_features(ft_trading_client PUBLIC cxx_std_17)
    target_compile_options(ft_trading_client PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

set_target_properties(ft_trading_client PROPERTIES
    OUTPUT_NAME "ft_trading_client"
)

if(UNIX)
    add_custom_command(TARGET ft_trading_client POST_BUILD
        COMMAND chmod 755 $<TARGET_FILE:ft_trading_client>
        COMMENT "Setting executable permissions for ft_trading_client"
    )
endif()

install(TARGETS ft_trading_client
    RUNTIME DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
               GROUP_READ GROUP_EXECUTE
               WORLD_READ WORLD_EXECUTE
)

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== 主程序构建配置 ===")
message(STATUS "可执行文件: ft_trading_client")
message(STATUS "主入口: main.cpp")
message(STATUS "依赖模块: network, quote, trade, strategy")
message(STATUS "输出目录: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "================================")