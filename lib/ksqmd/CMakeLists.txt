# KSQMD library CMakeLists.txt - 简化版本，不创建目标

# 检查必需文件
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    message(WARNING "KSQMD include directory not found: ${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libksHsUdp.so)
    message(FATAL_ERROR "KSQMD library not found: ${CMAKE_CURRENT_SOURCE_DIR}/libksHsUdp.so")
endif()

# 打印信息，但不创建任何目标
message(STATUS "KSQMD library found:")
message(STATUS "  - Include directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "  - Library file: ${CMAKE_CURRENT_SOURCE_DIR}/libksHsUdp.so")
message(STATUS "  - Will be linked directly by modules that need it")
message(STATUS "  - Build type: ${CMAKE_BUILD_TYPE}") 