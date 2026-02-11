cmake_minimum_required(VERSION 3.10)

# ==============================
# 0) 项目名称（可配置）
# ==============================
# 说明：通过 -DPROJECT_NAME_OVERRIDE=YourName 传入即可修改项目/可执行文件名称。
# 示例：cmake -S . -B build -DPROJECT_NAME_OVERRIDE=MyApp
set(PROJECT_NAME_OVERRIDE "UpgradeAndTest" CACHE STRING "Project name (also used as executable target name)")
project(${PROJECT_NAME_OVERRIDE} LANGUAGES CXX)

# ==============================
# 1) 基础编译选项
# ==============================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 说明：当前工程是单配置（Debug/Release 通过 CMAKE_BUILD_TYPE 切换）。
# Windows 下用 Makefiles/Ninja 时通常是单配置；若切到多配置生成器，CMAKE_BUILD_TYPE 可能为空。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose build type" FORCE)
endif()
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Configs" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================
# 2) 仅交叉编译（强制目标为 aarch64 Linux）
# ==============================
# 说明：本工程不再支持 host/native 编译流程，默认始终构建 aarch64 Linux 目标。
# 你可以通过以下方式提供工具链信息（推荐由脚本 build.ps1 传入）：
# - 通过 CMake Cache 变量：-DCROSS_SYSROOT=... -DCROSS_CXX_COMPILER=...（可选 -DCROSS_C_COMPILER=...）
# - 或直接用标准变量：-DCMAKE_SYSROOT=... -DCMAKE_CXX_COMPILER=...
# - 或提供 toolchain 文件：-DCMAKE_TOOLCHAIN_FILE=...

set(CMAKE_SYSTEM_NAME Linux CACHE STRING "Target system" FORCE)
set(CMAKE_SYSTEM_PROCESSOR aarch64 CACHE STRING "Target architecture" FORCE)

# 当没有显式指定 toolchain 文件时，使用 sysroot/编译器参数进行交叉编译配置。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{OECORE_CMAKE_TOOLCHAIN_FILE})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{OECORE_CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH "Toolchain" FORCE)
endif()

set(CROSS_SYSROOT "${CROSS_SYSROOT}" CACHE PATH "Cross sysroot")
set(CROSS_C_COMPILER "${CROSS_C_COMPILER}" CACHE STRING "Cross C compiler")
set(CROSS_CXX_COMPILER "${CROSS_CXX_COMPILER}" CACHE STRING "Cross CXX compiler")

# 从环境变量回填（便于脚本用 env 驱动）。
if(NOT CROSS_SYSROOT AND DEFINED ENV{CROSS_SYSROOT})
    set(CROSS_SYSROOT "$ENV{CROSS_SYSROOT}")
endif()
if(NOT CROSS_C_COMPILER AND DEFINED ENV{CROSS_C_COMPILER})
    set(CROSS_C_COMPILER "$ENV{CROSS_C_COMPILER}")
endif()
if(NOT CROSS_CXX_COMPILER AND DEFINED ENV{CROSS_CXX_COMPILER})
    set(CROSS_CXX_COMPILER "$ENV{CROSS_CXX_COMPILER}")
endif()

# 兜底查找常见交叉编译器名称（仅在未显式传入时使用）。
if(NOT CROSS_CXX_COMPILER OR CROSS_CXX_COMPILER STREQUAL "")
    find_program(CROSS_CXX_COMPILER NAMES aarch64-none-linux-gnu-g++ aarch64-ucas-linux-g++ aarch64-linux-gnu-g++)
endif()
if(NOT CROSS_C_COMPILER OR CROSS_C_COMPILER STREQUAL "")
    find_program(CROSS_C_COMPILER NAMES aarch64-none-linux-gnu-gcc aarch64-ucas-linux-gcc aarch64-linux-gnu-gcc)
endif()

# sysroot 如果未提供，且旧默认路径存在，则沿用旧默认；否则要求用户显式提供。
if((NOT CROSS_SYSROOT OR CROSS_SYSROOT STREQUAL "") AND EXISTS "/opt/wanghuo/v2.0.0-rc4/sysroots/armv8a-ucas-linux")
    set(CROSS_SYSROOT "/opt/wanghuo/v2.0.0-rc4/sysroots/armv8a-ucas-linux")
endif()

if((NOT CROSS_SYSROOT OR CROSS_SYSROOT STREQUAL "") AND (NOT CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL ""))
    message(FATAL_ERROR "Cross sysroot is not set. Provide -DCROSS_SYSROOT=... or -DCMAKE_SYSROOT=... (build.ps1 will set it automatically).")
endif()

# 将 CROSS_* 同步到 CMake 标准变量（优先不覆盖用户已通过 -D 传入的 CMAKE_*）。
if((NOT CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "") AND (CROSS_SYSROOT AND NOT CROSS_SYSROOT STREQUAL ""))
    set(CMAKE_SYSROOT "${CROSS_SYSROOT}" CACHE PATH "CMake sysroot" FORCE)
endif()
if((NOT CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "") AND (CROSS_CXX_COMPILER AND NOT CROSS_CXX_COMPILER STREQUAL ""))
    set(CMAKE_CXX_COMPILER "${CROSS_CXX_COMPILER}" CACHE FILEPATH "CMake CXX compiler" FORCE)
endif()
if((NOT CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "") AND (CROSS_C_COMPILER AND NOT CROSS_C_COMPILER STREQUAL ""))
    set(CMAKE_C_COMPILER "${CROSS_C_COMPILER}" CACHE FILEPATH "CMake C compiler" FORCE)
endif()

# 限制 find_* 行为：程序走 host，库/头/包走目标 sysroot。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ==============================
# 3) ARM64_LIBS_PREFIX（第三方库前缀）
# ==============================
# 说明：用于在交叉编译时优先从指定前缀下查找目标库/头文件。
# - build.ps1 会设置环境变量 ARM64_LIBS_PREFIX（例如 C:\euler\libs）
# - 也可手动通过 -DARM64_LIBS_PREFIX=... 传入
set(ARM64_LIBS_PREFIX "$ENV{ARM64_LIBS_PREFIX}" CACHE PATH "Prefix for aarch64 third-party libs")

# ==============================
# 4) 目标与源码
# ==============================
set(TARGET_NAME "${PROJECT_NAME}")
file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(${TARGET_NAME} ${ALL_SOURCES})
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${TARGET_NAME} PRIVATE pthread rt)

# ==============================
# 4.1) ftxui (UI)
# ==============================
find_path(FTXUI_INCLUDE_DIR NAMES ftxui/component/component.hpp)
find_library(FTXUI_DOM_LIBRARY NAMES ftxui-dom)
find_library(FTXUI_COMPONENT_LIBRARY NAMES ftxui-component)
find_library(FTXUI_SCREEN_LIBRARY NAMES ftxui-screen)

if((NOT FTXUI_INCLUDE_DIR OR NOT FTXUI_DOM_LIBRARY OR NOT FTXUI_COMPONENT_LIBRARY OR NOT FTXUI_SCREEN_LIBRARY) AND ARM64_LIBS_PREFIX AND NOT ARM64_LIBS_PREFIX STREQUAL "")
    find_path(FTXUI_INCLUDE_DIR NAMES ftxui/component/component.hpp PATHS "${ARM64_LIBS_PREFIX}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_DOM_LIBRARY NAMES ftxui-dom PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_COMPONENT_LIBRARY NAMES ftxui-component PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_SCREEN_LIBRARY NAMES ftxui-screen PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()

if(FTXUI_INCLUDE_DIR AND FTXUI_DOM_LIBRARY AND FTXUI_COMPONENT_LIBRARY AND FTXUI_SCREEN_LIBRARY)
    target_include_directories(${TARGET_NAME} PRIVATE ${FTXUI_INCLUDE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE ${FTXUI_DOM_LIBRARY} ${FTXUI_COMPONENT_LIBRARY} ${FTXUI_SCREEN_LIBRARY})
else()
    message(FATAL_ERROR "FTXUI not found. Provide headers/libs via sysroot or ARM64_LIBS_PREFIX.")
endif()

# ==============================
# 5) 可选依赖：liburing / libaio / libgpiod
# ==============================
# 说明：优先按常规 find_library/find_path 查找；若找不到且 ARM64_LIBS_PREFIX 已设置，
# 则到 ${ARM64_LIBS_PREFIX}/lib 与 ${ARM64_LIBS_PREFIX}/include 下查找。

find_library(LIBURING_LIBRARY NAMES uring)
find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
if((NOT LIBURING_LIBRARY OR NOT LIBURING_INCLUDE_DIR) AND ARM64_LIBS_PREFIX AND NOT ARM64_LIBS_PREFIX STREQUAL "")
    find_library(LIBURING_LIBRARY NAMES uring PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_path(LIBURING_INCLUDE_DIR NAMES liburing.h PATHS "${ARM64_LIBS_PREFIX}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
if(LIBURING_LIBRARY AND LIBURING_INCLUDE_DIR)
    target_compile_definitions(${TARGET_NAME} PRIVATE MB_DDF_HAS_IOURING=1)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBURING_INCLUDE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIBURING_LIBRARY})
endif()

find_library(LIBAIO_LIBRARY NAMES aio)
find_path(LIBAIO_INCLUDE_DIR NAMES libaio.h)
if((NOT LIBAIO_LIBRARY OR NOT LIBAIO_INCLUDE_DIR) AND ARM64_LIBS_PREFIX AND NOT ARM64_LIBS_PREFIX STREQUAL "")
    find_library(LIBAIO_LIBRARY NAMES aio PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_path(LIBAIO_INCLUDE_DIR NAMES libaio.h PATHS "${ARM64_LIBS_PREFIX}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
if(LIBAIO_LIBRARY AND LIBAIO_INCLUDE_DIR AND NOT LIBURING_LIBRARY)
    target_compile_definitions(${TARGET_NAME} PRIVATE MB_DDF_HAS_LIBAIO=1)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBAIO_INCLUDE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIBAIO_LIBRARY})
endif()

find_library(GPIOD_LIBRARY NAMES gpiod)
find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h)
if((NOT GPIOD_LIBRARY OR NOT GPIOD_INCLUDE_DIR) AND ARM64_LIBS_PREFIX AND NOT ARM64_LIBS_PREFIX STREQUAL "")
    find_library(GPIOD_LIBRARY NAMES gpiod PATHS "${ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h PATHS "${ARM64_LIBS_PREFIX}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
if(GPIOD_LIBRARY AND GPIOD_INCLUDE_DIR)
    target_include_directories(${TARGET_NAME} PRIVATE ${GPIOD_INCLUDE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE ${GPIOD_LIBRARY})
endif()

# ==============================
# 6) Debug/Release 编译与链接参数
# ==============================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${TARGET_NAME} PRIVATE -g -O0 -Wall -Wextra -pedantic)
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -g")
else()
    target_compile_options(${TARGET_NAME} PRIVATE -O3 -DNDEBUG -Werror)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=auto -s")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin -s")
    endif()
endif()

# ==============================
# 7) 用户自定义库（静态/动态）链接模板
# ==============================
# 目标：让你能很容易把自定义库加入编译/链接（例如你自己的 .a/.so 或第三方库）。
# 本模板仅提供一种“固定路径”的用法（方法 A）：你指定 include 目录与 lib 目录，并显式链接库文件或库名。
#
# A) 固定路径链接（推荐最直接、最不容易踩坑的方式）
#
# 1) 设定头文件与库文件所在目录（示例使用 ARM64_LIBS_PREFIX，也可以写绝对路径）
# set(USER_LIB_INCLUDE_DIR "${ARM64_LIBS_PREFIX}/include")
# set(USER_LIB_DIR         "${ARM64_LIBS_PREFIX}/lib")
#
# 2) 让编译器能找到头文件
# target_include_directories(${TARGET_NAME} PRIVATE "${USER_LIB_INCLUDE_DIR}")
#
# 3) 让链接器能找到库文件（目录）
# target_link_directories(${TARGET_NAME} PRIVATE "${USER_LIB_DIR}")
#
# 4) 链接库（二选一）
# 4.1) 按库名链接：会在 USER_LIB_DIR 下找 lib<name>.a / lib<name>.so
# target_link_libraries(${TARGET_NAME} PRIVATE yourlibname)
#
# 4.2) 按“库文件完整路径”链接：最明确，适合你手上就是一个具体 .a/.so 文件
# target_link_libraries(${TARGET_NAME} PRIVATE "${USER_LIB_DIR}/libyourlibname.a")
# target_link_libraries(${TARGET_NAME} PRIVATE "${USER_LIB_DIR}/libyourlibname.so")

# =============================================================================
# 8) 自动链接项目本地 libs 目录下的库（增强功能）
# =============================================================================
# 功能说明：
# 自动扫描当前 CMakeLists.txt 同级目录下的 libs 文件夹：
# 1. 头文件目录：libs/include -> 自动添加到 include 路径
# 2. 库文件目录：libs/lib -> 自动扫描该目录下所有 .a (静态库) 和 .so (动态库) 并链接
#
# 注意：这会链接 libs/lib 下的所有库，如果你只需要链接特定库，请手动管理或移除不需要的库文件。

set(_LOCAL_LIBS_ROOT "${CMAKE_CURRENT_LIST_DIR}/libs")
set(_LOCAL_LIBS_INCLUDE_DIR "${_LOCAL_LIBS_ROOT}/include")
set(_LOCAL_LIBS_LIB_DIR "${_LOCAL_LIBS_ROOT}/lib")

# 8.1) 自动添加头文件路径
if(EXISTS "${_LOCAL_LIBS_INCLUDE_DIR}")
    message(STATUS "Found local libs include dir: ${_LOCAL_LIBS_INCLUDE_DIR}")
    target_include_directories(${TARGET_NAME} PRIVATE "${_LOCAL_LIBS_INCLUDE_DIR}")
endif()

# 8.2) 自动扫描并链接库文件 (.a 和 .so)
if(EXISTS "${_LOCAL_LIBS_LIB_DIR}")
    file(GLOB _LOCAL_STATIC_LIB_FILES "${_LOCAL_LIBS_LIB_DIR}/*.a")
    file(GLOB _LOCAL_SHARED_LIB_FILES "${_LOCAL_LIBS_LIB_DIR}/*.so")
    
    # 合并列表
    set(_LOCAL_ALL_LIB_FILES ${_LOCAL_STATIC_LIB_FILES} ${_LOCAL_SHARED_LIB_FILES})
    
    if(_LOCAL_ALL_LIB_FILES)
        # 去重（虽然 glob 通常不会重复，但作为保险）
        list(REMOVE_DUPLICATES _LOCAL_ALL_LIB_FILES)
        
        message(STATUS "Found local libs in ${_LOCAL_LIBS_LIB_DIR}, linking: ${_LOCAL_ALL_LIB_FILES}")
        target_link_libraries(${TARGET_NAME} PRIVATE ${_LOCAL_ALL_LIB_FILES})
    else()
        message(STATUS "Local libs dir exists but no .a/.so files found in ${_LOCAL_LIBS_LIB_DIR}")
    endif()
endif()
