# Migration CLI Tool
cmake_minimum_required(VERSION 3.16)

# Create the migration CLI executable
add_executable(ft_migration_cli main.cpp migration_manager.cpp)

# Set target properties
set_target_properties(ft_migration_cli PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "ft_migration"
)

# Include directories
target_include_directories(ft_migration_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(ft_migration_cli PRIVATE
    SQLiteCpp
    ft_logger
    ${CMAKE_DL_LIBS}
)

# Compiler features and options
target_compile_features(ft_migration_cli PRIVATE cxx_std_17)
target_compile_options(ft_migration_cli PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Set target properties
set_target_properties(ft_migration_cli PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
)

# Production build settings
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(ft_migration_cli PRIVATE FT_PRODUCTION_BUILD)
    target_compile_options(ft_migration_cli PRIVATE -fvisibility=hidden)
    message(STATUS "Migration CLI: Production build enabled")
else()
    message(STATUS "Migration CLI: Development build")
endif()

# Build information
message(STATUS "Migration CLI executable: ${CMAKE_CURRENT_BINARY_DIR}/ft_migration")

# Post-build check
add_custom_command(TARGET ft_migration_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Migration CLI build completed: $<TARGET_FILE:ft_migration_cli>"
    COMMENT "Migration CLI build completed"
)

# Installation
install(TARGETS ft_migration_cli
    RUNTIME DESTINATION bin
    COMPONENT migration_cli
)

# Install example config file
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/migration.ini.example")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/migration.ini.example"
        DESTINATION share/ft_migration
        COMPONENT migration_cli
    )
endif()