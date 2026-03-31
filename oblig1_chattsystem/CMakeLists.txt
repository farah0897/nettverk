cmake_minimum_required(VERSION 3.20)
project(chat_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chatapp
    src/main.cpp
    src/protocol/secp.cpp
    src/net/udp_socket.cpp
    src/net/local_endpoint.cpp
    src/state/user_directory.cpp
    src/services/discovery_service.cpp
    src/services/direct_message_service.cpp
    src/services/group_room_coordinator.cpp
    src/services/lobby_service.cpp
    src/app/receive_multiplex_loop.cpp
    src/app/sync_output.cpp
    src/app/text_menu.cpp
    src/log/logger.cpp
)

target_include_directories(chatapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(chatapp PRIVATE
    -Wall -Wextra -Wpedantic
)
