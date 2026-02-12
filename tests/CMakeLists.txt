# tests/CMakeLists.txt - 测试模块构建配置

cmake_minimum_required(VERSION 3.10)

# ==============================
# 测试可执行文件
# ==============================
set(TEST_TARGET_NAME "MB_DDF_Tests")

# 测试源文件
file(GLOB_RECURSE TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/component/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp"
)

# 收集被测代码（排除 main.cpp、Demo 目录和 UI 相关文件）
file(GLOB_RECURSE SRC_FILES
    "${CMAKE_SOURCE_DIR}/src/MB_DDF/*.cpp"
)
# 排除不需要测试的文件
list(FILTER SRC_FILES EXCLUDE REGEX "main\\.cpp$")
list(FILTER SRC_FILES EXCLUDE REGEX "/Demo/")
list(FILTER SRC_FILES EXCLUDE REGEX "monitor_ui")  # UI 依赖 ftxui，跳过

# 如果没有测试源文件，创建一个最小测试
if(NOT TEST_SOURCES)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dummy_test.cpp"
        "#include <gtest/gtest.h>\n"
        "TEST(Dummy, Placeholder) { EXPECT_TRUE(true); }\n"
    )
    set(TEST_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/dummy_test.cpp")
    message(STATUS "No test sources found, using placeholder")
endif()

add_executable(${TEST_TARGET_NAME} ${TEST_SOURCES} ${SRC_FILES})

# ==============================
# 包含路径
# ==============================
target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # tests/
    ${CMAKE_SOURCE_DIR}/src               # src/
    ${CMAKE_SOURCE_DIR}                   # 项目根目录
)

# ==============================
# ftxui (从主项目继承或重新查找)
# ==============================
if(NOT FTXUI_INCLUDE_DIR)
    find_path(FTXUI_INCLUDE_DIR NAMES ftxui/component/component.hpp PATHS "$ENV{ARM64_LIBS_PREFIX}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_DOM_LIBRARY NAMES ftxui-dom PATHS "$ENV{ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_COMPONENT_LIBRARY NAMES ftxui-component PATHS "$ENV{ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FTXUI_SCREEN_LIBRARY NAMES ftxui-screen PATHS "$ENV{ARM64_LIBS_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()

if(FTXUI_INCLUDE_DIR AND FTXUI_DOM_LIBRARY)
    target_include_directories(${TEST_TARGET_NAME} PRIVATE ${FTXUI_INCLUDE_DIR})
    target_link_libraries(${TEST_TARGET_NAME} PRIVATE ${FTXUI_DOM_LIBRARY} ${FTXUI_COMPONENT_LIBRARY} ${FTXUI_SCREEN_LIBRARY})
endif()

# ==============================
# 链接库
# ==============================
target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    gtest_main      # Google Test main函数
    gmock           # Google Mock
    pthread
    rt
)

# ==============================
# 编译选项
# ==============================
target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_20)

# 测试专用宏定义
target_compile_definitions(${TEST_TARGET_NAME} PRIVATE
    MB_DDF_TEST_BUILD=1
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${TEST_TARGET_NAME} PRIVATE
        -g -O0
        -Wall -Wextra
        --coverage  # 覆盖率支持
    )
    target_link_options(${TEST_TARGET_NAME} PRIVATE --coverage)
else()
    target_compile_options(${TEST_TARGET_NAME} PRIVATE -O2)
endif()

# 静态链接C++运行时，减少目标板依赖
target_link_options(${TEST_TARGET_NAME} PRIVATE
    -static-libstdc++
    -static-libgcc
)

# ==============================
# 注册测试
# ==============================
enable_testing()
add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})

# 输出测试二进制位置
message(STATUS "Test target: ${TEST_TARGET_NAME}")
message(STATUS "Test sources: ${TEST_SOURCES}")
message(STATUS "SRC sources: ${SRC_FILES}")
