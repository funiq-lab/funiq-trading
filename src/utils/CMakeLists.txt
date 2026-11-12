# Utils module - manages its own dependencies

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


set(UTILS_SOURCES
    config.cpp
    time.cpp
    mock.cpp
    path_matcher.cpp
    url_parser.cpp
    cron_scheduler.cpp
    trading_time.cpp
    auto_scheduler.cpp
    performance_stats_collector.cpp
    trade_utils.cpp
)

set(UTILS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/utils/config.h
    ${CMAKE_SOURCE_DIR}/include/utils/string.hpp
    ${CMAKE_SOURCE_DIR}/include/utils/time.h
    ${CMAKE_SOURCE_DIR}/include/utils/dll_helper.hpp
    ${CMAKE_SOURCE_DIR}/include/utils/file.hpp
    ${CMAKE_SOURCE_DIR}/include/utils/mock.h
    ${CMAKE_SOURCE_DIR}/include/utils/path_matcher.h
    ${CMAKE_SOURCE_DIR}/include/utils/url_parser.h
    ${CMAKE_SOURCE_DIR}/include/utils/cron_scheduler.h
    ${CMAKE_SOURCE_DIR}/include/utils/trading_time.h
    ${CMAKE_SOURCE_DIR}/include/utils/auto_scheduler.h
    ${CMAKE_SOURCE_DIR}/include/utils/INI.h
    ${CMAKE_SOURCE_DIR}/include/utils/performance_stats_collector.h
    ${CMAKE_SOURCE_DIR}/include/utils/trade_utils.h
)

# Create static library
add_library(ft_utils STATIC ${UTILS_SOURCES} ${UTILS_HEADERS})

# Set include directories
target_include_directories(ft_utils PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(ft_utils PUBLIC
    ft_logger
)


# Platform-specific definitions
if(MSVC)
    target_compile_definitions(ft_utils PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0601
        NOMINMAX
    )
endif()

# Compiler-specific flags with Debug/Release distinction
if(MSVC)
    target_compile_options(ft_utils PRIVATE
        /W4 /permissive-
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    target_compile_options(ft_utils PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3 -DNDEBUG -s>
        $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
    )
endif()

# Set properties
set_target_properties(ft_utils PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ft_utils"
)