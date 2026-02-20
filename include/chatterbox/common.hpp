#ifndef CHATTERBOX_COMMON_HPP
#define CHATTERBOX_COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <functional>
#include <chrono>

namespace chatterbox {

// Version information
constexpr uint8_t PROTOCOL_VERSION_MAJOR = 1;
constexpr uint8_t PROTOCOL_VERSION_MINOR = 0;

// System limits
constexpr size_t MAX_USERS = 64;
constexpr size_t MAX_MESSAGE_SIZE = 4096;
constexpr size_t MAX_PAYLOAD_SIZE = MAX_MESSAGE_SIZE - 32; // Header is 32 bytes
constexpr size_t MAX_USERNAME_LENGTH = 32;
constexpr size_t MAX_ROOM_NAME_LENGTH = 64;

// IPC keys
constexpr key_t BASE_IPC_KEY = 0x43484154; // "CHAT" in hex
constexpr key_t SERVER_QUEUE_KEY = BASE_IPC_KEY + 1;
constexpr key_t SHARED_MEM_KEY = BASE_IPC_KEY + 2;
constexpr key_t SEMAPHORE_KEY = BASE_IPC_KEY + 3;

// Timing constants (in milliseconds)
constexpr int64_t HEARTBEAT_INTERVAL_MS = 5000;
constexpr int64_t CONNECTION_TIMEOUT_MS = 15000;
constexpr int64_t MESSAGE_RETRY_DELAY_MS = 100;
constexpr int MAX_RETRY_ATTEMPTS = 5;

// Target latency
constexpr double TARGET_LATENCY_MS = 400.0;

// Error codes
enum class ErrorCode : int {
    SUCCESS = 0,
    ERR_IPC_CREATE = -1,
    ERR_IPC_ATTACH = -2,
    ERR_IPC_SEND = -3,
    ERR_IPC_RECV = -4,
    ERR_INVALID_MESSAGE = -5,
    ERR_CHECKSUM_MISMATCH = -6,
    ERR_TIMEOUT = -7,
    ERR_CONNECTION_LOST = -8,
    ERR_SERVER_FULL = -9,
    ERR_INVALID_USER = -10,
    ERR_PERMISSION_DENIED = -11,
    ERR_UNKNOWN = -99
};

// Convert error code to string
inline const char* error_to_string(ErrorCode err) {
    switch (err) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::ERR_IPC_CREATE: return "IPC creation failed";
        case ErrorCode::ERR_IPC_ATTACH: return "IPC attach failed";
        case ErrorCode::ERR_IPC_SEND: return "IPC send failed";
        case ErrorCode::ERR_IPC_RECV: return "IPC receive failed";
        case ErrorCode::ERR_INVALID_MESSAGE: return "Invalid message";
        case ErrorCode::ERR_CHECKSUM_MISMATCH: return "Checksum mismatch";
        case ErrorCode::ERR_TIMEOUT: return "Operation timeout";
        case ErrorCode::ERR_CONNECTION_LOST: return "Connection lost";
        case ErrorCode::ERR_SERVER_FULL: return "Server is full";
        case ErrorCode::ERR_INVALID_USER: return "Invalid user";
        case ErrorCode::ERR_PERMISSION_DENIED: return "Permission denied";
        default: return "Unknown error";
    }
}

// Utility type aliases
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

// User ID type
using UserId = uint32_t;
constexpr UserId INVALID_USER_ID = 0;
constexpr UserId SERVER_USER_ID = 1;
constexpr UserId BROADCAST_USER_ID = 0xFFFFFFFF;

// Room ID type
using RoomId = uint32_t;
constexpr RoomId LOBBY_ROOM_ID = 0;
constexpr RoomId INVALID_ROOM_ID = 0xFFFFFFFF;

// Sequence number type
using SequenceNum = uint32_t;

} // namespace chatterbox

#endif // CHATTERBOX_COMMON_HPP
