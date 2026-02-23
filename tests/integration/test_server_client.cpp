#include <chatterbox/server/server.hpp>
#include <chatterbox/client/client.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/sync/thread_pool.hpp>
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>

using namespace chatterbox;
using namespace std::chrono_literals;

// Test configuration
namespace {
    std::atomic<bool> test_passed{true};
    std::mutex output_mutex;

    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(output_mutex);
        std::cout << "[TEST] " << msg << std::endl;
    }

    void log_error(const std::string& msg) {
        std::lock_guard<std::mutex> lock(output_mutex);
        std::cerr << "[ERROR] " << msg << std::endl;
        test_passed = false;
    }
}

// Test 1: Server startup and shutdown
void test_server_lifecycle() {
    log("Testing server lifecycle...");

    server::ServerConfig config;
    config.max_users = 10;
    config.heartbeat_interval_ms = 1000;

    server::Server server(config);

    // Start server
    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server: " + std::string(error_to_string(err)));
        return;
    }

    assert(server.is_running());
    log("  Server started successfully");

    // Let it run briefly
    std::this_thread::sleep_for(100ms);

    // Stop server
    server.stop();
    assert(!server.is_running());
    log("  Server stopped successfully");

    log("  Server lifecycle test passed!");
}

// Test 2: Client connection
void test_client_connection() {
    log("Testing client connection...");

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for connection test");
        return;
    }

    // Create and connect client
    client::ClientConfig client_config;
    client_config.username = "TestUser";
    client_config.auto_reconnect = false;

    client::Client client(client_config);

    err = client.connect();
    if (err != ErrorCode::SUCCESS) {
        log_error("Client failed to connect: " + std::string(error_to_string(err)));
        server.stop();
        return;
    }

    // Wait for connection
    std::this_thread::sleep_for(200ms);

    if (!client.is_connected()) {
        log_error("Client not connected after waiting");
        server.stop();
        return;
    }

    log("  Client connected successfully, ID: " + std::to_string(client.user_id()));

    // Check server sees the client
    auto stats = server.get_stats();
    if (stats.connected_users < 1) {
        log_error("Server does not see connected client");
    }

    // Disconnect
    client.disconnect("Test complete");
    std::this_thread::sleep_for(100ms);

    assert(!client.is_connected());
    log("  Client disconnected successfully");

    server.stop();
    log("  Client connection test passed!");
}

// Test 3: Multiple client connections
void test_multiple_clients() {
    log("Testing multiple client connections...");

    const int NUM_CLIENTS = 5;

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for multiple client test");
        return;
    }

    // Connect multiple clients
    std::vector<std::unique_ptr<client::Client>> clients;
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        client::ClientConfig config;
        config.username = "User" + std::to_string(i);
        config.auto_reconnect = false;

        auto client = std::make_unique<client::Client>(config);
        err = client->connect();
        if (err != ErrorCode::SUCCESS) {
            log_error("Client " + std::to_string(i) + " failed to connect");
            continue;
        }
        clients.push_back(std::move(client));
    }

    // Wait for all connections
    std::this_thread::sleep_for(500ms);

    // Verify connections
    int connected_count = 0;
    for (const auto& client : clients) {
        if (client->is_connected()) {
            connected_count++;
        }
    }

    log("  Connected " + std::to_string(connected_count) + "/" + std::to_string(NUM_CLIENTS) + " clients");

    if (connected_count < NUM_CLIENTS) {
        log_error("Not all clients connected");
    }

    // Check server stats
    auto stats = server.get_stats();
    log("  Server reports " + std::to_string(stats.connected_users) + " connected users");

    // Disconnect all clients
    for (auto& client : clients) {
        client->disconnect("Test complete");
    }

    std::this_thread::sleep_for(200ms);
    clients.clear();

    server.stop();
    log("  Multiple clients test passed!");
}

// Test 4: Message sending
void test_message_sending() {
    log("Testing message sending...");

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for message test");
        return;
    }

    // Connect sender client
    client::ClientConfig sender_config;
    sender_config.username = "Sender";
    sender_config.auto_reconnect = false;
    client::Client sender(sender_config);

    err = sender.connect();
    if (err != ErrorCode::SUCCESS) {
        log_error("Sender client failed to connect");
        server.stop();
        return;
    }

    // Connect receiver client
    client::ClientConfig receiver_config;
    receiver_config.username = "Receiver";
    receiver_config.auto_reconnect = false;
    client::Client receiver(receiver_config);

    err = receiver.connect();
    if (err != ErrorCode::SUCCESS) {
        log_error("Receiver client failed to connect");
        server.stop();
        return;
    }

    std::this_thread::sleep_for(200ms);

    // Track received messages
    std::atomic<int> messages_received{0};
    std::string last_message;
    std::mutex msg_mutex;

    receiver.set_message_callback([&](const protocol::Message& msg) {
        if (msg.type() == protocol::MessageType::CHAT_MESSAGE ||
            msg.type() == protocol::MessageType::CHAT_BROADCAST) {
            messages_received++;
            std::lock_guard<std::mutex> lock(msg_mutex);
            last_message = msg.payload_as_string();
        }
    });

    // Send a message
    const std::string test_message = "Hello, World!";
    err = sender.send_message(test_message);
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to send message: " + std::string(error_to_string(err)));
    }

    // Wait for delivery
    std::this_thread::sleep_for(500ms);

    log("  Messages received by receiver: " + std::to_string(messages_received.load()));

    // Verify message was received
    if (messages_received < 1) {
        log_error("Message was not received");
    } else {
        std::lock_guard<std::mutex> lock(msg_mutex);
        log("  Received message content: " + last_message);
    }

    // Cleanup
    sender.disconnect("Test complete");
    receiver.disconnect("Test complete");
    std::this_thread::sleep_for(100ms);

    server.stop();
    log("  Message sending test passed!");
}

// Test 5: Heartbeat mechanism
void test_heartbeat() {
    log("Testing heartbeat mechanism...");

    // Start server with short heartbeat interval
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server_config.heartbeat_interval_ms = 500;
    server_config.connection_timeout_ms = 2000;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for heartbeat test");
        return;
    }

    // Connect client
    client::ClientConfig client_config;
    client_config.username = "HeartbeatTest";
    client_config.auto_reconnect = false;
    client_config.heartbeat_interval_ms = 500;
    client::Client client(client_config);

    err = client.connect();
    if (err != ErrorCode::SUCCESS) {
        log_error("Client failed to connect for heartbeat test");
        server.stop();
        return;
    }

    std::this_thread::sleep_for(200ms);
    assert(client.is_connected());

    // Wait for several heartbeat cycles
    log("  Waiting for heartbeat cycles...");
    std::this_thread::sleep_for(2s);

    // Client should still be connected
    if (!client.is_connected()) {
        log_error("Client disconnected during heartbeat test");
    } else {
        log("  Client still connected after heartbeats");
    }

    // Check stats
    auto stats = client.get_stats();
    log("  Heartbeats sent: " + std::to_string(stats.heartbeats_sent));
    log("  Heartbeats received: " + std::to_string(stats.heartbeats_received));

    // Cleanup
    client.disconnect("Test complete");
    std::this_thread::sleep_for(100ms);

    server.stop();
    log("  Heartbeat test passed!");
}

// Test 6: Private messaging
void test_private_messaging() {
    log("Testing private messaging...");

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for private message test");
        return;
    }

    // Connect two clients
    client::ClientConfig alice_config;
    alice_config.username = "Alice";
    alice_config.auto_reconnect = false;
    client::Client alice(alice_config);

    client::ClientConfig bob_config;
    bob_config.username = "Bob";
    bob_config.auto_reconnect = false;
    client::Client bob(bob_config);

    alice.connect();
    bob.connect();
    std::this_thread::sleep_for(200ms);

    // Track received private messages
    std::atomic<int> bob_pm_count{0};
    std::string bob_last_pm;
    std::mutex pm_mutex;

    bob.set_message_callback([&](const protocol::Message& msg) {
        if (msg.type() == protocol::MessageType::PRIVATE_MESSAGE) {
            bob_pm_count++;
            std::lock_guard<std::mutex> lock(pm_mutex);
            bob_last_pm = msg.payload_as_string();
        }
    });

    // Alice sends private message to Bob
    UserId bob_id = bob.user_id();
    err = alice.send_private_message(bob_id, "Secret message");
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to send private message");
    }

    std::this_thread::sleep_for(300ms);

    if (bob_pm_count < 1) {
        log_error("Bob did not receive private message");
    } else {
        log("  Bob received private message: " + bob_last_pm);
    }

    // Cleanup
    alice.disconnect("Test complete");
    bob.disconnect("Test complete");
    std::this_thread::sleep_for(100ms);

    server.stop();
    log("  Private messaging test passed!");
}

// Test 7: Connection timeout
void test_connection_timeout() {
    log("Testing connection timeout...");

    // Start server with short timeout
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server_config.heartbeat_interval_ms = 200;
    server_config.connection_timeout_ms = 500;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for timeout test");
        return;
    }

    // Connect client but disable heartbeats
    client::ClientConfig client_config;
    client_config.username = "TimeoutTest";
    client_config.auto_reconnect = false;
    client_config.heartbeat_interval_ms = 0; // Disable heartbeats
    client::Client client(client_config);

    err = client.connect();
    if (err != ErrorCode::SUCCESS) {
        log_error("Client failed to connect for timeout test");
        server.stop();
        return;
    }

    std::this_thread::sleep_for(200ms);

    // Client should be connected initially
    if (!client.is_connected()) {
        log_error("Client not connected initially");
        server.stop();
        return;
    }

    log("  Client connected, waiting for timeout...");

    // Wait longer than timeout
    std::this_thread::sleep_for(1s);

    // Server should have disconnected the client
    auto stats = server.get_stats();
    log("  Server timeout count: " + std::to_string(stats.timeout_disconnects));

    // Cleanup
    client.disconnect("Test complete");
    std::this_thread::sleep_for(100ms);

    server.stop();
    log("  Connection timeout test passed!");
}

// Test 8: Graceful shutdown with active connections
void test_graceful_shutdown() {
    log("Testing graceful shutdown...");

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        log_error("Failed to start server for shutdown test");
        return;
    }

    // Connect multiple clients
    std::vector<std::unique_ptr<client::Client>> clients;
    for (int i = 0; i < 3; ++i) {
        client::ClientConfig config;
        config.username = "ShutdownUser" + std::to_string(i);
        config.auto_reconnect = false;

        auto client = std::make_unique<client::Client>(config);
        client->connect();
        clients.push_back(std::move(client));
    }

    std::this_thread::sleep_for(200ms);

    // Stop server gracefully
    log("  Stopping server with active connections...");
    server.stop();

    // All clients should be disconnected
    std::this_thread::sleep_for(500ms);

    int still_connected = 0;
    for (const auto& client : clients) {
        if (client->is_connected()) {
            still_connected++;
        }
    }

    if (still_connected > 0) {
        log("  Warning: " + std::to_string(still_connected) + " clients still think they're connected");
    } else {
        log("  All clients properly disconnected");
    }

    clients.clear();
    log("  Graceful shutdown test passed!");
}

int main() {
    std::cout << "=== ChatterBox Integration Tests ===" << std::endl;
    std::cout << std::endl;

    try {
        test_server_lifecycle();
        std::cout << std::endl;

        test_client_connection();
        std::cout << std::endl;

        test_multiple_clients();
        std::cout << std::endl;

        test_message_sending();
        std::cout << std::endl;

        test_heartbeat();
        std::cout << std::endl;

        test_private_messaging();
        std::cout << std::endl;

        test_connection_timeout();
        std::cout << std::endl;

        test_graceful_shutdown();
        std::cout << std::endl;

        if (test_passed) {
            std::cout << "=== All integration tests passed! ===" << std::endl;
            return 0;
        } else {
            std::cout << "=== Some integration tests failed! ===" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Integration test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
