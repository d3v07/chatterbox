#include <chatterbox/server/server.hpp>
#include <iostream>
#include <csignal>
#include <cstring>

using namespace chatterbox;
using namespace chatterbox::server;

// Global server pointer for signal handling
static std::unique_ptr<Server> g_server;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  -p, --port <port>     Server port (IPC key offset)\n"
              << "  -u, --max-users <n>   Maximum number of users (default: 64)\n"
              << "  -t, --threads <n>     Thread pool size (default: 8)\n"
              << "  -l, --log <file>      Log file path\n"
              << "  -q, --quiet           Disable console logging\n"
              << "  -h, --help            Show this help\n"
              << "\nExample:\n"
              << "  " << program << " -u 100 -t 16\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ServerConfig config;
    config.enable_logging = true;
    config.log_file = "";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                int offset = std::stoi(argv[++i]);
                config.ipc_base_key = BASE_IPC_KEY + offset;
            }
        } else if (arg == "-u" || arg == "--max-users") {
            if (i + 1 < argc) {
                config.max_users = static_cast<size_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                config.thread_pool_size = static_cast<size_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) {
                config.log_file = argv[++i];
            }
        } else if (arg == "-q" || arg == "--quiet") {
            config.enable_logging = false;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================\n"
              << "       ChatterBox Server v1.0.0\n"
              << "========================================\n"
              << std::endl;

    std::cout << "Configuration:\n"
              << "  IPC Base Key: 0x" << std::hex << config.ipc_base_key << std::dec << "\n"
              << "  Max Users: " << config.max_users << "\n"
              << "  Thread Pool Size: " << config.thread_pool_size << "\n"
              << "  Heartbeat Interval: " << config.heartbeat_interval_ms << "ms\n"
              << "  Connection Timeout: " << config.connection_timeout_ms << "ms\n"
              << "  Target Latency: " << config.target_latency_ms << "ms\n"
              << std::endl;

    // Create and start server
    g_server = std::make_unique<Server>(config);

    // Set up callbacks
    g_server->on_connection([](UserId user_id, const std::string& username) {
        std::cout << "[+] User connected: " << username << " (ID: " << user_id << ")" << std::endl;
    });

    g_server->on_disconnection([](UserId user_id, const std::string& reason) {
        std::cout << "[-] User disconnected: ID " << user_id << " (" << reason << ")" << std::endl;
    });

    g_server->on_message([](const protocol::Message& msg, UserId sender) {
        if (msg.type() == protocol::MessageType::CHAT_MESSAGE) {
            std::cout << "[MSG] From " << sender << ": "
                      << protocol::PayloadSerializer::deserialize_chat_message(msg.payload())
                      << std::endl;
        }
    });

    auto err = g_server->start();
    if (err != ErrorCode::SUCCESS) {
        std::cerr << "Failed to start server: " << error_to_string(err) << std::endl;
        return 1;
    }

    std::cout << "Server started. Press Ctrl+C to stop.\n" << std::endl;

    // Wait for server to stop
    g_server->wait();

    std::cout << "\nServer stopped." << std::endl;

    // Print final statistics
    auto stats = g_server->get_stats();
    std::cout << "\nFinal Statistics:\n"
              << "  Total Connections: " << stats.connections_total << "\n"
              << "  Messages Received: " << stats.messages_received << "\n"
              << "  Messages Sent: " << stats.messages_sent << "\n"
              << "  Messages Dropped: " << stats.messages_dropped << "\n"
              << "  Bytes Received: " << stats.bytes_received << "\n"
              << "  Bytes Sent: " << stats.bytes_sent << "\n"
              << "  Uptime: " << stats.uptime_seconds << " seconds\n"
              << std::endl;

    g_server.reset();
    return 0;
}
