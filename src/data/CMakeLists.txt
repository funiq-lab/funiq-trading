# Data module - manages its own dependencies

# Find required packages for this module
# 使用全局依赖处理函数（如果存在）
if(COMMAND setup_boost_dependency)
    setup_boost_dependency()
else()
    find_package(Boost REQUIRED COMPONENTS system thread)
endif()

find_package(Threads REQUIRED)

set(DATA_SOURCES
    data_manager.cpp
    shared_memory.cpp
)

set(DATA_HEADERS
    ${CMAKE_SOURCE_DIR}/include/data/data_manager.h
    ${CMAKE_SOURCE_DIR}/include/data/shared_memory.h
)

# Create static library
add_library(ft_data STATIC ${DATA_SOURCES} ${DATA_HEADERS})

# Set include directories
target_include_directories(ft_data PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies using modern CMake targets
target_link_libraries(ft_data PUBLIC
    # Internal modules
    ft_utils
    ft_logger
    # External libraries
    Boost::system
    Boost::thread
    Threads::Threads
)

# Platform-specific libraries
if(UNIX)
    target_link_libraries(ft_data PUBLIC rt)  # Real-time library for shared memory
endif()

# Apply common compile options
setup_target_compile_options(ft_data)

# Set output name
set_target_properties(ft_data PROPERTIES
    OUTPUT_NAME "ft_data"
) 