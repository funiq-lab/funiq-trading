# KSQMD library CMakeLists.txt - 简化版本，不创建目标

# SDK files are obtained separately from the provider and ignored by Git.
foreach(required_file IN ITEMS "include/KsHSInterfaceEx.h" "libksHsUdp.so")
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${required_file}")
        message(FATAL_ERROR
            "KSQMD SDK is not bundled with this repository. "
            "Obtain it from your provider and follow lib/ksqmd/README.md. "
            "Missing: ${CMAKE_CURRENT_LIST_DIR}/${required_file}")
    endif()
endforeach()

# 打印信息，但不创建任何目标
message(STATUS "KSQMD library found:")
message(STATUS "  - Include directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "  - Library file: ${CMAKE_CURRENT_SOURCE_DIR}/libksHsUdp.so")
message(STATUS "  - Will be linked directly by modules that need it")
message(STATUS "  - Build type: ${CMAKE_BUILD_TYPE}") 