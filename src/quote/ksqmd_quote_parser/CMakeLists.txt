# KSQMD Quote Parser - 运行时依赖友好配置

# Find required packages for this module
find_package(Threads REQUIRED)

# =============================================================================
# KSQMD 库查找函数 - 简化版本
# =============================================================================

function(find_ksqmd_library)
    # 定义搜索路径优先级
    set(KSQMD_SEARCH_PATHS
        # 项目相对路径（首选）
        ${CMAKE_SOURCE_DIR}/lib/ksqmd
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/ksqmd
        
        # 环境变量指定路径
        $ENV{KSQMD_ROOT}
        
        # 标准安装路径
        ${CMAKE_PREFIX_PATH}/lib/ksqmd
        /usr/local/lib/ksqmd
        /opt/ksqmd
    )
    
    message(STATUS "搜索 KSQMD 库...")
    
    # 查找头文件
    find_path(KSQMD_INCLUDE_DIR
        NAMES KsHSDef.h KsHSInterfaceEx.h
        PATHS ${KSQMD_SEARCH_PATHS}
        PATH_SUFFIXES include
        NO_DEFAULT_PATH
    )
    
    # 查找动态库
    find_library(KSQMD_LIBRARY
        NAMES ksHsUdp libksHsUdp
        PATHS ${KSQMD_SEARCH_PATHS}
        PATH_SUFFIXES 
            lib 
            . 
            gcc_high_version_so
        NO_DEFAULT_PATH
    )
    
    # 验证找到的文件
    if(KSQMD_INCLUDE_DIR AND KSQMD_LIBRARY)
        if(NOT EXISTS ${KSQMD_INCLUDE_DIR}/KsHSDef.h)
            message(FATAL_ERROR "KSQMD include directory found but KsHSDef.h missing: ${KSQMD_INCLUDE_DIR}")
        endif()
        
        if(NOT EXISTS ${KSQMD_LIBRARY})
            message(FATAL_ERROR "KSQMD library path invalid: ${KSQMD_LIBRARY}")
        endif()
        
        set(KSQMD_FOUND TRUE PARENT_SCOPE)
        set(KSQMD_INCLUDE_DIR ${KSQMD_INCLUDE_DIR} PARENT_SCOPE)
        set(KSQMD_LIBRARY ${KSQMD_LIBRARY} PARENT_SCOPE)
        
        message(STATUS "✓ 找到 KSQMD 库: ${KSQMD_LIBRARY}")
        message(STATUS "✓ 找到 KSQMD 头文件: ${KSQMD_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "KSQMD 库未找到，请确保以下路径之一包含 KSQMD 库:\n  ${KSQMD_SEARCH_PATHS}")
    endif()
endfunction()

# 调用查找函数
find_ksqmd_library()

# =============================================================================
# 第三方依赖管理
# =============================================================================

# tinyxml2 依赖处理 - 使用内置源码
function(setup_tinyxml2_dependency)
    # 检查本地源码文件
    set(TINYXML2_LOCAL_DIR ${CMAKE_SOURCE_DIR}/lib/tinyxml2)
    
    if(EXISTS ${TINYXML2_LOCAL_DIR}/tinyxml2.h AND EXISTS ${TINYXML2_LOCAL_DIR}/tinyxml2.cpp)
        message(STATUS "✓ KSQMD 模块使用内置 tinyxml2 源码: ${TINYXML2_LOCAL_DIR}")
        
        # 检查是否已经被父级创建
        if(NOT TARGET tinyxml2_builtin)
            # 创建内置 tinyxml2 库
            add_library(tinyxml2_builtin STATIC 
                ${TINYXML2_LOCAL_DIR}/tinyxml2.cpp
            )
            
            target_include_directories(tinyxml2_builtin PUBLIC
                ${TINYXML2_LOCAL_DIR}
            )
            
            # 设置编译选项
            if(MSVC)
                target_compile_definitions(tinyxml2_builtin PRIVATE
                    _CRT_SECURE_NO_WARNINGS
                )
            endif()
            
            # 静态库设置
            set_target_properties(tinyxml2_builtin PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                OUTPUT_NAME "tinyxml2"
            )
        endif()
        
        # 创建标准别名（如果不存在）
        if(NOT TARGET tinyxml2::tinyxml2)
            add_library(tinyxml2::tinyxml2 ALIAS tinyxml2_builtin)
        endif()
        
        return()
    endif()
    
    # 后备方案：尝试系统库
    find_package(tinyxml2 QUIET)
    if(tinyxml2_FOUND)
        message(STATUS "✓ 找到系统 tinyxml2 库")
        return()
    endif()
    
    # 尝试 pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TINYXML2 QUIET tinyxml2)
        if(TINYXML2_FOUND)
            add_library(tinyxml2_pkg INTERFACE)
            target_include_directories(tinyxml2_pkg INTERFACE ${TINYXML2_INCLUDE_DIRS})
            target_link_libraries(tinyxml2_pkg INTERFACE ${TINYXML2_LIBRARIES})
            add_library(tinyxml2::tinyxml2 ALIAS tinyxml2_pkg)
            message(STATUS "✓ 找到 pkg-config tinyxml2 库")
            return()
        endif()
    endif()
    
    # 手动查找
    find_path(TINYXML2_INCLUDE_DIR tinyxml2.h)
    find_library(TINYXML2_LIBRARY tinyxml2)
    
    if(TINYXML2_INCLUDE_DIR AND TINYXML2_LIBRARY)
        add_library(tinyxml2_manual INTERFACE)
        target_include_directories(tinyxml2_manual INTERFACE ${TINYXML2_INCLUDE_DIR})
        target_link_libraries(tinyxml2_manual INTERFACE ${TINYXML2_LIBRARY})
        add_library(tinyxml2::tinyxml2 ALIAS tinyxml2_manual)
        message(STATUS "✓ 找到手动搜索的 tinyxml2 库")
        return()
    endif()
    
    message(FATAL_ERROR "tinyxml2 not found! 请确保 ${TINYXML2_LOCAL_DIR} 包含 tinyxml2.h 和 tinyxml2.cpp")
endfunction()

# 设置 tinyxml2
setup_tinyxml2_dependency()

# =============================================================================
# 构建配置
# =============================================================================

set(KSQMD_PARSER_SOURCES
    ksqmd_quote_parser.cpp
    xml_config_ex.cpp
)

set(KSQMD_PARSER_HEADERS
    ksqmd_quote_parser.h
    xml_config_ex.h
)

# 创建库 - 永远构建为共享库，因为需要动态加载
add_library(ft_ksqmd_quote_parser SHARED ${KSQMD_PARSER_SOURCES} ${KSQMD_PARSER_HEADERS})
message(STATUS "KSQMD Quote Parser: 构建为共享库（用于动态加载）")

# Set include directories
target_include_directories(ft_ksqmd_quote_parser PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${KSQMD_INCLUDE_DIR}
)

# =============================================================================
# 创建 KSQMD 导入目标
# =============================================================================

if(NOT TARGET ksqmd::ksHsUdp)
    add_library(ksqmd::ksHsUdp SHARED IMPORTED)
    set_target_properties(ksqmd::ksHsUdp PROPERTIES
        IMPORTED_LOCATION ${KSQMD_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${KSQMD_INCLUDE_DIR}
    )
endif()

# Link dependencies - 根据模式决定静态还是动态链接
if(BUILD_PRODUCTION)
    # 生产模式：内部依赖静态链接，第三方库处理依赖问题
    target_link_libraries(ft_ksqmd_quote_parser PRIVATE
        # Internal modules - 静态链接以减少依赖
        ft_utils
        ft_base_quote_parser  
        ft_data
        ft_logger
        # External libraries
        ksqmd::ksHsUdp
        tinyxml2::tinyxml2
        Threads::Threads
    )
    
    message(STATUS "✓ ft_ksqmd_quote_parser: 生产模式 - 内部依赖静态链接")
else()
    # 开发模式：正常动态链接
    target_link_libraries(ft_ksqmd_quote_parser PUBLIC
        # Internal modules
        ft_utils
        ft_base_quote_parser
        ft_data
        ft_logger
        # External libraries
        ksqmd::ksHsUdp
        tinyxml2::tinyxml2
        Threads::Threads
    )
    
    message(STATUS "✓ ft_ksqmd_quote_parser: 开发模式 - 动态链接")
endif()

# Platform-specific libraries
if(UNIX)
    target_link_libraries(ft_ksqmd_quote_parser PRIVATE dl pthread)
endif()

# Apply common compile options
setup_target_compile_options(ft_ksqmd_quote_parser)

# =============================================================================
# 设置 RPATH 以解决运行时依赖查找问题
# =============================================================================

# 移除版本设置，简化库名
set_target_properties(ft_ksqmd_quote_parser PROPERTIES
    OUTPUT_NAME "ft_ksqmd_quote_parser"
    POSITION_INDEPENDENT_CODE ON
)

# 关键：设置 RPATH 让库在运行时优先从当前目录查找依赖
if(NOT WIN32)
    set_target_properties(ft_ksqmd_quote_parser PROPERTIES
        # 在构建时使用的 RPATH - 优先当前目录
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
        
        # 安装时使用的 RPATH - 优先当前目录
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib:$ORIGIN/..:$ORIGIN/../lib"
    )
    
    message(STATUS "✓ ft_ksqmd_quote_parser RPATH 设置：优先当前目录查找")
endif()

# =============================================================================
# 构建后处理：自动复制依赖库
# =============================================================================

# 添加自定义命令来复制 KSQMD 库到输出目录
add_custom_command(TARGET ft_ksqmd_quote_parser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "📦 处理 KSQMD 运行时依赖..."
    
    # 复制 KSQMD 库到输出目录
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${KSQMD_LIBRARY} 
            ${LIBRARY_OUTPUT_PATH}/
    
    # 显示复制的文件
    COMMAND ${CMAKE_COMMAND} -E echo "✅ 已复制: ${KSQMD_LIBRARY} -> ${LIBRARY_OUTPUT_PATH}/"
    
    COMMENT "复制 KSQMD 依赖库到输出目录"
    VERBATIM
)

# =============================================================================
# 安装规则
# =============================================================================

install(TARGETS ft_ksqmd_quote_parser
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# 生产模式安装 KSQMD 依赖库
if(BUILD_PRODUCTION)
    install(FILES ${KSQMD_LIBRARY}
        DESTINATION lib
    )
endif()

# =============================================================================
# 构建信息
# =============================================================================

message(STATUS "=== KSQMD Quote Parser 配置 ===")
message(STATUS "✓ KSQMD 库: ${KSQMD_LIBRARY}")
message(STATUS "✓ 运行时依赖: 将自动复制到输出目录")
message(STATUS "✓ RPATH 设置: 优先从当前目录查找依赖")
message(STATUS "  📁 搜索顺序:")
message(STATUS "    1. 当前目录（与 .so 文件同目录）")
message(STATUS "    2. 当前目录/lib 子目录")
message(STATUS "    3. 父目录")
message(STATUS "    4. 父目录/lib 子目录")
message(STATUS "🚀 部署方式: 将所有 .so 文件放在同一目录即可") 