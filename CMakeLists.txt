cmake_minimum_required(VERSION 3.20)

project(rtcvpn LANGUAGES C CXX)

# ============================================================
# C++ 标准
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# 平台
# ============================================================

if(MSVC)

    message(STATUS "Compiler: MSVC")

    add_compile_options(
        /W4
        /utf-8
    )
    set(MY_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Windows")
elseif(MINGW)

    message(STATUS "Compiler: MinGW")

    add_compile_options(
        -Wall
    )
    set(MY_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Windows")
elseif(UNIX)

    message(STATUS "Compiler: Unix")

    add_compile_options(
        -Wall
    )
    set(MY_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Linux")

else()

    message(FATAL_ERROR "Unsupported compiler/platform")

endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asio-1.38.2)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_CURRENT_SOURCE_DIR}/downloads/asio-1.38.2.zip" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/)
endif()
message(STATUS "uncompress asio-1.38.2.zip")

set(DOWNLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/downloads")
set(BUILD_DIR "${CMAKE_BINARY_DIR}/builds")

file(MAKE_DIRECTORY ${DOWNLOAD_DIR})
file(MAKE_DIRECTORY ${BUILD_DIR})

include(FetchContent)

if(TARGET nlohmann_json::nlohmann_json)
    message(STATUS "✅ nlohmann_json::nlohmann_json target 存在")
else()
    FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

    FetchContent_MakeAvailable(nlohmann_json)
endif()


# ---- 在 Declare/MakeAvailable 之前设置选项 ----
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)          # 不编译示例
set(NO_TESTS ON CACHE BOOL "" FORCE)             # 不编译测试
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)        # 是否禁用 WebSocket 支持
set(NO_MEDIA OFF CACHE BOOL "" FORCE)            # 是否禁用媒体传输(SRTP)支持
set(ENABLE_OPENSSL ON CACHE BOOL "" FORCE)                # 是否启用 OpenSSL
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)          # 用 GnuTLS 而不是 OpenSSL
set(USE_NICE OFF CACHE BOOL "" FORCE)            # 用 libnice 而不是内置的 libjuice 做 ICE
set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)         # 用 mbedTLS
set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)   # 是否优先用系统已装的依赖库（而不是自己 fetch）
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)   # 静态库还是动态库
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)   # 是否编译测试应用
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)            # 是否将警告作为错误处理

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-error=format -Wno-error=format-extra-args)
endif()

FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.24.5
)

FetchContent_MakeAvailable(libdatachannel)

set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")

FetchContent_Declare(
    llhttp
    GIT_REPOSITORY https://github.com/nodejs/llhttp.git
    GIT_TAG release/v9.2.1
)
FetchContent_MakeAvailable(llhttp)


if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

add_subdirectory(Src/client)
add_subdirectory(Src/server)
add_subdirectory(Src/cli)