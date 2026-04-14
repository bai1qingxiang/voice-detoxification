cmake_minimum_required(VERSION 3.20)

project(voice_detoxification VERSION 0.1.0 LANGUAGES C CXX)

# =========================
# 基础设置
# =========================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =========================
# 开关
# =========================
option(VOICE_DETOX_BUILD_APP "Build main application" ON)
option(VOICE_DETOX_ENABLE_WHISPER "Enable whisper.cpp" ON)

# =========================
# 路径
# =========================
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)
set(WHISPER_DIR ${THIRD_PARTY_DIR}/whisper.cpp)

# 如果您还没移动到 third_party，就改成：
# set(WHISPER_DIR ${CMAKE_SOURCE_DIR}/whisper.cpp)

# =========================
# 第三方：whisper.cpp
# =========================
if(VOICE_DETOX_ENABLE_WHISPER)
    if(EXISTS ${WHISPER_DIR}/CMakeLists.txt)
        message(STATUS "Adding whisper.cpp: ${WHISPER_DIR}")
        add_subdirectory(${WHISPER_DIR} ${CMAKE_BINARY_DIR}/third_party/whisper)
    else()
        message(FATAL_ERROR "whisper.cpp not found at: ${WHISPER_DIR}")
    endif()
endif()

# =========================
# 收集自己的源码
# =========================
file(GLOB_RECURSE VOICE_DETOX_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

if(NOT VOICE_DETOX_SOURCES)
    message(FATAL_ERROR "No source files found under src/")
endif()

add_library(voice_detox_core STATIC ${VOICE_DETOX_SOURCES})

target_include_directories(voice_detox_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(voice_detox_core PUBLIC cxx_std_17)

# =========================
# 链接 whisper.cpp
# 不同版本 target 名可能不同
# =========================
if(TARGET whisper)
    message(STATUS "Linking whisper target: whisper")
    target_link_libraries(voice_detox_core PUBLIC whisper)
elseif(TARGET libwhisper)
    message(STATUS "Linking whisper target: libwhisper")
    target_link_libraries(voice_detox_core PUBLIC libwhisper)
else()
    message(FATAL_ERROR "No whisper library target found.")
endif()

# Windows 常用系统库
if(WIN32)
    target_link_libraries(voice_detox_core PUBLIC
        ws2_32
        bcrypt
    )
endif()

# =========================
# 主程序
# =========================
if(VOICE_DETOX_BUILD_APP)
    add_executable(voice_detox_app
        ${CMAKE_SOURCE_DIR}/app/cli/main.cpp
    )

    target_link_libraries(voice_detox_app
        PRIVATE
            voice_detox_core
    )

    target_include_directories(voice_detox_app
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
endif()

# =========================
# 输出信息
# =========================
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Source dir: ${CMAKE_SOURCE_DIR}")
message(STATUS "Binary dir: ${CMAKE_BINARY_DIR}")
message(STATUS "========================================")