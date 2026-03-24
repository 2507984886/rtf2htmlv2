cmake_minimum_required(VERSION 3.16)
project(rtf2html VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 检测平台 ----
if(WIN32)
    set(PLATFORM "windows")
elseif(APPLE)
    set(PLATFORM "macos")
else()
    set(PLATFORM "linux")
endif()
message(STATUS "目标平台: ${PLATFORM}")

# ---- 可执行文件（零依赖，单文件） ----
add_executable(rtf2html src/rtf2html.cpp)

# ---- 链接库（仅系统库） ----
if(WIN32)
    target_link_libraries(rtf2html PRIVATE kernel32 user32 shell32)
endif()

# ---- 编译选项 ----
if(MSVC)
    target_compile_options(rtf2html PRIVATE /W3 /utf-8 /EHsc /O2)
    target_compile_definitions(rtf2html PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        $<$<CONFIG:Release>:NDEBUG>
    )
elseif(APPLE)
    target_compile_options(rtf2html PRIVATE -Wall -Wextra -O2)
    set_target_properties(rtf2html PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
else()
    # Linux / 鸿蒙 HarmonyOS
    target_compile_options(rtf2html PRIVATE -Wall -Wextra -O2)
    set_target_properties(rtf2html PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ---- 安装 ----
install(TARGETS rtf2html DESTINATION bin)

message(STATUS "构建配置完成 - 零依赖纯C++实现，无需安装LibreOffice")
