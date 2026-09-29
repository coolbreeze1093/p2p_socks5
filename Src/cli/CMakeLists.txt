cmake_minimum_required(VERSION 3.20)

# 命令行版 P2P SOCKS5：一对程序，无 WebSocket 信令，
# 双方在命令行手动交换 SDP(base64) 建立 WebRTC P2P 连接。
#   rtcsocks       —— 本地端(offerer)：提供 SOCKS5 代理入口
#   rtcsocks-exit  —— 出口端(answerer)：真正对目标发起 TCP/UDP 连接
#
# manual_p2p 为新写的无信令 P2P 通道；其余实现全部复用：
#   - SOCKS5 会话复用 Src/client（其 tcp_session/udp_session 已修复：
#     SOCKS5 握手解析、UDP 应答竞态、UDP stream id，修复同时惠及旧 client）
#   - 出口端复用 Src/server 的 tunel_session 等
project(cli LANGUAGES C CXX)

set(COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_protocol.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_mux.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_id_generator.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/tcp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/udp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/crash_dump.cpp
)

add_executable(rtcsocks
    src/main_client.cpp
    src/manual_p2p.cpp
    ${CMAKE_SOURCE_DIR}/Src/client/src/socks5.cpp
    ${CMAKE_SOURCE_DIR}/Src/client/src/socks5_session.cpp
    ${CMAKE_SOURCE_DIR}/Src/client/src/udp_session.cpp
    ${COMMON_SOURCES}
)

add_executable(rtcsocks-exit
    src/main_exit.cpp
    src/manual_p2p.cpp
    ${CMAKE_SOURCE_DIR}/Src/server/src/tunnel_session.cpp
    ${CMAKE_SOURCE_DIR}/Src/server/src/tcp_session.cpp
    ${CMAKE_SOURCE_DIR}/Src/server/src/udp_session.cpp
    ${CMAKE_SOURCE_DIR}/Src/server/src/http_session.cpp
    ${COMMON_SOURCES}
)

if(WIN32)
    # user32/gdi32/gdiplus: 远程桌面(窗口、抓屏、JPEG 编解码、SendInput)
    # openh264: 跨平台 H.264 编解码(msys2 包 mingw-w64-x86_64-openh264)
    set(windows_libs ws2_32 mswsock dbghelp shlwapi user32 gdi32 gdiplus openh264)
endif()

foreach(target IN ITEMS rtcsocks rtcsocks-exit)
    target_include_directories(${target}
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ThirdParty/asio-1.38.2/include
        ${CMAKE_SOURCE_DIR}/Src/common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_link_libraries(${target}
        PRIVATE
        datachannel
        nlohmann_json::nlohmann_json
        plog::plog
        llhttp::llhttp
        ${windows_libs})

    set_target_properties(${target} PROPERTIES
        LINKER_LANGUAGE CXX
    )
endforeach()

# 客户端：复用 client 的 SOCKS5 会话头/源
target_include_directories(rtcsocks
    PRIVATE
    ${CMAKE_SOURCE_DIR}/Src/client/include)

# 出口端：复用 server 的出口实现（tcp_session.h 与 client 的同名但不同类，
# 两个目标各用各的 include 目录，不能混排）
target_include_directories(rtcsocks-exit
    PRIVATE
    ${CMAKE_SOURCE_DIR}/Src/server/include)
