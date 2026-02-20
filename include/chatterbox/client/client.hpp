#ifndef CHATTERBOX_CLIENT_CLIENT_HPP
#define CHATTERBOX_CLIENT_CLIENT_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/ipc/message_queue.hpp>
#include <chatterbox/ipc/shared_memory.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <chatterbox/client/terminal_ui.hpp>
#include <chatterbox/client/input_handler.hpp>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <queue>
#include <mutex>

namespace chatterbox {
namespace client {

// Client configuration
struct ClientConfig {
    key_t server_queue_key = SERVER_QUEUE_KEY;
    std::string username;
    int64_t heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS;
    int64_t connection_timeout_ms = CONNECTION_TIMEOUT_MS;
    bool auto_reconnect = true;
    int max_reconnect_attempts = 5;
    bool enable_ui = true;
};

// Client state
enum class ClientState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

// Callback types
using MessageReceivedCallback = std::function<void(const protocol::Message&)>;
using ConnectionStateCallback = std::function<void(ClientState, const std::string&)>;
using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;

// Main client class
class Client {
public:
    explicit Client(const ClientConfig& config = ClientConfig());
    ~Client();

    // Non-copyable, non-movable
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    // Connection lifecycle
    ErrorCode connect(const std::string& username = "");
    ErrorCode disconnect(const std::string& reason = "");
    ErrorCode reconnect();
    bool is_connected() const { return state_.load() == ClientState::CONNECTED; }
    ClientState state() const { return state_.load(); }

    // Configuration
    const ClientConfig& config() const { return config_; }
    void set_username(const std::string& username) { config_.username = username; }

    // User information
    UserId user_id() const { return user_id_.load(); }
    const std::string& username() const { return config_.username; }

    // Messaging
    ErrorCode send_message(const std::string& content);
    ErrorCode send_private_message(UserId recipient, const std::string& content);
    ErrorCode send_private_message(const std::string& username, const std::string& content);

    // Room management
    ErrorCode join_room(RoomId room_id);
    ErrorCode leave_room();
    ErrorCode create_room(const std::string& name);
    RoomId current_room() const { return current_room_.load(); }

    // User queries
    ErrorCode request_user_list();
    std::vector<std::pair<UserId, std::string>> get_cached_user_list() const;

    // Callbacks
    void on_message_received(MessageReceivedCallback callback) {
        message_callback_ = std::move(callback);
    }
    void on_state_changed(ConnectionStateCallback callback) {
        state_callback_ = std::move(callback);
    }
    void on_error(ErrorCallback callback) {
        error_callback_ = std::move(callback);
    }

    // UI control
    void run_ui();
    void stop_ui();
    TerminalUI* ui() { return ui_.get(); }

    // Wait for client to finish
    void wait();

    // Statistics
    struct Stats {
        uint64_t messages_sent;
        uint64_t messages_received;
        uint64_t bytes_sent;
        uint64_t bytes_received;
        double avg_latency_ms;
        int64_t connected_duration_ms;
        int reconnect_count;
    };
    Stats get_stats() const;

private:
    ClientConfig config_;
    std::atomic<ClientState> state_{ClientState::DISCONNECTED};
    std::atomic<UserId> user_id_{INVALID_USER_ID};
    std::atomic<RoomId> current_room_{LOBBY_ROOM_ID};

    // IPC resources
    std::unique_ptr<ipc::MessageQueue> server_queue_;
    std::unique_ptr<ipc::MessageQueue> client_queue_;
    key_t client_queue_key_;

    // Components
    std::unique_ptr<TerminalUI> ui_;
    std::unique_ptr<InputHandler> input_handler_;

    // Worker threads
    std::thread receiver_thread_;
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};

    // Message queue for pending messages
    mutable std::mutex pending_mutex_;
    std::queue<protocol::Message> pending_messages_;

    // User list cache
    mutable std::mutex user_list_mutex_;
    std::vector<std::pair<UserId, std::string>> user_list_cache_;
    protocol::Timestamp user_list_timestamp_;

    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_{};
    protocol::Timestamp connect_time_;

    // Sequence tracking
    std::atomic<SequenceNum> next_sequence_{0};
    protocol::TimestampGenerator timestamp_generator_;

    // Callbacks
    MessageReceivedCallback message_callback_;
    ConnectionStateCallback state_callback_;
    ErrorCallback error_callback_;

    // Internal methods
    void receiver_loop();
    void heartbeat_loop();
    void process_message(const protocol::Message& msg);
    void handle_connect_response(const protocol::Message& msg);
    void handle_disconnect(const protocol::Message& msg);
    void handle_heartbeat_ack(const protocol::Message& msg);
    void handle_chat_message(const protocol::Message& msg);
    void handle_private_message(const protocol::Message& msg);
    void handle_user_list(const protocol::Message& msg);
    void handle_system_message(const protocol::Message& msg);
    void handle_error(const protocol::Message& msg);

    ErrorCode send_raw(const protocol::Message& msg);
    void set_state(ClientState new_state, const std::string& reason = "");
    void emit_error(ErrorCode code, const std::string& message);
    void update_stats(const protocol::Message& msg, bool sent);

    // IPC setup
    ErrorCode initialize_ipc();
    void cleanup_ipc();
    key_t generate_client_queue_key();
};

// Client builder for fluent configuration
class ClientBuilder {
public:
    ClientBuilder() = default;

    ClientBuilder& with_username(const std::string& username) {
        config_.username = username;
        return *this;
    }

    ClientBuilder& with_server_key(key_t key) {
        config_.server_queue_key = key;
        return *this;
    }

    ClientBuilder& with_heartbeat_interval(int64_t ms) {
        config_.heartbeat_interval_ms = ms;
        return *this;
    }

    ClientBuilder& with_auto_reconnect(bool enabled, int max_attempts = 5) {
        config_.auto_reconnect = enabled;
        config_.max_reconnect_attempts = max_attempts;
        return *this;
    }

    ClientBuilder& with_ui(bool enabled) {
        config_.enable_ui = enabled;
        return *this;
    }

    std::unique_ptr<Client> build() {
        return std::make_unique<Client>(config_);
    }

private:
    ClientConfig config_;
};

} // namespace client
} // namespace chatterbox

#endif // CHATTERBOX_CLIENT_CLIENT_HPP
