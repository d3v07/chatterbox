cmake_minimum_required(VERSION 3.16)
project(ChatterBox VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Platform-specific settings
if(APPLE)
    add_definitions(-DPLATFORM_MACOS)
    # macOS doesn't have System V IPC in the same way, use POSIX alternatives
    add_definitions(-DUSE_POSIX_IPC)
elseif(UNIX)
    add_definitions(-DPLATFORM_LINUX)
    add_definitions(-DUSE_SYSV_IPC)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# IPC Library
add_library(chatterbox_ipc STATIC
    src/ipc/message_queue.cpp
    src/ipc/shared_memory.cpp
    src/ipc/semaphore.cpp
)
target_link_libraries(chatterbox_ipc PRIVATE Threads::Threads)

# Protocol Library
add_library(chatterbox_protocol STATIC
    src/protocol/message.cpp
    src/protocol/serializer.cpp
    src/protocol/timestamp.cpp
)

# Sync Library
add_library(chatterbox_sync STATIC
    src/sync/thread_pool.cpp
    src/sync/mutex_guard.cpp
    src/sync/condition_var.cpp
)
target_link_libraries(chatterbox_sync PRIVATE Threads::Threads)

# Server Library
add_library(chatterbox_server STATIC
    src/server/server.cpp
    src/server/connection_manager.cpp
    src/server/message_router.cpp
)
target_link_libraries(chatterbox_server PRIVATE
    chatterbox_ipc
    chatterbox_protocol
    chatterbox_sync
    Threads::Threads
)

# Client Library
add_library(chatterbox_client STATIC
    src/client/client.cpp
    src/client/terminal_ui.cpp
    src/client/input_handler.cpp
)
target_link_libraries(chatterbox_client PRIVATE
    chatterbox_ipc
    chatterbox_protocol
    chatterbox_sync
    Threads::Threads
)

# Server Executable
add_executable(chatterbox_server_app apps/server_main.cpp)
target_link_libraries(chatterbox_server_app PRIVATE
    chatterbox_server
    Threads::Threads
)

# Client Executable
add_executable(chatterbox_client_app apps/client_main.cpp)
target_link_libraries(chatterbox_client_app PRIVATE
    chatterbox_client
    Threads::Threads
)

# Testing
enable_testing()

# Unit Tests
add_executable(test_protocol tests/unit/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE chatterbox_protocol)
add_test(NAME ProtocolTest COMMAND test_protocol)

add_executable(test_ipc tests/unit/test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE chatterbox_ipc Threads::Threads)
add_test(NAME IPCTest COMMAND test_ipc)

add_executable(test_sync tests/unit/test_sync.cpp)
target_link_libraries(test_sync PRIVATE chatterbox_sync Threads::Threads)
add_test(NAME SyncTest COMMAND test_sync)

# Integration Tests
add_executable(test_integration tests/integration/test_server_client.cpp)
target_link_libraries(test_integration PRIVATE
    chatterbox_server
    chatterbox_client
    Threads::Threads
)
add_test(NAME IntegrationTest COMMAND test_integration)

# Stress Tests
add_executable(test_stress tests/stress/stress_test.cpp)
target_link_libraries(test_stress PRIVATE
    chatterbox_server
    chatterbox_client
    Threads::Threads
)
add_test(NAME StressTest COMMAND test_stress)

# Install targets
install(TARGETS chatterbox_server_app chatterbox_client_app
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/chatterbox
    DESTINATION include
)

# Custom targets
add_custom_target(clean-ipc
    COMMAND ipcrm -a 2>/dev/null || true
    COMMENT "Cleaning up IPC resources"
)

message(STATUS "ChatterBox Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
