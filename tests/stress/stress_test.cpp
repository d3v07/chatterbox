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
#include <random>
#include <iomanip>

using namespace chatterbox;
using namespace std::chrono_literals;

// Stress test configuration
struct StressConfig {
    int num_clients = 50;
    int messages_per_client = 100;
    int test_duration_seconds = 30;
    int message_size = 256;
    bool enable_private_messages = true;
};

// Statistics
struct StressStats {
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_failed{0};
    std::atomic<uint64_t> connections_successful{0};
    std::atomic<uint64_t> connections_failed{0};
    std::atomic<uint64_t> disconnections{0};
    std::atomic<uint64_t> reconnections{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;

    void print() const {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count();

        std::cout << "\n=== Stress Test Results ===" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Duration: " << duration / 1000.0 << " seconds" << std::endl;
        std::cout << std::endl;

        std::cout << "Connections:" << std::endl;
        std::cout << "  Successful: " << connections_successful.load() << std::endl;
        std::cout << "  Failed: " << connections_failed.load() << std::endl;
        std::cout << "  Disconnections: " << disconnections.load() << std::endl;
        std::cout << "  Reconnections: " << reconnections.load() << std::endl;
        std::cout << std::endl;

        std::cout << "Messages:" << std::endl;
        std::cout << "  Sent: " << messages_sent.load() << std::endl;
        std::cout << "  Received: " << messages_received.load() << std::endl;
        std::cout << "  Failed: " << messages_failed.load() << std::endl;
        std::cout << std::endl;

        double msg_per_sec = messages_sent.load() / (duration / 1000.0);
        double kb_per_sec = (bytes_sent.load() / 1024.0) / (duration / 1000.0);
        std::cout << "Throughput:" << std::endl;
        std::cout << "  Messages/sec: " << msg_per_sec << std::endl;
        std::cout << "  KB/sec sent: " << kb_per_sec << std::endl;
        std::cout << "  Total bytes sent: " << bytes_sent.load() << std::endl;
        std::cout << "  Total bytes received: " << bytes_received.load() << std::endl;

        // Calculate delivery rate
        if (messages_sent > 0) {
            double delivery_rate = (double)messages_received.load() / messages_sent.load() * 100.0;
            std::cout << "  Delivery rate: " << delivery_rate << "%" << std::endl;
        }
    }
};

class StressTestClient {
public:
    StressTestClient(int id, const StressConfig& config, StressStats& stats,
                     const std::vector<UserId>& all_user_ids)
        : id_(id)
        , config_(config)
        , stats_(stats)
        , all_user_ids_(all_user_ids)
        , rng_(std::random_device{}())
    {
        client_config_.username = "StressUser" + std::to_string(id);
        client_config_.auto_reconnect = true;
        client_config_.heartbeat_interval_ms = 2000;
    }

    void start() {
        running_ = true;
        thread_ = std::thread(&StressTestClient::run, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    UserId user_id() const { return client_ ? client_->user_id() : 0; }

private:
    void run() {
        client_ = std::make_unique<client::Client>(client_config_);

        // Set up message callback
        client_->set_message_callback([this](const protocol::Message& msg) {
            stats_.messages_received++;
            stats_.bytes_received += msg.total_size();
        });

        // Connect
        auto err = client_->connect();
        if (err != ErrorCode::SUCCESS) {
            stats_.connections_failed++;
            return;
        }
        stats_.connections_successful++;

        // Wait for connection to stabilize
        std::this_thread::sleep_for(100ms);

        // Generate random messages
        std::uniform_int_distribution<int> msg_len_dist(10, config_.message_size);
        std::uniform_int_distribution<int> action_dist(0, 100);
        std::uniform_int_distribution<size_t> user_dist(0, all_user_ids_.size() - 1);

        while (running_) {
            if (!client_->is_connected()) {
                stats_.disconnections++;

                // Try to reconnect
                std::this_thread::sleep_for(500ms);
                err = client_->connect();
                if (err == ErrorCode::SUCCESS) {
                    stats_.reconnections++;
                }
                continue;
            }

            // Generate and send a message
            int action = action_dist(rng_);

            if (action < 70) {
                // Regular broadcast message (70% of the time)
                std::string content = generate_random_message(msg_len_dist(rng_));
                err = client_->send_message(content);

                if (err == ErrorCode::SUCCESS) {
                    stats_.messages_sent++;
                    stats_.bytes_sent += content.size() + sizeof(protocol::MessageHeader);
                } else {
                    stats_.messages_failed++;
                }
            } else if (action < 90 && config_.enable_private_messages && !all_user_ids_.empty()) {
                // Private message (20% of the time)
                size_t target_idx = user_dist(rng_);
                UserId target = all_user_ids_[target_idx];

                if (target != client_->user_id()) {
                    std::string content = generate_random_message(msg_len_dist(rng_));
                    err = client_->send_private_message(target, content);

                    if (err == ErrorCode::SUCCESS) {
                        stats_.messages_sent++;
                        stats_.bytes_sent += content.size() + sizeof(protocol::MessageHeader);
                    } else {
                        stats_.messages_failed++;
                    }
                }
            } else {
                // Request user list (10% of the time)
                client_->request_user_list();
            }

            // Random delay between messages
            std::uniform_int_distribution<int> delay_dist(10, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(rng_)));
        }

        client_->disconnect("Stress test complete");
    }

    std::string generate_random_message(int length) {
        static const char charset[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
        std::uniform_int_distribution<size_t> char_dist(0, sizeof(charset) - 2);

        std::string result;
        result.reserve(length);
        for (int i = 0; i < length; ++i) {
            result += charset[char_dist(rng_)];
        }
        return result;
    }

    int id_;
    StressConfig config_;
    StressStats& stats_;
    const std::vector<UserId>& all_user_ids_;

    client::ClientConfig client_config_;
    std::unique_ptr<client::Client> client_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mt19937 rng_;
};

// Test 1: Connection stress test
void test_connection_stress(const StressConfig& config) {
    std::cout << "\n=== Connection Stress Test ===" << std::endl;
    std::cout << "Testing " << config.num_clients << " rapid connections..." << std::endl;

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = config.num_clients + 10;
    server_config.heartbeat_interval_ms = 2000;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to start server: " << error_to_string(err) << std::endl;
        return;
    }

    StressStats stats;
    stats.start_time = std::chrono::steady_clock::now();

    // Rapidly connect and disconnect clients
    std::vector<std::unique_ptr<client::Client>> clients;

    for (int i = 0; i < config.num_clients; ++i) {
        client::ClientConfig client_config;
        client_config.username = "ConnStress" + std::to_string(i);
        client_config.auto_reconnect = false;

        auto client = std::make_unique<client::Client>(client_config);
        err = client->connect();

        if (err == ErrorCode::SUCCESS) {
            stats.connections_successful++;
            clients.push_back(std::move(client));
        } else {
            stats.connections_failed++;
        }

        // Small delay between connections
        std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(500ms);

    auto server_stats = server.get_stats();
    std::cout << "Server sees " << server_stats.connected_users << " users" << std::endl;

    // Disconnect all
    for (auto& client : clients) {
        client->disconnect("Test complete");
    }
    clients.clear();

    stats.end_time = std::chrono::steady_clock::now();
    stats.print();

    server.stop();
}

// Test 2: Message throughput stress test
void test_message_stress(const StressConfig& config) {
    std::cout << "\n=== Message Throughput Stress Test ===" << std::endl;
    std::cout << "Testing " << config.num_clients << " clients sending messages for "
              << config.test_duration_seconds << " seconds..." << std::endl;

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = config.num_clients + 10;
    server_config.heartbeat_interval_ms = 2000;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to start server: " << error_to_string(err) << std::endl;
        return;
    }

    StressStats stats;
    std::vector<UserId> all_user_ids;
    std::vector<std::unique_ptr<StressTestClient>> clients;

    // Create and start all clients
    for (int i = 0; i < config.num_clients; ++i) {
        auto client = std::make_unique<StressTestClient>(i, config, stats, all_user_ids);
        clients.push_back(std::move(client));
    }

    // Start all clients
    for (auto& client : clients) {
        client->start();
    }

    // Wait for all to connect
    std::this_thread::sleep_for(1s);

    // Collect user IDs for private messaging
    for (const auto& client : clients) {
        if (client->user_id() != 0) {
            all_user_ids.push_back(client->user_id());
        }
    }

    stats.start_time = std::chrono::steady_clock::now();

    std::cout << "Running stress test..." << std::endl;

    // Run for specified duration
    for (int i = 0; i < config.test_duration_seconds; ++i) {
        std::this_thread::sleep_for(1s);
        std::cout << "  " << (i + 1) << "/" << config.test_duration_seconds
                  << " seconds - Messages: " << stats.messages_sent.load()
                  << " sent, " << stats.messages_received.load() << " received" << std::endl;
    }

    stats.end_time = std::chrono::steady_clock::now();

    // Stop all clients
    for (auto& client : clients) {
        client->stop();
    }
    clients.clear();

    // Print results
    stats.print();

    // Print server stats
    auto server_stats = server.get_stats();
    std::cout << "\nServer Statistics:" << std::endl;
    std::cout << "  Peak users: " << server_stats.peak_users << std::endl;
    std::cout << "  Total messages routed: " << server_stats.messages_routed << std::endl;

    server.stop();
}

// Test 3: Latency measurement
void test_latency(const StressConfig& config) {
    std::cout << "\n=== Latency Measurement Test ===" << std::endl;

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 10;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to start server: " << error_to_string(err) << std::endl;
        return;
    }

    // Connect a single client for accurate latency measurement
    client::ClientConfig client_config;
    client_config.username = "LatencyTest";
    client_config.auto_reconnect = false;
    client::Client client(client_config);

    err = client.connect();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to connect client: " << error_to_string(err) << std::endl;
        server.stop();
        return;
    }

    std::this_thread::sleep_for(200ms);

    // Measure round-trip latency
    std::vector<double> latencies;
    std::atomic<bool> message_received{false};
    std::chrono::steady_clock::time_point send_time;

    client.set_message_callback([&](const protocol::Message& msg) {
        if (msg.type() == protocol::MessageType::CHAT_MESSAGE ||
            msg.type() == protocol::MessageType::CHAT_BROADCAST) {
            auto now = std::chrono::steady_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                now - send_time
            ).count();
            latencies.push_back(latency / 1000.0); // Convert to ms
            message_received = true;
        }
    });

    const int NUM_SAMPLES = 100;
    std::cout << "Measuring latency over " << NUM_SAMPLES << " messages..." << std::endl;

    for (int i = 0; i < NUM_SAMPLES; ++i) {
        message_received = false;
        send_time = std::chrono::steady_clock::now();

        std::string content = "Latency test message " + std::to_string(i);
        err = client.send_message(content);

        if (err != ErrorCode::SUCCESS) {
            continue;
        }

        // Wait for echo
        for (int j = 0; j < 100 && !message_received; ++j) {
            std::this_thread::sleep_for(10ms);
        }

        if (!message_received) {
            std::cout << "Message " << i << " not received" << std::endl;
        }
    }

    // Calculate statistics
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());

        double sum = 0;
        for (double l : latencies) sum += l;
        double avg = sum / latencies.size();

        double min = latencies.front();
        double max = latencies.back();
        double median = latencies[latencies.size() / 2];
        double p95 = latencies[latencies.size() * 95 / 100];
        double p99 = latencies[latencies.size() * 99 / 100];

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\nLatency Results (ms):" << std::endl;
        std::cout << "  Min: " << min << std::endl;
        std::cout << "  Max: " << max << std::endl;
        std::cout << "  Average: " << avg << std::endl;
        std::cout << "  Median: " << median << std::endl;
        std::cout << "  P95: " << p95 << std::endl;
        std::cout << "  P99: " << p99 << std::endl;

        // Check if within target
        if (avg <= TARGET_LATENCY_MS) {
            std::cout << "\n  [PASS] Average latency within target (" << TARGET_LATENCY_MS << " ms)" << std::endl;
        } else {
            std::cout << "\n  [WARN] Average latency exceeds target (" << TARGET_LATENCY_MS << " ms)" << std::endl;
        }
    } else {
        std::cerr << "No latency samples collected!" << std::endl;
    }

    client.disconnect("Test complete");
    server.stop();
}

// Test 4: Memory stability test
void test_memory_stability(const StressConfig& config) {
    std::cout << "\n=== Memory Stability Test ===" << std::endl;
    std::cout << "Running rapid connect/disconnect cycles..." << std::endl;

    // Start server
    server::ServerConfig server_config;
    server_config.max_users = 100;
    server::Server server(server_config);

    auto err = server.start();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to start server: " << error_to_string(err) << std::endl;
        return;
    }

    const int CYCLES = 10;
    const int CLIENTS_PER_CYCLE = 20;

    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        std::cout << "  Cycle " << (cycle + 1) << "/" << CYCLES << std::endl;

        std::vector<std::unique_ptr<client::Client>> clients;

        // Connect clients
        for (int i = 0; i < CLIENTS_PER_CYCLE; ++i) {
            client::ClientConfig client_config;
            client_config.username = "MemTest_" + std::to_string(cycle) + "_" + std::to_string(i);
            client_config.auto_reconnect = false;

            auto client = std::make_unique<client::Client>(client_config);
            client->connect();
            clients.push_back(std::move(client));
        }

        std::this_thread::sleep_for(200ms);

        // Send some messages
        for (auto& client : clients) {
            if (client->is_connected()) {
                client->send_message("Memory test message");
            }
        }

        std::this_thread::sleep_for(100ms);

        // Disconnect all
        for (auto& client : clients) {
            client->disconnect("Cycle complete");
        }
        clients.clear();

        // Give time for cleanup
        std::this_thread::sleep_for(100ms);
    }

    std::cout << "Memory stability test completed" << std::endl;

    auto server_stats = server.get_stats();
    std::cout << "Server stats:" << std::endl;
    std::cout << "  Total connections: " << server_stats.total_connections << std::endl;
    std::cout << "  Current users: " << server_stats.connected_users << std::endl;

    server.stop();
}

int main(int argc, char* argv[]) {
    std::cout << "=== ChatterBox Stress Tests ===" << std::endl;
    std::cout << "Testing system with 50+ concurrent users" << std::endl;

    StressConfig config;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--clients" && i + 1 < argc) {
            config.num_clients = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            config.test_duration_seconds = std::stoi(argv[++i]);
        } else if (arg == "--msg-size" && i + 1 < argc) {
            config.message_size = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --clients N     Number of clients (default: 50)" << std::endl;
            std::cout << "  --duration N    Test duration in seconds (default: 30)" << std::endl;
            std::cout << "  --msg-size N    Message size in bytes (default: 256)" << std::endl;
            return 0;
        }
    }

    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Clients: " << config.num_clients << std::endl;
    std::cout << "  Duration: " << config.test_duration_seconds << " seconds" << std::endl;
    std::cout << "  Message size: " << config.message_size << " bytes" << std::endl;

    try {
        // Run all stress tests
        test_connection_stress(config);
        test_message_stress(config);
        test_latency(config);
        test_memory_stability(config);

        std::cout << "\n=== All stress tests completed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Stress test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
