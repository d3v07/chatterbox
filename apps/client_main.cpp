#include <chatterbox/client/client.hpp>
#include <iostream>
#include <csignal>

using namespace chatterbox;
using namespace chatterbox::client;

// Global client pointer for signal handling
static std::unique_ptr<Client> g_client;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", disconnecting..." << std::endl;
    if (g_client) {
        g_client->disconnect("Interrupted");
        g_client->stop_ui();
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] [username]\n"
              << "\nOptions:\n"
              << "  -s, --server <key>    Server IPC key offset\n"
              << "  -n, --no-ui           Disable terminal UI\n"
              << "  -r, --no-reconnect    Disable auto-reconnect\n"
              << "  -h, --help            Show this help\n"
              << "\nExample:\n"
              << "  " << program << " -s 0 MyUsername\n"
              << "  " << program << " --no-ui TestUser\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ClientConfig config;
    config.enable_ui = true;
    config.auto_reconnect = true;
    std::string username;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                int offset = std::stoi(argv[++i]);
                config.server_queue_key = SERVER_QUEUE_KEY + offset;
            }
        } else if (arg == "-n" || arg == "--no-ui") {
            config.enable_ui = false;
        } else if (arg == "-r" || arg == "--no-reconnect") {
            config.auto_reconnect = false;
        } else if (arg[0] != '-') {
            username = arg;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!config.enable_ui) {
        std::cout << "========================================\n"
                  << "       ChatterBox Client v1.0.0\n"
                  << "========================================\n"
                  << std::endl;
    }

    // Prompt for username if not provided
    if (username.empty()) {
        if (config.enable_ui) {
            // Will be prompted by UI
        } else {
            std::cout << "Enter username: ";
            std::getline(std::cin, username);
            if (username.empty()) {
                std::cerr << "Username is required." << std::endl;
                return 1;
            }
        }
    }

    config.username = username;

    // Create client
    g_client = std::make_unique<Client>(config);

    // Set up callbacks for non-UI mode
    if (!config.enable_ui) {
        SimpleUI simple_ui;

        g_client->on_message_received([&simple_ui](const protocol::Message& msg) {
            switch (msg.type()) {
                case protocol::MessageType::CHAT_BROADCAST: {
                    protocol::BinaryReader reader(msg.payload());
                    UserId sender_id = reader.read_u32();
                    std::string sender_name = reader.read_string();
                    std::string content = reader.read_string();
                    (void)sender_id;
                    simple_ui.display_message(sender_name, content, false);
                    break;
                }
                case protocol::MessageType::PRIVATE_MESSAGE: {
                    protocol::BinaryReader reader(msg.payload());
                    UserId sender_id = reader.read_u32();
                    std::string sender_name = reader.read_string();
                    std::string content = reader.read_string();
                    (void)sender_id;
                    simple_ui.display_message(sender_name, content, true);
                    break;
                }
                case protocol::MessageType::SYSTEM_MESSAGE:
                    simple_ui.display_system_message(msg.payload_as_string());
                    break;
                case protocol::MessageType::USER_LIST: {
                    auto users = protocol::PayloadSerializer::deserialize_user_list(msg.payload());
                    simple_ui.display_user_list(users);
                    break;
                }
                case protocol::MessageType::ERROR_MSG: {
                    auto err = protocol::PayloadSerializer::deserialize_error(msg.payload());
                    simple_ui.display_error(err.message);
                    break;
                }
                default:
                    break;
            }
        });

        g_client->on_state_changed([&simple_ui](ClientState state, const std::string& reason) {
            switch (state) {
                case ClientState::CONNECTED:
                    simple_ui.display_system_message("Connected: " + reason);
                    break;
                case ClientState::DISCONNECTED:
                    simple_ui.display_system_message("Disconnected: " + reason);
                    break;
                case ClientState::RECONNECTING:
                    simple_ui.display_system_message("Reconnecting: " + reason);
                    break;
                case ClientState::ERROR:
                    simple_ui.display_error("Error: " + reason);
                    break;
                default:
                    break;
            }
        });

        g_client->on_error([&simple_ui](ErrorCode code, const std::string& message) {
            simple_ui.display_error(std::string(error_to_string(code)) + ": " + message);
        });
    }

    // Connect to server
    if (!username.empty()) {
        auto err = g_client->connect(username);
        if (err != ErrorCode::SUCCESS && !config.enable_ui) {
            std::cerr << "Failed to connect: " << error_to_string(err) << std::endl;
            if (!config.auto_reconnect) {
                return 1;
            }
        }
    }

    // Run UI or command loop
    if (config.enable_ui) {
        g_client->run_ui();
    } else {
        // Simple command loop for non-UI mode
        std::cout << "Connected. Type messages and press Enter to send.\n"
                  << "Commands: /quit, /users, /msg <user> <message>\n"
                  << std::endl;

        std::string input;
        while (std::getline(std::cin, input)) {
            if (input == "/quit" || input == "/exit") {
                break;
            } else if (input == "/users") {
                g_client->request_user_list();
            } else if (input.substr(0, 5) == "/msg ") {
                size_t space = input.find(' ', 5);
                if (space != std::string::npos) {
                    std::string target = input.substr(5, space - 5);
                    std::string message = input.substr(space + 1);
                    g_client->send_private_message(target, message);
                }
            } else if (!input.empty() && input[0] != '/') {
                g_client->send_message(input);
            }
        }
    }

    // Disconnect and cleanup
    g_client->disconnect("User exit");

    if (!config.enable_ui) {
        // Print final statistics
        auto stats = g_client->get_stats();
        std::cout << "\nSession Statistics:\n"
                  << "  Messages Sent: " << stats.messages_sent << "\n"
                  << "  Messages Received: " << stats.messages_received << "\n"
                  << "  Average Latency: " << stats.avg_latency_ms << "ms\n"
                  << "  Reconnect Count: " << stats.reconnect_count << "\n"
                  << std::endl;
    }

    g_client.reset();
    return 0;
}
