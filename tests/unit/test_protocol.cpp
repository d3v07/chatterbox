#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/serializer.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <iostream>
#include <cassert>
#include <cstring>

using namespace chatterbox;
using namespace chatterbox::protocol;

void test_timestamp() {
    std::cout << "Testing Timestamp..." << std::endl;

    // Test basic timestamp creation
    Timestamp t1;
    assert(t1.is_valid());
    assert(!t1.is_future());

    // Test epoch
    Timestamp epoch = Timestamp::epoch();
    assert(epoch.value() == 0);
    assert(!epoch.is_valid());

    // Test comparison
    Timestamp t2 = Timestamp::now();
    assert(t2 >= t1);

    // Test arithmetic
    Timestamp t3 = t1 + 1000000; // 1 second
    assert(t3 > t1);
    assert((t3 - t1) == 1000000);

    // Test string conversion
    std::string str = t1.to_string();
    assert(!str.empty());
    assert(str.find("T") != std::string::npos);

    std::cout << "  Timestamp tests passed!" << std::endl;
}

void test_timestamp_generator() {
    std::cout << "Testing TimestampGenerator..." << std::endl;

    TimestampGenerator gen;

    // Generate multiple timestamps
    Timestamp prev = gen.next();
    for (int i = 0; i < 1000; ++i) {
        Timestamp curr = gen.next();
        assert(curr > prev);
        prev = curr;
    }

    // Test with sequence
    auto [ts, seq] = gen.next_with_sequence();
    assert(ts.is_valid());
    assert(seq > 0);

    std::cout << "  TimestampGenerator tests passed!" << std::endl;
}

void test_message_header() {
    std::cout << "Testing MessageHeader..." << std::endl;

    MessageHeader header;
    header.init();

    assert(header.magic == PROTOCOL_MAGIC);
    assert(header.version_major == PROTOCOL_VERSION_MAJOR);
    assert(header.version_minor == PROTOCOL_VERSION_MINOR);
    assert(header.validate());

    // Test size
    assert(sizeof(MessageHeader) == 32);

    std::cout << "  MessageHeader tests passed!" << std::endl;
}

void test_message_creation() {
    std::cout << "Testing Message creation..." << std::endl;

    // Test default constructor
    Message m1;
    assert(m1.type() == MessageType::UNKNOWN);
    assert(m1.payload_size() == 0);

    // Test constructor with type
    Message m2(MessageType::CHAT_MESSAGE);
    assert(m2.type() == MessageType::CHAT_MESSAGE);

    // Test constructor with payload
    Message m3(MessageType::CHAT_MESSAGE, "Hello, World!");
    assert(m3.type() == MessageType::CHAT_MESSAGE);
    assert(m3.payload_size() == 13);
    assert(m3.payload_as_string() == "Hello, World!");

    std::cout << "  Message creation tests passed!" << std::endl;
}

void test_message_serialization() {
    std::cout << "Testing Message serialization..." << std::endl;

    // Create a message
    Message original(MessageType::CHAT_MESSAGE, "Test message content");
    original.set_sender_id(42);
    original.set_sequence(123);
    original.update_checksum();

    // Serialize
    auto data = original.serialize();
    assert(data.size() == sizeof(MessageHeader) + original.payload_size());

    // Deserialize
    Message restored = Message::deserialize(data);

    assert(restored.type() == original.type());
    assert(restored.sender_id() == original.sender_id());
    assert(restored.sequence() == original.sequence());
    assert(restored.payload_as_string() == original.payload_as_string());
    assert(restored.validate_checksum());
    assert(restored.is_valid());

    std::cout << "  Message serialization tests passed!" << std::endl;
}

void test_message_factory() {
    std::cout << "Testing Message factory methods..." << std::endl;

    // Connect request
    auto connect = Message::create_connect_request("TestUser");
    assert(connect.type() == MessageType::CONNECT_REQUEST);
    assert(connect.is_valid());

    // Connect response
    auto response = Message::create_connect_response(42, true, "Welcome!");
    assert(response.type() == MessageType::CONNECT_RESPONSE);
    assert(response.is_valid());

    // Chat message
    auto chat = Message::create_chat_message(42, "Hello everyone!");
    assert(chat.type() == MessageType::CHAT_MESSAGE);
    assert(chat.sender_id() == 42);
    assert(chat.is_valid());

    // Heartbeat
    auto hb = Message::create_heartbeat(42);
    assert(hb.type() == MessageType::HEARTBEAT);
    assert(hb.is_valid());

    // System message
    auto sys = Message::create_system_message("Server announcement");
    assert(sys.type() == MessageType::SYSTEM_MESSAGE);
    assert(sys.is_valid());

    std::cout << "  Message factory tests passed!" << std::endl;
}

void test_binary_writer_reader() {
    std::cout << "Testing BinaryWriter/Reader..." << std::endl;

    BinaryWriter writer;

    // Write various types
    writer.write_u8(0xFF);
    writer.write_u16(0x1234);
    writer.write_u32(0xDEADBEEF);
    writer.write_u64(0x123456789ABCDEF0ULL);
    writer.write_i32(-42);
    writer.write_float(3.14f);
    writer.write_double(2.718281828);
    writer.write_bool(true);
    writer.write_string("Hello");
    writer.write_fixed_string("World", 10);

    auto data = writer.data();

    // Read back
    BinaryReader reader(data);

    assert(reader.read_u8() == 0xFF);
    assert(reader.read_u16() == 0x1234);
    assert(reader.read_u32() == 0xDEADBEEF);
    assert(reader.read_u64() == 0x123456789ABCDEF0ULL);
    assert(reader.read_i32() == -42);
    assert(std::abs(reader.read_float() - 3.14f) < 0.001f);
    assert(std::abs(reader.read_double() - 2.718281828) < 0.0001);
    assert(reader.read_bool() == true);
    assert(reader.read_string() == "Hello");
    assert(reader.read_fixed_string(10) == "World");
    assert(reader.at_end());

    std::cout << "  BinaryWriter/Reader tests passed!" << std::endl;
}

void test_payload_serializers() {
    std::cout << "Testing PayloadSerializers..." << std::endl;

    // Connect request
    {
        auto payload = PayloadSerializer::serialize_connect_request("TestUser");
        auto result = PayloadSerializer::deserialize_connect_request(payload);
        assert(result == "TestUser");
    }

    // Connect response
    {
        auto payload = PayloadSerializer::serialize_connect_response(42, true, "Welcome");
        auto result = PayloadSerializer::deserialize_connect_response(payload);
        assert(result.user_id == 42);
        assert(result.success == true);
        assert(result.message == "Welcome");
    }

    // Chat message
    {
        auto payload = PayloadSerializer::serialize_chat_message("Hello!");
        auto result = PayloadSerializer::deserialize_chat_message(payload);
        assert(result == "Hello!");
    }

    // Private message
    {
        auto payload = PayloadSerializer::serialize_private_message(42, "Secret");
        auto result = PayloadSerializer::deserialize_private_message(payload);
        assert(result.recipient == 42);
        assert(result.content == "Secret");
    }

    // User list
    {
        std::vector<std::pair<UserId, std::string>> users = {
            {1, "Alice"},
            {2, "Bob"},
            {3, "Charlie"}
        };
        auto payload = PayloadSerializer::serialize_user_list(users);
        auto result = PayloadSerializer::deserialize_user_list(payload);
        assert(result.size() == 3);
        assert(result[0].first == 1 && result[0].second == "Alice");
        assert(result[1].first == 2 && result[1].second == "Bob");
        assert(result[2].first == 3 && result[2].second == "Charlie");
    }

    // Error
    {
        auto payload = PayloadSerializer::serialize_error(ErrorCode::ERR_TIMEOUT, "Timeout occurred");
        auto result = PayloadSerializer::deserialize_error(payload);
        assert(result.code == ErrorCode::ERR_TIMEOUT);
        assert(result.message == "Timeout occurred");
    }

    std::cout << "  PayloadSerializer tests passed!" << std::endl;
}

void test_checksum() {
    std::cout << "Testing CRC32 checksum..." << std::endl;

    // Test known value
    const char* data = "123456789";
    uint32_t crc = compute_crc32(data, 9);
    assert(crc == 0xCBF43926);  // Standard CRC32 test vector

    // Test incremental update
    uint32_t crc1 = compute_crc32("12345", 5);
    uint32_t crc2 = update_crc32(crc1, "6789", 4);
    assert(crc2 == crc);

    // Test empty data
    uint32_t empty_crc = compute_crc32("", 0);
    assert(empty_crc == 0);

    std::cout << "  CRC32 tests passed!" << std::endl;
}

void test_message_validation() {
    std::cout << "Testing Message validation..." << std::endl;

    // Valid message
    Message valid(MessageType::CHAT_MESSAGE, "Hello");
    valid.update_checksum();
    assert(valid.is_valid());

    // Tampered payload
    Message tampered(MessageType::CHAT_MESSAGE, "Hello");
    tampered.update_checksum();
    tampered.payload()[0] = 'X'; // Modify payload
    assert(!tampered.validate_checksum());
    assert(!tampered.is_valid());

    // Invalid magic
    Message invalid_magic;
    invalid_magic.header().magic = 0x0000;
    assert(!invalid_magic.header().validate());

    // Invalid version
    Message invalid_version;
    invalid_version.header().version_major = 99;
    assert(!invalid_version.header().validate());

    std::cout << "  Message validation tests passed!" << std::endl;
}

int main() {
    std::cout << "=== ChatterBox Protocol Unit Tests ===" << std::endl;

    try {
        test_timestamp();
        test_timestamp_generator();
        test_message_header();
        test_message_creation();
        test_message_serialization();
        test_message_factory();
        test_binary_writer_reader();
        test_payload_serializers();
        test_checksum();
        test_message_validation();

        std::cout << "\n=== All protocol tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
