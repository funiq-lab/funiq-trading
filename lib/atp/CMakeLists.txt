# ATP library CMakeLists.txt - 简化版本，不创建目标

# 检查必需文件
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    message(WARNING "ATP include directory not found: ${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/libatpquantapi.so)
    message(FATAL_ERROR "ATP library not found: ${CMAKE_CURRENT_SOURCE_DIR}/lib/libatpquantapi.so")
endif()

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