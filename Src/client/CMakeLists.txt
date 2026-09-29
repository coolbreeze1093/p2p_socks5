cmake_minimum_required(VERSION 3.20)

project(client LANGUAGES C CXX)

add_executable(client
    src/main.cpp
    src/socks5_session.cpp
    src/udp_session.cpp
    src/socks5.cpp
    src/network_rtc_app.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_protocol.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_mux.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_id_generator.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/p2p_client.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/signaling.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/tcp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/udp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/crash_dump.cpp
)

target_include_directories(client
    PRIVATE
    ${CMAKE_SOURCE_DIR}/ThirdParty/asio-1.38.2/include
    ${CMAKE_SOURCE_DIR}/Src/common/include
    include)

if(UNIX)
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shlwapi.lib")
elseif(WIN32)
    set(windows_libs ws2_32 mswsock dbghelp shlwapi)
endif()


target_link_libraries(client
    PRIVATE
    datachannel
    nlohmann_json::nlohmann_json
    plog::plog
    llhttp::llhttp
    ${windows_libs}
)

# main.c 使用 C++ 库，确保最终链接使用 C++ linker
set_target_properties(client PROPERTIES
    LINKER_LANGUAGE CXX
)