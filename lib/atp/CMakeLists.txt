# ATP library CMakeLists.txt - 简化版本，不创建目标

# SDK files are obtained separately from the provider and ignored by Git.
foreach(required_file IN ITEMS "include/atp_quant_api.h" "lib/libatpquantapi.so")
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${required_file}")
        message(FATAL_ERROR
            "ATP SDK is not bundled with this repository. "
            "Obtain it from your provider and follow lib/atp/README.md. "
            "Missing: ${CMAKE_CURRENT_LIST_DIR}/${required_file}")
    endif()
endforeach()

# 检查ATP特有的依赖文件
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/rsa_public_key.pem)
    message(WARNING "ATP RSA public key not found: ${CMAKE_CURRENT_SOURCE_DIR}/lib/rsa_public_key.pem")
    message(WARNING "ATP 可能无法正常工作，请确保 rsa_public_key.pem 文件存在")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/atp_quant_api.lic)
    message(STATUS "  - License file: ${CMAKE_CURRENT_SOURCE_DIR}/lib/atp_quant_api.lic")
else()
    message(STATUS "  - License file: Not found (可能不需要)")
endif()

# 打印信息，但不创建任何目标
message(STATUS "ATP library found:")
message(STATUS "  - Include directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "  - Library file: ${CMAKE_CURRENT_SOURCE_DIR}/lib/libatpquantapi.so")
message(STATUS "  - RSA public key: ${CMAKE_CURRENT_SOURCE_DIR}/lib/rsa_public_key.pem")
message(STATUS "  - Will be linked directly by modules that need it")
message(STATUS "  - Build type: ${CMAKE_BUILD_TYPE}") 