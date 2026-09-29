cmake_minimum_required(VERSION 3.20)

project(server LANGUAGES C CXX)

add_executable(server
    src/main.cpp
    src/udp_session.cpp
    src/tcp_session.cpp
    src/process_new_client.cpp
    src/http_session.cpp
    src/tunnel_session.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_protocol.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_mux.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/session_id_generator.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/tcp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/udp_socket.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/p2p_client.cpp
    ${CMAKE_SOURCE_DIR}/Src/common/src/signaling.cpp
    
)

target_include_directories(server
    PRIVATE
    ${CMAKE_SOURCE_DIR}/ThirdParty/asio-1.38.2/include
    ${CMAKE_SOURCE_DIR}/Src/common/include
    include)

if(UNIX)

elseif(WIN32)
    set(windows_libs ws2_32 mswsock)
endif()

target_link_libraries(server
    PRIVATE
    datachannel
    nlohmann_json::nlohmann_json
    plog::plog
    llhttp::llhttp
    ${windows_libs}
)

# main.c 使用 C++ 库，确保最终链接使用 C++ linker
set_target_properties(server PROPERTIES
    LINKER_LANGUAGE CXX
)