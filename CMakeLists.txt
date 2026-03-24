cmake_minimum_required(VERSION 3.16)
project(rtf2html VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 检测平台 ----
if(WIN32)
    set(LO_PLATFORM "windows")
elseif(APPLE)
    set(LO_PLATFORM "macos")
else()
    set(LO_PLATFORM "linux")   # Linux / 鸿蒙 HarmonyOS
endif()

message(STATUS "目标平台: ${LO_PLATFORM}")

# ---- LibreOffice 安装目录（可命令行传入） ----
if(NOT DEFINED LIBREOFFICE_DIR)
    if(WIN32)
        if(EXISTS "C:/Program Files/LibreOffice")
            set(LIBREOFFICE_DIR "C:/Program Files/LibreOffice")
        elseif(EXISTS "C:/Program Files (x86)/LibreOffice")
            set(LIBREOFFICE_DIR "C:/Program Files (x86)/LibreOffice")
        endif()
    elseif(APPLE)
        if(EXISTS "/Applications/LibreOffice.app")
            set(LIBREOFFICE_DIR "/Applications/LibreOffice.app/Contents")
        endif()
    else()
        # Linux / 鸿蒙
        foreach(candidate
                "/usr/lib/libreoffice"
                "/usr/lib64/libreoffice"
                "/opt/libreoffice")
            if(EXISTS "${candidate}")
                set(LIBREOFFICE_DIR "${candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT DEFINED LIBREOFFICE_DIR)
    message(WARNING
        "未找到 LibreOffice 安装目录，仅头文件编译。\n"
        "若需链接，请指定: cmake -DLIBREOFFICE_DIR=<path>")
    set(LIBREOFFICE_DIR "")
endif()

if(LIBREOFFICE_DIR)
    message(STATUS "LibreOffice 目录: ${LIBREOFFICE_DIR}")
endif()

# ---- 可执行文件 ----
add_executable(rtf2html src/rtf2html.cpp)

# ---- 头文件包含路径 ----
target_include_directories(rtf2html PRIVATE
    ${CMAKE_SOURCE_DIR}/include     # 项目内的 LibreOfficeKit/ 头文件
)
if(LIBREOFFICE_DIR)
    if(APPLE)
        target_include_directories(rtf2html PRIVATE
            "${LIBREOFFICE_DIR}/MacOS"
            "${LIBREOFFICE_DIR}/sdk/include"
        )
    else()
        target_include_directories(rtf2html PRIVATE
            "${LIBREOFFICE_DIR}/sdk/include"
            "${LIBREOFFICE_DIR}/program"
        )
    endif()
endif()

# ---- 链接库 ----
# LOKit 通过 dlopen/LoadLibrary 动态加载，编译期只需系统库
if(WIN32)
    target_link_libraries(rtf2html PRIVATE kernel32 user32 advapi32)
elseif(APPLE)
    target_link_libraries(rtf2html PRIVATE dl)
else()
    # Linux / 鸿蒙
    target_link_libraries(rtf2html PRIVATE dl)
endif()

# ---- 编译选项 ----
if(MSVC)
    target_compile_options(rtf2html PRIVATE /W3 /utf-8 /EHsc)
    target_compile_definitions(rtf2html PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Release>:NDEBUG>
    )
elseif(APPLE)
    target_compile_options(rtf2html PRIVATE -Wall -Wextra)
    set_target_properties(rtf2html PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
else()
    # Linux / 鸿蒙（GCC / Clang）
    target_compile_options(rtf2html PRIVATE -Wall -Wextra)
    set_target_properties(rtf2html PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ---- 安装 ----
install(TARGETS rtf2html DESTINATION bin)
