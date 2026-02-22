#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/serializer.hpp>
#include <cstring>
#include <algorithm>

namespace chatterbox {
namespace protocol {

// CRC32 lookup table
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7d43, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t compute_crc32(const void* data, size_t size) {
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; ++i) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

uint32_t update_crc32(uint32_t crc, const void* data, size_t size) {
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    crc = ~crc;

    for (size_t i = 0; i < size; ++i) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

// MessageHeader implementation
MessageHeader::MessageHeader() {
    init();
}

void MessageHeader::init() {
    magic = PROTOCOL_MAGIC;
    version_major = PROTOCOL_VERSION_MAJOR;
    version_minor = PROTOCOL_VERSION_MINOR;
    type = static_cast<uint8_t>(MessageType::UNKNOWN);
    flags = static_cast<uint8_t>(MessageFlags::NONE);
    reserved = 0;
    timestamp = Timestamp::now().value();
    sequence = 0;
    sender_id = INVALID_USER_ID;
    payload_length = 0;
    checksum = 0;
}

bool MessageHeader::validate() const {
    if (magic != PROTOCOL_MAGIC) {
        return false;
    }
    if (version_major != PROTOCOL_VERSION_MAJOR) {
        return false;
    }
    if (payload_length > MAX_PAYLOAD_SIZE) {
        return false;
    }
    return true;
}

uint32_t MessageHeader::compute_checksum(const void* payload, size_t payload_len) const {
    // Create a copy of header with checksum set to 0
    MessageHeader temp = *this;
    temp.checksum = 0;

    // Compute CRC of header
    uint32_t crc = compute_crc32(&temp, sizeof(MessageHeader));

    // Update with payload
    if (payload && payload_len > 0) {
        crc = update_crc32(crc, payload, payload_len);
    }

    return crc;
}

// Message implementation
Message::Message() {
    header_.init();
}

Message::Message(MessageType type) {
    header_.init();
    header_.type = static_cast<uint8_t>(type);
}

Message::Message(MessageType type, const std::string& payload) {
    header_.init();
    header_.type = static_cast<uint8_t>(type);
    set_payload(payload);
}

Message::Message(MessageType type, const void* data, size_t size) {
    header_.init();
    header_.type = static_cast<uint8_t>(type);
    set_payload(data, size);
}

Message::Message(const Message& other)
    : header_(other.header_)
    , payload_(other.payload_)
{
}

Message::Message(Message&& other) noexcept
    : header_(other.header_)
    , payload_(std::move(other.payload_))
{
}

Message& Message::operator=(const Message& other) {
    if (this != &other) {
        header_ = other.header_;
        payload_ = other.payload_;
    }
    return *this;
}

Message& Message::operator=(Message&& other) noexcept {
    if (this != &other) {
        header_ = other.header_;
        payload_ = std::move(other.payload_);
    }
    return *this;
}

void Message::set_payload(const void* data, size_t size) {
    if (size > MAX_PAYLOAD_SIZE) {
        size = MAX_PAYLOAD_SIZE;
    }
    payload_.resize(size);
    if (size > 0 && data) {
        std::memcpy(payload_.data(), data, size);
    }
    header_.payload_length = static_cast<uint32_t>(payload_.size());
}

void Message::set_payload(const std::string& str) {
    set_payload(str.data(), str.size());
}

void Message::set_payload(const std::vector<uint8_t>& data) {
    set_payload(data.data(), data.size());
}

std::string Message::payload_as_string() const {
    return std::string(reinterpret_cast<const char*>(payload_.data()), payload_.size());
}

void Message::update_checksum() {
    header_.payload_length = static_cast<uint32_t>(payload_.size());
    header_.checksum = header_.compute_checksum(payload_.data(), payload_.size());
}

bool Message::validate_checksum() const {
    uint32_t expected = header_.compute_checksum(payload_.data(), payload_.size());
    return header_.checksum == expected;
}

bool Message::is_valid() const {
    return header_.validate() && validate_checksum();
}

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + payload_.size());
    std::memcpy(buffer.data(), &header_, sizeof(MessageHeader));
    if (!payload_.empty()) {
        std::memcpy(buffer.data() + sizeof(MessageHeader), payload_.data(), payload_.size());
    }
    return buffer;
}

Message Message::deserialize(const void* data, size_t size) {
    if (size < sizeof(MessageHeader)) {
        throw std::runtime_error("Buffer too small for message header");
    }

    Message msg;
    std::memcpy(&msg.header_, data, sizeof(MessageHeader));

    if (!msg.header_.validate()) {
        throw std::runtime_error("Invalid message header");
    }

    size_t payload_size = msg.header_.payload_length;
    if (size < sizeof(MessageHeader) + payload_size) {
        throw std::runtime_error("Buffer too small for payload");
    }

    if (payload_size > 0) {
        msg.payload_.resize(payload_size);
        std::memcpy(msg.payload_.data(),
                   static_cast<const uint8_t*>(data) + sizeof(MessageHeader),
                   payload_size);
    }

    return msg;
}

Message Message::deserialize(const std::vector<uint8_t>& data) {
    return deserialize(data.data(), data.size());
}

// Factory methods for creating specific message types
Message Message::create_connect_request(const std::string& username) {
    Message msg(MessageType::CONNECT_REQUEST);
    msg.set_payload(PayloadSerializer::serialize_connect_request(username));
    msg.update_checksum();
    return msg;
}

Message Message::create_connect_response(UserId user_id, bool success, const std::string& message) {
    Message msg(MessageType::CONNECT_RESPONSE);
    msg.set_payload(PayloadSerializer::serialize_connect_response(user_id, success, message));
    msg.set_sender_id(SERVER_USER_ID);
    msg.update_checksum();
    return msg;
}

Message Message::create_disconnect(UserId user_id, const std::string& reason) {
    Message msg(MessageType::DISCONNECT);
    msg.set_sender_id(user_id);
    msg.set_payload(reason);
    msg.update_checksum();
    return msg;
}

Message Message::create_heartbeat(UserId user_id) {
    Message msg(MessageType::HEARTBEAT);
    msg.set_sender_id(user_id);
    msg.update_checksum();
    return msg;
}

Message Message::create_heartbeat_ack(UserId user_id) {
    Message msg(MessageType::HEARTBEAT_ACK);
    msg.set_sender_id(SERVER_USER_ID);
    // Echo back the user_id in payload for verification
    BinaryWriter writer;
    writer.write_u32(user_id);
    msg.set_payload(writer.data());
    msg.update_checksum();
    return msg;
}

Message Message::create_chat_message(UserId sender, const std::string& content) {
    Message msg(MessageType::CHAT_MESSAGE);
    msg.set_sender_id(sender);
    msg.set_payload(PayloadSerializer::serialize_chat_message(content));
    msg.update_checksum();
    return msg;
}

Message Message::create_private_message(UserId sender, UserId recipient, const std::string& content) {
    Message msg(MessageType::PRIVATE_MESSAGE);
    msg.set_sender_id(sender);
    msg.set_payload(PayloadSerializer::serialize_private_message(recipient, content));
    msg.update_checksum();
    return msg;
}

Message Message::create_ack(SequenceNum seq) {
    Message msg(MessageType::ACK);
    msg.set_sender_id(SERVER_USER_ID);
    BinaryWriter writer;
    writer.write_u32(seq);
    msg.set_payload(writer.data());
    msg.update_checksum();
    return msg;
}

Message Message::create_nack(SequenceNum seq, const std::string& reason) {
    Message msg(MessageType::NACK);
    msg.set_sender_id(SERVER_USER_ID);
    BinaryWriter writer;
    writer.write_u32(seq);
    writer.write_string(reason);
    msg.set_payload(writer.data());
    msg.update_checksum();
    return msg;
}

Message Message::create_error(ErrorCode code, const std::string& message) {
    Message msg(MessageType::ERROR_MSG);
    msg.set_sender_id(SERVER_USER_ID);
    msg.set_payload(PayloadSerializer::serialize_error(code, message));
    msg.update_checksum();
    return msg;
}

Message Message::create_system_message(const std::string& content) {
    Message msg(MessageType::SYSTEM_MESSAGE);
    msg.set_sender_id(SERVER_USER_ID);
    msg.set_payload(content);
    msg.update_checksum();
    return msg;
}

Message Message::create_user_list(const std::vector<std::pair<UserId, std::string>>& users) {
    Message msg(MessageType::USER_LIST);
    msg.set_sender_id(SERVER_USER_ID);
    msg.set_payload(PayloadSerializer::serialize_user_list(users));
    msg.update_checksum();
    return msg;
}

const char* message_type_to_string(MessageType type) {
    switch (type) {
        case MessageType::CONNECT_REQUEST: return "CONNECT_REQUEST";
        case MessageType::CONNECT_RESPONSE: return "CONNECT_RESPONSE";
        case MessageType::DISCONNECT: return "DISCONNECT";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        case MessageType::HEARTBEAT_ACK: return "HEARTBEAT_ACK";
        case MessageType::CHAT_MESSAGE: return "CHAT_MESSAGE";
        case MessageType::CHAT_BROADCAST: return "CHAT_BROADCAST";
        case MessageType::PRIVATE_MESSAGE: return "PRIVATE_MESSAGE";
        case MessageType::ROOM_JOIN: return "ROOM_JOIN";
        case MessageType::ROOM_LEAVE: return "ROOM_LEAVE";
        case MessageType::ROOM_CREATE: return "ROOM_CREATE";
        case MessageType::ROOM_LIST: return "ROOM_LIST";
        case MessageType::ROOM_USERS: return "ROOM_USERS";
        case MessageType::USER_LIST: return "USER_LIST";
        case MessageType::USER_INFO: return "USER_INFO";
        case MessageType::USER_STATUS: return "USER_STATUS";
        case MessageType::ACK: return "ACK";
        case MessageType::NACK: return "NACK";
        case MessageType::ERROR_MSG: return "ERROR";
        case MessageType::SYSTEM_MESSAGE: return "SYSTEM_MESSAGE";
        default: return "UNKNOWN";
    }
}

} // namespace protocol
} // namespace chatterbox
