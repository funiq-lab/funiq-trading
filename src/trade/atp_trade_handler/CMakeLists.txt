# ATP Trade Handler

# Find required packages for this module
find_package(Threads REQUIRED)

# =============================================================================
# ATP 库查找函数
# =============================================================================

function(find_atp_library)
    # 定义搜索路径优先级
    set(ATP_SEARCH_PATHS
        # 项目相对路径（首选）
        ${CMAKE_SOURCE_DIR}/lib/atp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/atp
        
        # 环境变量指定路径
        $ENV{ATP_ROOT}
        
        # 标准安装路径
        ${CMAKE_PREFIX_PATH}/lib/atp
        /usr/local/lib/atp
        /opt/atp
    )
    
    message(STATUS "🔍 搜索 ATP 库...")
    
    # 查找ATP库路径
    foreach(search_path ${ATP_SEARCH_PATHS})
        if(EXISTS "${search_path}")
            message(STATUS "  ✓ 找到 ATP 路径: ${search_path}")
            
            # 查找头文件
            if(EXISTS "${search_path}/include/atp_quant_api.h")
                set(ATP_INCLUDE_DIR "${search_path}/include" PARENT_SCOPE)
                message(STATUS "  ✓ 找到 ATP 头文件: ${search_path}/include")
            endif()
            
            # 查找库文件
            if(EXISTS "${search_path}/lib/libatpquantapi.so")
                set(ATP_LIBRARY "${search_path}/lib/libatpquantapi.so" PARENT_SCOPE)
                set(ATP_LIBRARY_DIR "${search_path}/lib" PARENT_SCOPE)
                message(STATUS "  ✓ 找到 ATP 库: ${search_path}/lib/libatpquantapi.so")
                
                # 查找依赖文件
                if(EXISTS "${search_path}/lib/rsa_public_key.pem")
                    set(ATP_RSA_KEY "${search_path}/lib/rsa_public_key.pem" PARENT_SCOPE)
                    message(STATUS "  ✓ 找到 RSA 公钥: ${search_path}/lib/rsa_public_key.pem")
                endif()
                
                if(EXISTS "${search_path}/lib/atp_quant_api.lic")
                    set(ATP_LICENSE "${search_path}/lib/atp_quant_api.lic" PARENT_SCOPE)
                    message(STATUS "  ✓ 找到 ATP 许可证: ${search_path}/lib/atp_quant_api.lic")
                endif()
                
                return()
            endif()
        endif()
    endforeach()
    
    message(FATAL_ERROR "❌ ATP 库未找到，请确保以下路径之一包含 ATP 库:\n  ${ATP_SEARCH_PATHS}")
endfunction()

# 调用查找函数
find_atp_library()

# 验证找到的库
if(NOT ATP_INCLUDE_DIR OR NOT ATP_LIBRARY)
    message(FATAL_ERROR "❌ ATP 库组件未找到")
endif()

message(STATUS "📦 ATP 库配置:")
message(STATUS "  Include: ${ATP_INCLUDE_DIR}")
message(STATUS "  Library: ${ATP_LIBRARY}")
message(STATUS "  Library Dir: ${ATP_LIBRARY_DIR}")

# =============================================================================
# 第三方依赖管理 - 使用全局函数
# =============================================================================

# 使用全局 tinyxml2 依赖处理函数
if(COMMAND setup_tinyxml2_dependency)
    setup_tinyxml2_dependency()
else()
    message(FATAL_ERROR "tinyxml2 依赖处理函数未找到，请确保全局依赖管理已配置")
endif()

# =============================================================================
# 目标定义
# =============================================================================

# 创建ATP处理器动态库
add_library(ft_atp_trade_handler SHARED
    atp_trade_handler.cpp
)

# 设置目标属性
set_target_properties(ft_atp_trade_handler PROPERTIES
    OUTPUT_NAME "ft_atp_trade_handler"
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 包含目录
target_include_directories(ft_atp_trade_handler PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ATP_INCLUDE_DIR}
)

# =============================================================================
# 创建 ATP 导入目标
# =============================================================================

if(NOT TARGET atp::atpquantapi)
    add_library(atp::atpquantapi SHARED IMPORTED)
    set_target_properties(atp::atpquantapi PROPERTIES
        IMPORTED_LOCATION ${ATP_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${ATP_INCLUDE_DIR}
    )
endif()

# 链接库
target_link_libraries(ft_atp_trade_handler PRIVATE
    atp::atpquantapi
    ft_base_trade_handler
    ft_logger
    ft_utils
    tinyxml2::tinyxml2
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# 使用全局编译选项设置
if(COMMAND setup_target_compile_options)
    setup_target_compile_options(ft_atp_trade_handler)
else()
    target_compile_features(ft_atp_trade_handler PUBLIC cxx_std_17)
    target_compile_options(ft_atp_trade_handler PRIVATE
        -Wall -Wextra
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
    )
    message(WARNING "使用本地编译选项设置，建议使用全局编译选项管理")
endif()

# 预处理器定义
target_compile_definitions(ft_atp_trade_handler PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
)

# =============================================================================
# 设置 RPATH 以解决运行时依赖查找问题
# =============================================================================

# 关键：设置 RPATH 让库在运行时优先从当前目录查找依赖
if(NOT WIN32)
    set_target_properties(ft_atp_trade_handler PROPERTIES
        # 在构建时使用的 RPATH - 优先当前目录
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib:${ATP_LIBRARY_DIR}"
        
        # 安装时使用的 RPATH - 优先当前目录
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib:${ATP_LIBRARY_DIR}"
    )
    
    message(STATUS "✓ ft_atp_trade_handler RPATH 设置：优先当前目录查找")
endif()

# =============================================================================
# ATP 库部署策略优化
# =============================================================================

# 移除条件判断，始终复制ATP库到输出目录
add_custom_command(TARGET ft_atp_trade_handler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "📦 复制 ATP 运行时依赖..."
    
    # 复制 ATP 库到输出目录
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${ATP_LIBRARY} 
            ${LIBRARY_OUTPUT_PATH}/
    
    # 显示复制的文件
    COMMAND ${CMAKE_COMMAND} -E echo "✅ 已复制: ${ATP_LIBRARY} -> ${LIBRARY_OUTPUT_PATH}/"
    
    COMMENT "复制 ATP 依赖库到输出目录"
    VERBATIM
)

# 复制 RSA 公钥文件（如果存在）
if(DEFINED ATP_RSA_KEY AND EXISTS ${ATP_RSA_KEY})
    add_custom_command(TARGET ft_atp_trade_handler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ATP_RSA_KEY}
            ${LIBRARY_OUTPUT_PATH}/
        COMMAND ${CMAKE_COMMAND} -E echo "✅ 已复制: ${ATP_RSA_KEY} -> ${LIBRARY_OUTPUT_PATH}/"
        COMMENT "复制ATP RSA公钥文件"
        VERBATIM
    )
endif()

# 复制许可证文件（如果存在）
if(DEFINED ATP_LICENSE AND EXISTS ${ATP_LICENSE})
    add_custom_command(TARGET ft_atp_trade_handler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ATP_LICENSE}
            ${LIBRARY_OUTPUT_PATH}/
        COMMAND ${CMAKE_COMMAND} -E echo "✅ 已复制: ${ATP_LICENSE} -> ${LIBRARY_OUTPUT_PATH}/"
        COMMENT "复制ATP许可证文件"
        VERBATIM
    )
endif()

# =============================================================================
# 安装配置
# =============================================================================

# 安装目标
install(TARGETS ft_atp_trade_handler
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# 根据模式决定是否安装ATP运行时依赖
if(NOT BUILD_PRODUCTION)
    # 只在非生产模式下安装ATP运行时依赖
    install(FILES ${ATP_LIBRARY}
        DESTINATION lib
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE
    )
    
    # 安装ATP配置文件（如果存在）
    if(DEFINED ATP_RSA_KEY AND EXISTS ${ATP_RSA_KEY})
        install(FILES ${ATP_RSA_KEY}
            DESTINATION lib
        )
    endif()
    
    if(DEFINED ATP_LICENSE AND EXISTS ${ATP_LICENSE})
        install(FILES ${ATP_LICENSE}
            DESTINATION lib
        )
    endif()
endif()

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== ATP Trade Handler 配置 ===")
message(STATUS "✓ ATP 库: ${ATP_LIBRARY}")
if(BUILD_PRODUCTION)
    message(STATUS "✓ 生产模式: 使用RPATH引用ATP库")
    message(STATUS "  📁 RPATH搜索路径: $ORIGIN, $ORIGIN/lib, ${ATP_LIBRARY_DIR}")
else()
    message(STATUS "✓ 开发模式: ATP库将复制到输出目录")
    message(STATUS "✓ 运行时依赖: 将自动复制到输出目录")
endif()
message(STATUS "✓ RPATH 设置: 优先从当前目录查找依赖")
message(STATUS "🚀 部署方式: 将所有 .so 文件放在同一目录即可")
message(STATUS "✅ ATP Trade Handler configured successfully")
