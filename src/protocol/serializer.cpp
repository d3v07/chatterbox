#include <chatterbox/protocol/serializer.hpp>
#include <algorithm>
#include <cstring>

namespace chatterbox {
namespace protocol {

// BinaryWriter implementation
BinaryWriter::BinaryWriter(size_t initial_capacity) {
    buffer_.reserve(initial_capacity);
}

void BinaryWriter::ensure_capacity(size_t additional) {
    if (buffer_.capacity() - buffer_.size() < additional) {
        buffer_.reserve(buffer_.capacity() * 2 + additional);
    }
}

void BinaryWriter::write_u8(uint8_t value) {
    buffer_.push_back(value);
}

void BinaryWriter::write_u16(uint16_t value) {
    ensure_capacity(2);
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void BinaryWriter::write_u32(uint32_t value) {
    ensure_capacity(4);
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void BinaryWriter::write_u64(uint64_t value) {
    ensure_capacity(8);
    for (int i = 0; i < 8; ++i) {
        buffer_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void BinaryWriter::write_i8(int8_t value) {
    write_u8(static_cast<uint8_t>(value));
}

void BinaryWriter::write_i16(int16_t value) {
    write_u16(static_cast<uint16_t>(value));
}

void BinaryWriter::write_i32(int32_t value) {
    write_u32(static_cast<uint32_t>(value));
}

void BinaryWriter::write_i64(int64_t value) {
    write_u64(static_cast<uint64_t>(value));
}

void BinaryWriter::write_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));
    write_u32(bits);
}

void BinaryWriter::write_double(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    write_u64(bits);
}

void BinaryWriter::write_bool(bool value) {
    write_u8(value ? 1 : 0);
}

void BinaryWriter::write_bytes(const void* data, size_t size) {
    ensure_capacity(size);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + size);
}

void BinaryWriter::write_bytes(const std::vector<uint8_t>& data) {
    write_bytes(data.data(), data.size());
}

void BinaryWriter::write_string(const std::string& str) {
    // Length-prefixed string (4-byte length + data)
    write_u32(static_cast<uint32_t>(str.size()));
    write_bytes(str.data(), str.size());
}

void BinaryWriter::write_fixed_string(const std::string& str, size_t size) {
    ensure_capacity(size);
    size_t copy_len = std::min(str.size(), size);
    buffer_.insert(buffer_.end(), str.begin(), str.begin() + copy_len);
    // Pad with zeros
    for (size_t i = copy_len; i < size; ++i) {
        buffer_.push_back(0);
    }
}

size_t BinaryWriter::reserve(size_t size) {
    size_t pos = buffer_.size();
    buffer_.resize(buffer_.size() + size);
    return pos;
}

void BinaryWriter::write_at(size_t pos, const void* data, size_t size) {
    if (pos + size > buffer_.size()) {
        throw SerializationError("Write position out of bounds");
    }
    std::memcpy(buffer_.data() + pos, data, size);
}

// BinaryReader implementation
BinaryReader::BinaryReader(const void* data, size_t size)
    : data_(static_cast<const uint8_t*>(data))
    , size_(size)
    , pos_(0)
{
}

BinaryReader::BinaryReader(const std::vector<uint8_t>& data)
    : data_(data.data())
    , size_(data.size())
    , pos_(0)
{
}

void BinaryReader::check_remaining(size_t count) const {
    if (remaining() < count) {
        throw SerializationError("Not enough data to read");
    }
}

uint8_t BinaryReader::read_u8() {
    check_remaining(1);
    return data_[pos_++];
}

uint16_t BinaryReader::read_u16() {
    check_remaining(2);
    uint16_t value = data_[pos_] | (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return value;
}

uint32_t BinaryReader::read_u32() {
    check_remaining(4);
    uint32_t value = data_[pos_] |
                    (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                    (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                    (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return value;
}

uint64_t BinaryReader::read_u64() {
    check_remaining(8);
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
    }
    pos_ += 8;
    return value;
}

int8_t BinaryReader::read_i8() {
    return static_cast<int8_t>(read_u8());
}

int16_t BinaryReader::read_i16() {
    return static_cast<int16_t>(read_u16());
}

int32_t BinaryReader::read_i32() {
    return static_cast<int32_t>(read_u32());
}

int64_t BinaryReader::read_i64() {
    return static_cast<int64_t>(read_u64());
}

float BinaryReader::read_float() {
    uint32_t bits = read_u32();
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

double BinaryReader::read_double() {
    uint64_t bits = read_u64();
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

bool BinaryReader::read_bool() {
    return read_u8() != 0;
}

void BinaryReader::read_bytes(void* dest, size_t size) {
    check_remaining(size);
    std::memcpy(dest, data_ + pos_, size);
    pos_ += size;
}

std::vector<uint8_t> BinaryReader::read_bytes(size_t size) {
    check_remaining(size);
    std::vector<uint8_t> result(data_ + pos_, data_ + pos_ + size);
    pos_ += size;
    return result;
}

std::string BinaryReader::read_string() {
    uint32_t length = read_u32();
    check_remaining(length);
    std::string result(reinterpret_cast<const char*>(data_ + pos_), length);
    pos_ += length;
    return result;
}

std::string BinaryReader::read_fixed_string(size_t size) {
    check_remaining(size);
    // Find null terminator or use full size
    size_t len = 0;
    while (len < size && data_[pos_ + len] != 0) {
        ++len;
    }
    std::string result(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += size;
    return result;
}

uint8_t BinaryReader::peek_u8() const {
    check_remaining(1);
    return data_[pos_];
}

void BinaryReader::skip(size_t count) {
    check_remaining(count);
    pos_ += count;
}

void BinaryReader::set_position(size_t pos) {
    if (pos > size_) {
        throw SerializationError("Position out of bounds");
    }
    pos_ = pos;
}

// MessageSerializer implementation
std::vector<uint8_t> MessageSerializer::serialize(const Message& msg) {
    return msg.serialize();
}

Message MessageSerializer::deserialize(const std::vector<uint8_t>& data) {
    return Message::deserialize(data);
}

Message MessageSerializer::deserialize(const void* data, size_t size) {
    return Message::deserialize(data, size);
}

void MessageSerializer::serialize_header(BinaryWriter& writer, const MessageHeader& header) {
    writer.write_u16(header.magic);
    writer.write_u8(header.version_major);
    writer.write_u8(header.version_minor);
    writer.write_u8(header.type);
    writer.write_u8(header.flags);
    writer.write_u16(header.reserved);
    writer.write_u64(header.timestamp);
    writer.write_u32(header.sequence);
    writer.write_u32(header.sender_id);
    writer.write_u32(header.payload_length);
    writer.write_u32(header.checksum);
}

MessageHeader MessageSerializer::deserialize_header(BinaryReader& reader) {
    MessageHeader header;
    header.magic = reader.read_u16();
    header.version_major = reader.read_u8();
    header.version_minor = reader.read_u8();
    header.type = reader.read_u8();
    header.flags = reader.read_u8();
    header.reserved = reader.read_u16();
    header.timestamp = reader.read_u64();
    header.sequence = reader.read_u32();
    header.sender_id = reader.read_u32();
    header.payload_length = reader.read_u32();
    header.checksum = reader.read_u32();
    return header;
}

bool MessageSerializer::validate(const void* data, size_t size) {
    if (size < sizeof(MessageHeader)) {
        return false;
    }

    try {
        auto msg = Message::deserialize(data, size);
        return msg.is_valid();
    } catch (...) {
        return false;
    }
}

size_t MessageSerializer::expected_size(const void* header_data) {
    const MessageHeader* header = static_cast<const MessageHeader*>(header_data);
    return sizeof(MessageHeader) + header->payload_length;
}

// PayloadSerializer implementation
std::vector<uint8_t> PayloadSerializer::serialize_connect_request(const std::string& username) {
    BinaryWriter writer;
    writer.write_string(username);
    return writer.take_data();
}

std::string PayloadSerializer::deserialize_connect_request(const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    return reader.read_string();
}

std::vector<uint8_t> PayloadSerializer::serialize_connect_response(
    UserId user_id, bool success, const std::string& message) {
    BinaryWriter writer;
    writer.write_u32(user_id);
    writer.write_bool(success);
    writer.write_string(message);
    return writer.take_data();
}

PayloadSerializer::ConnectResponse PayloadSerializer::deserialize_connect_response(
    const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    ConnectResponse response;
    response.user_id = reader.read_u32();
    response.success = reader.read_bool();
    response.message = reader.read_string();
    return response;
}

std::vector<uint8_t> PayloadSerializer::serialize_chat_message(const std::string& content) {
    BinaryWriter writer;
    writer.write_string(content);
    return writer.take_data();
}

std::string PayloadSerializer::deserialize_chat_message(const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    return reader.read_string();
}

std::vector<uint8_t> PayloadSerializer::serialize_private_message(
    UserId recipient, const std::string& content) {
    BinaryWriter writer;
    writer.write_u32(recipient);
    writer.write_string(content);
    return writer.take_data();
}

PayloadSerializer::PrivateMessage PayloadSerializer::deserialize_private_message(
    const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    PrivateMessage msg;
    msg.recipient = reader.read_u32();
    msg.content = reader.read_string();
    return msg;
}

std::vector<uint8_t> PayloadSerializer::serialize_user_list(
    const std::vector<std::pair<UserId, std::string>>& users) {
    BinaryWriter writer;
    writer.write_u32(static_cast<uint32_t>(users.size()));
    for (const auto& [user_id, username] : users) {
        writer.write_u32(user_id);
        writer.write_string(username);
    }
    return writer.take_data();
}

std::vector<std::pair<UserId, std::string>> PayloadSerializer::deserialize_user_list(
    const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    uint32_t count = reader.read_u32();
    std::vector<std::pair<UserId, std::string>> users;
    users.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        UserId id = reader.read_u32();
        std::string name = reader.read_string();
        users.emplace_back(id, std::move(name));
    }
    return users;
}

std::vector<uint8_t> PayloadSerializer::serialize_error(ErrorCode code, const std::string& message) {
    BinaryWriter writer;
    writer.write_i32(static_cast<int32_t>(code));
    writer.write_string(message);
    return writer.take_data();
}

PayloadSerializer::ErrorPayload PayloadSerializer::deserialize_error(
    const std::vector<uint8_t>& payload) {
    BinaryReader reader(payload);
    ErrorPayload err;
    err.code = static_cast<ErrorCode>(reader.read_i32());
    err.message = reader.read_string();
    return err;
}

} // namespace protocol
} // namespace chatterbox
