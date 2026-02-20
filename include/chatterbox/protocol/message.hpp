#ifndef CHATTERBOX_PROTOCOL_MESSAGE_HPP
#define CHATTERBOX_PROTOCOL_MESSAGE_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <memory>

namespace chatterbox {
namespace protocol {

// Magic number for protocol identification
constexpr uint16_t PROTOCOL_MAGIC = 0x4342; // "CB" for ChatterBox

// Message types
enum class MessageType : uint8_t {
    // Connection management
    CONNECT_REQUEST = 0x01,
    CONNECT_RESPONSE = 0x02,
    DISCONNECT = 0x03,
    HEARTBEAT = 0x04,
    HEARTBEAT_ACK = 0x05,

    // Chat messages
    CHAT_MESSAGE = 0x10,
    CHAT_BROADCAST = 0x11,
    PRIVATE_MESSAGE = 0x12,

    // Room management
    ROOM_JOIN = 0x20,
    ROOM_LEAVE = 0x21,
    ROOM_CREATE = 0x22,
    ROOM_LIST = 0x23,
    ROOM_USERS = 0x24,

    // User management
    USER_LIST = 0x30,
    USER_INFO = 0x31,
    USER_STATUS = 0x32,

    // Acknowledgments
    ACK = 0x40,
    NACK = 0x41,

    // Error messages
    ERROR_MSG = 0xF0,

    // System messages
    SYSTEM_MESSAGE = 0xFE,
    UNKNOWN = 0xFF
};

// Message flags
enum class MessageFlags : uint8_t {
    NONE = 0x00,
    REQUIRES_ACK = 0x01,
    COMPRESSED = 0x02,
    ENCRYPTED = 0x04,
    PRIORITY = 0x08,
    FRAGMENTED = 0x10,
    LAST_FRAGMENT = 0x20,
    BROADCAST = 0x40,
    RESERVED = 0x80
};

// Combine flags
inline MessageFlags operator|(MessageFlags a, MessageFlags b) {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline MessageFlags operator&(MessageFlags a, MessageFlags b) {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool has_flag(MessageFlags flags, MessageFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// 32-byte binary protocol header
// Packed to ensure consistent size across platforms
#pragma pack(push, 1)
struct MessageHeader {
    uint16_t magic;           // 0-1:   Protocol magic number (0x4342)
    uint8_t version_major;    // 2:     Protocol version major
    uint8_t version_minor;    // 3:     Protocol version minor
    uint8_t type;             // 4:     Message type
    uint8_t flags;            // 5:     Message flags
    uint16_t reserved;        // 6-7:   Reserved for future use
    uint64_t timestamp;       // 8-15:  Timestamp (microseconds since epoch)
    uint32_t sequence;        // 16-19: Sequence number
    uint32_t sender_id;       // 20-23: Sender user ID
    uint32_t payload_length;  // 24-27: Payload length in bytes
    uint32_t checksum;        // 28-31: CRC32 checksum of header + payload

    MessageHeader();
    void init();
    bool validate() const;
    uint32_t compute_checksum(const void* payload, size_t payload_len) const;
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 32, "MessageHeader must be exactly 32 bytes");

// Complete message with header and payload
class Message {
public:
    // Constructors
    Message();
    explicit Message(MessageType type);
    Message(MessageType type, const std::string& payload);
    Message(MessageType type, const void* data, size_t size);

    // Copy and move
    Message(const Message& other);
    Message(Message&& other) noexcept;
    Message& operator=(const Message& other);
    Message& operator=(Message&& other) noexcept;

    ~Message() = default;

    // Header accessors
    const MessageHeader& header() const { return header_; }
    MessageHeader& header() { return header_; }

    // Type accessors
    MessageType type() const { return static_cast<MessageType>(header_.type); }
    void set_type(MessageType type) { header_.type = static_cast<uint8_t>(type); }

    // Flag accessors
    MessageFlags flags() const { return static_cast<MessageFlags>(header_.flags); }
    void set_flags(MessageFlags flags) { header_.flags = static_cast<uint8_t>(flags); }
    void add_flag(MessageFlags flag) {
        header_.flags |= static_cast<uint8_t>(flag);
    }
    bool has_flag(MessageFlags flag) const {
        return (header_.flags & static_cast<uint8_t>(flag)) != 0;
    }

    // Timestamp accessors
    Timestamp timestamp() const { return Timestamp(header_.timestamp); }
    void set_timestamp(const Timestamp& ts) { header_.timestamp = ts.value(); }

    // Sequence accessors
    SequenceNum sequence() const { return header_.sequence; }
    void set_sequence(SequenceNum seq) { header_.sequence = seq; }

    // Sender accessors
    UserId sender_id() const { return header_.sender_id; }
    void set_sender_id(UserId id) { header_.sender_id = id; }

    // Payload accessors
    const std::vector<uint8_t>& payload() const { return payload_; }
    std::vector<uint8_t>& payload() { return payload_; }
    size_t payload_size() const { return payload_.size(); }

    // Set payload
    void set_payload(const void* data, size_t size);
    void set_payload(const std::string& str);
    void set_payload(const std::vector<uint8_t>& data);

    // Get payload as string
    std::string payload_as_string() const;

    // Compute and update checksum
    void update_checksum();

    // Validate checksum
    bool validate_checksum() const;

    // Validate entire message
    bool is_valid() const;

    // Serialize to buffer
    std::vector<uint8_t> serialize() const;

    // Deserialize from buffer
    static Message deserialize(const void* data, size_t size);
    static Message deserialize(const std::vector<uint8_t>& data);

    // Total message size
    size_t total_size() const { return sizeof(MessageHeader) + payload_.size(); }

    // Create specific message types
    static Message create_connect_request(const std::string& username);
    static Message create_connect_response(UserId user_id, bool success, const std::string& message = "");
    static Message create_disconnect(UserId user_id, const std::string& reason = "");
    static Message create_heartbeat(UserId user_id);
    static Message create_heartbeat_ack(UserId user_id);
    static Message create_chat_message(UserId sender, const std::string& content);
    static Message create_private_message(UserId sender, UserId recipient, const std::string& content);
    static Message create_ack(SequenceNum seq);
    static Message create_nack(SequenceNum seq, const std::string& reason);
    static Message create_error(ErrorCode code, const std::string& message);
    static Message create_system_message(const std::string& content);
    static Message create_user_list(const std::vector<std::pair<UserId, std::string>>& users);

private:
    MessageHeader header_;
    std::vector<uint8_t> payload_;
};

// Message type to string conversion
const char* message_type_to_string(MessageType type);

// CRC32 checksum computation
uint32_t compute_crc32(const void* data, size_t size);
uint32_t update_crc32(uint32_t crc, const void* data, size_t size);

} // namespace protocol
} // namespace chatterbox

#endif // CHATTERBOX_PROTOCOL_MESSAGE_HPP
