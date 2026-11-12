# Network Module CMakeLists.txt - 网络模块静态库

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
# 静态库 - 网络服务模块
# =============================================================================

add_library(ft_network STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/http/base_api_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/websocket/base_websocket_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/http/system_api_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/http/trade_api_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/http/quote_api_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/http/strategy_api_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/error/error.cpp
)

target_link_libraries(ft_network PRIVATE
    ft_logger
    ft_utils
    ft_database_manager
    Boost::system
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(UNIX)
    target_link_libraries(ft_network PRIVATE dl)
    if(NOT BUILD_PRODUCTION)
        set_target_properties(ft_network PROPERTIES
            LINK_FLAGS "-ldl -lpthread -Wl,--no-as-needed"
        )
    endif()
endif()

target_include_directories(ft_network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_network)
else()
    target_compile_features(ft_network PUBLIC cxx_std_17)
    target_compile_options(ft_network PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== Network 模块构建配置 ===")
message(STATUS "静态库: ft_network")
message(STATUS "模块类型: 网络服务模块")
message(STATUS "依赖: logger, utils, boost, nlohmann_json")
message(STATUS "================================")