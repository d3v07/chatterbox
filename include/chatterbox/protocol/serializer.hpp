#ifndef CHATTERBOX_PROTOCOL_SERIALIZER_HPP
#define CHATTERBOX_PROTOCOL_SERIALIZER_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/protocol/message.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace chatterbox {
namespace protocol {

// Exception for serialization errors
class SerializationError : public std::runtime_error {
public:
    explicit SerializationError(const std::string& msg) : std::runtime_error(msg) {}
};

// Binary serializer for writing data
class BinaryWriter {
public:
    explicit BinaryWriter(size_t initial_capacity = 256);

    // Write primitive types
    void write_u8(uint8_t value);
    void write_u16(uint16_t value);
    void write_u32(uint32_t value);
    void write_u64(uint64_t value);
    void write_i8(int8_t value);
    void write_i16(int16_t value);
    void write_i32(int32_t value);
    void write_i64(int64_t value);
    void write_float(float value);
    void write_double(double value);
    void write_bool(bool value);

    // Write raw bytes
    void write_bytes(const void* data, size_t size);
    void write_bytes(const std::vector<uint8_t>& data);

    // Write string (length-prefixed)
    void write_string(const std::string& str);

    // Write fixed-size string (padded or truncated)
    void write_fixed_string(const std::string& str, size_t size);

    // Reserve space and get position
    size_t reserve(size_t size);

    // Write at specific position
    void write_at(size_t pos, const void* data, size_t size);

    // Get the result
    const std::vector<uint8_t>& data() const { return buffer_; }
    std::vector<uint8_t> take_data() { return std::move(buffer_); }

    // Get current size
    size_t size() const { return buffer_.size(); }

    // Clear buffer
    void clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;

    void ensure_capacity(size_t additional);
};

// Binary deserializer for reading data
class BinaryReader {
public:
    BinaryReader(const void* data, size_t size);
    explicit BinaryReader(const std::vector<uint8_t>& data);

    // Read primitive types
    uint8_t read_u8();
    uint16_t read_u16();
    uint32_t read_u32();
    uint64_t read_u64();
    int8_t read_i8();
    int16_t read_i16();
    int32_t read_i32();
    int64_t read_i64();
    float read_float();
    double read_double();
    bool read_bool();

    // Read raw bytes
    void read_bytes(void* dest, size_t size);
    std::vector<uint8_t> read_bytes(size_t size);

    // Read string (length-prefixed)
    std::string read_string();

    // Read fixed-size string
    std::string read_fixed_string(size_t size);

    // Peek without advancing
    uint8_t peek_u8() const;

    // Skip bytes
    void skip(size_t count);

    // Position management
    size_t position() const { return pos_; }
    void set_position(size_t pos);
    size_t remaining() const { return size_ - pos_; }
    bool has_remaining(size_t count = 1) const { return remaining() >= count; }
    bool at_end() const { return pos_ >= size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    void check_remaining(size_t count) const;
};

// Message serializer/deserializer
class MessageSerializer {
public:
    // Serialize a message to bytes
    static std::vector<uint8_t> serialize(const Message& msg);

    // Deserialize a message from bytes
    static Message deserialize(const std::vector<uint8_t>& data);
    static Message deserialize(const void* data, size_t size);

    // Serialize header only
    static void serialize_header(BinaryWriter& writer, const MessageHeader& header);

    // Deserialize header only
    static MessageHeader deserialize_header(BinaryReader& reader);

    // Validate message structure
    static bool validate(const void* data, size_t size);

    // Get expected message size from header
    static size_t expected_size(const void* header_data);
};

// Payload serializers for specific message types
class PayloadSerializer {
public:
    // Connect request: username
    static std::vector<uint8_t> serialize_connect_request(const std::string& username);
    static std::string deserialize_connect_request(const std::vector<uint8_t>& payload);

    // Connect response: user_id, success, message
    static std::vector<uint8_t> serialize_connect_response(UserId user_id, bool success, const std::string& message);
    struct ConnectResponse {
        UserId user_id;
        bool success;
        std::string message;
    };
    static ConnectResponse deserialize_connect_response(const std::vector<uint8_t>& payload);

    // Chat message: content
    static std::vector<uint8_t> serialize_chat_message(const std::string& content);
    static std::string deserialize_chat_message(const std::vector<uint8_t>& payload);

    // Private message: recipient_id, content
    static std::vector<uint8_t> serialize_private_message(UserId recipient, const std::string& content);
    struct PrivateMessage {
        UserId recipient;
        std::string content;
    };
    static PrivateMessage deserialize_private_message(const std::vector<uint8_t>& payload);

    // User list: vector of (user_id, username)
    static std::vector<uint8_t> serialize_user_list(const std::vector<std::pair<UserId, std::string>>& users);
    static std::vector<std::pair<UserId, std::string>> deserialize_user_list(const std::vector<uint8_t>& payload);

    // Error: error_code, message
    static std::vector<uint8_t> serialize_error(ErrorCode code, const std::string& message);
    struct ErrorPayload {
        ErrorCode code;
        std::string message;
    };
    static ErrorPayload deserialize_error(const std::vector<uint8_t>& payload);
};

// Utility functions for endianness
namespace endian {

inline uint16_t to_network(uint16_t value) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

inline uint32_t to_network(uint32_t value) {
    return ((value & 0xFF) << 24) |
           ((value & 0xFF00) << 8) |
           ((value >> 8) & 0xFF00) |
           ((value >> 24) & 0xFF);
}

inline uint64_t to_network(uint64_t value) {
    return ((value & 0xFF) << 56) |
           ((value & 0xFF00) << 40) |
           ((value & 0xFF0000) << 24) |
           ((value & 0xFF000000) << 8) |
           ((value >> 8) & 0xFF000000) |
           ((value >> 24) & 0xFF0000) |
           ((value >> 40) & 0xFF00) |
           ((value >> 56) & 0xFF);
}

inline uint16_t from_network(uint16_t value) { return to_network(value); }
inline uint32_t from_network(uint32_t value) { return to_network(value); }
inline uint64_t from_network(uint64_t value) { return to_network(value); }

// Check system endianness
inline bool is_little_endian() {
    uint16_t value = 0x0001;
    return *reinterpret_cast<uint8_t*>(&value) == 0x01;
}

} // namespace endian

} // namespace protocol
} // namespace chatterbox

#endif // CHATTERBOX_PROTOCOL_SERIALIZER_HPP
