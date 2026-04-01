cmake_minimum_required(VERSION 3.20)
project(chat_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chatapp
    src/main.cpp
    src/protocol/secp.cpp
    src/protocol/tcp_line_reader.cpp
    src/net/udp_socket.cpp
    src/net/tcp_connection.cpp
    src/net/tcp_server.cpp
    src/net/tcp_client.cpp
    src/net/local_endpoint.cpp
    src/state/user_directory.cpp
    src/services/discovery_service.cpp
    src/services/direct_message_service.cpp
    src/services/group_room_coordinator.cpp
    src/services/guaranteed_open_room_service.cpp
    src/services/secure_room_service.cpp
    src/services/lobby_service.cpp
    src/crypto/chacha20.cpp
    src/crypto/sha256_hmac.cpp
    src/crypto/secure_message_crypto.cpp
    src/crypto/random_bytes.cpp
    src/app/receive_multiplex_loop.cpp
    src/app/sync_output.cpp
    src/app/text_menu.cpp
    src/log/logger.cpp
)

target_include_directories(chatapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(chatapp PRIVATE
    -Wall -Wextra -Wpedantic
)
