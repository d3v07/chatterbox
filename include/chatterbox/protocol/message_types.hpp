#ifndef CHATTERBOX_PROTOCOL_MESSAGE_TYPES_HPP
#define CHATTERBOX_PROTOCOL_MESSAGE_TYPES_HPP

// This header provides convenient re-exports of message type definitions
// from message.hpp for projects that only need the type definitions.

#include <chatterbox/protocol/message.hpp>

namespace chatterbox {
namespace protocol {

// Re-export MessageType from message.hpp
// The enum is defined there as:
//
// enum class MessageType : uint8_t {
//     // Connection management
//     CONNECT_REQUEST = 0x01,
//     CONNECT_RESPONSE = 0x02,
//     DISCONNECT = 0x03,
//     HEARTBEAT = 0x04,
//     HEARTBEAT_ACK = 0x05,
//
//     // Chat messages
//     CHAT_MESSAGE = 0x10,
//     CHAT_BROADCAST = 0x11,
//     PRIVATE_MESSAGE = 0x12,
//
//     // Room management
//     ROOM_JOIN = 0x20,
//     ROOM_LEAVE = 0x21,
//     ROOM_CREATE = 0x22,
//     ROOM_LIST = 0x23,
//     ROOM_USERS = 0x24,
//
//     // User management
//     USER_LIST = 0x30,
//     USER_INFO = 0x31,
//     USER_STATUS = 0x32,
//
//     // Acknowledgments
//     ACK = 0x40,
//     NACK = 0x41,
//
//     // Error messages
//     ERROR_MSG = 0xF0,
//
//     // System messages
//     SYSTEM_MESSAGE = 0xFE,
//     UNKNOWN = 0xFF
// };

// Message type category checks
inline bool is_connection_message(MessageType type) {
    uint8_t t = static_cast<uint8_t>(type);
    return t >= 0x01 && t <= 0x05;
}

inline bool is_chat_message(MessageType type) {
    uint8_t t = static_cast<uint8_t>(type);
    return t >= 0x10 && t <= 0x1F;
}

inline bool is_room_message(MessageType type) {
    uint8_t t = static_cast<uint8_t>(type);
    return t >= 0x20 && t <= 0x2F;
}

inline bool is_user_message(MessageType type) {
    uint8_t t = static_cast<uint8_t>(type);
    return t >= 0x30 && t <= 0x3F;
}

inline bool is_ack_message(MessageType type) {
    uint8_t t = static_cast<uint8_t>(type);
    return t >= 0x40 && t <= 0x4F;
}

inline bool is_error_message(MessageType type) {
    return type == MessageType::ERROR_MSG;
}

inline bool is_system_message(MessageType type) {
    return type == MessageType::SYSTEM_MESSAGE;
}

// Check if a message type requires acknowledgment
inline bool requires_ack_default(MessageType type) {
    switch (type) {
        case MessageType::CONNECT_REQUEST:
        case MessageType::CHAT_MESSAGE:
        case MessageType::PRIVATE_MESSAGE:
        case MessageType::ROOM_JOIN:
        case MessageType::ROOM_LEAVE:
        case MessageType::ROOM_CREATE:
            return true;
        default:
            return false;
    }
}

// Check if a message type should be logged
inline bool should_log_message(MessageType type) {
    switch (type) {
        case MessageType::HEARTBEAT:
        case MessageType::HEARTBEAT_ACK:
            return false; // Too verbose
        default:
            return true;
    }
}

} // namespace protocol
} // namespace chatterbox

#endif // CHATTERBOX_PROTOCOL_MESSAGE_TYPES_HPP
