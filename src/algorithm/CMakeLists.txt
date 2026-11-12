# Algorithm module - contains multiple trading algorithms

find_package(Threads REQUIRED)

# Handle dependencies
if(DEFINED setup_nlohmann_json_dependency)
    setup_nlohmann_json_dependency()
else()
    find_package(nlohmann_json REQUIRED)
endif()

# Algorithm sources - include all algorithms
set(ALGORITHM_SOURCES 
    twap.cpp
    # 未来可以添加其他算法：
    # vwap.cpp
    # pov.cpp
    # iceberg.cpp
)

set(ALGORITHM_HEADERS 
    ${CMAKE_SOURCE_DIR}/include/algorithm/twap.h
    # 未来可以添加其他算法头文件：
    # ${CMAKE_SOURCE_DIR}/include/algorithm/vwap.h
    # ${CMAKE_SOURCE_DIR}/include/algorithm/pov.h
    # ${CMAKE_SOURCE_DIR}/include/algorithm/iceberg.h
)

# Create static library
add_library(ft_algorithm STATIC ${ALGORITHM_SOURCES} ${ALGORITHM_HEADERS})

# Set include directories
target_include_directories(ft_algorithm PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(ft_algorithm PUBLIC 
    ft_logger      # 日志库
    ft_utils       # 工具库（包含trading_time）
    ft_database_manager    # 数据库库
    Threads::Threads
)

# Platform-specific definitions
if(MSVC)
    target_compile_definitions(ft_algorithm PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0601
        NOMINMAX
    )
endif()

# Compiler-specific flags with Debug/Release distinction
if(MSVC)
    target_compile_options(ft_algorithm PRIVATE
        /W4 /permissive-
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    target_compile_options(ft_algorithm PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3 -DNDEBUG -s>
        $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
    )
endif()

# Set properties
set_target_properties(ft_algorithm PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ft_algorithm"
)