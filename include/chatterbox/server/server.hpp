#ifndef CHATTERBOX_SERVER_SERVER_HPP
#define CHATTERBOX_SERVER_SERVER_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/ipc/message_queue.hpp>
#include <chatterbox/ipc/shared_memory.hpp>
#include <chatterbox/ipc/semaphore.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/sync/thread_pool.hpp>
#include <chatterbox/sync/condition_var.hpp>
#include <chatterbox/server/connection_manager.hpp>
#include <chatterbox/server/message_router.hpp>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

namespace chatterbox {
namespace server {

// Server configuration
struct ServerConfig {
    key_t ipc_base_key = BASE_IPC_KEY;
    size_t max_users = MAX_USERS;
    size_t thread_pool_size = 8;
    int64_t heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS;
    int64_t connection_timeout_ms = CONNECTION_TIMEOUT_MS;
    double target_latency_ms = TARGET_LATENCY_MS;
    bool enable_logging = true;
    std::string log_file = "chatterbox_server.log";
};

// Server statistics
struct ServerStats {
    // Message statistics
    uint64_t messages_received = 0;
    uint64_t messages_sent = 0;
    uint64_t messages_dropped = 0;
    uint64_t messages_routed = 0;

    // Byte statistics
    uint64_t bytes_received = 0;
    uint64_t bytes_sent = 0;

    // Connection statistics
    uint64_t total_connections = 0;
    uint64_t connected_users = 0;
    uint64_t peak_users = 0;
    uint64_t timeout_disconnects = 0;

    // Latency statistics
    double avg_latency_ms = 0.0;
    double max_latency_ms = 0.0;

    // Uptime
    uint64_t uptime_seconds = 0;

    // Deprecated aliases for backward compatibility
    uint64_t connections_total() const { return total_connections; }
    uint64_t connections_active() const { return connected_users; }
};

// Callback types
using MessageCallback = std::function<void(const protocol::Message&, UserId)>;
using ConnectionCallback = std::function<void(UserId, const std::string&)>;
using DisconnectionCallback = std::function<void(UserId, const std::string&)>;

// Main server class
class Server {
public:
    explicit Server(const ServerConfig& config = ServerConfig());
    ~Server();

    // Non-copyable, non-movable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    // Server lifecycle
    ErrorCode start();
    void stop();
    bool is_running() const { return running_.load(); }

    // Wait for server to finish
    void wait();

    // Configuration
    const ServerConfig& config() const { return config_; }

    // Statistics
    ServerStats get_stats() const;

    // Callbacks
    void on_message(MessageCallback callback) { message_callback_ = std::move(callback); }
    void on_connection(ConnectionCallback callback) { connection_callback_ = std::move(callback); }
    void on_disconnection(DisconnectionCallback callback) { disconnection_callback_ = std::move(callback); }

    // Manual message sending
    ErrorCode send_to_user(UserId user_id, const protocol::Message& msg);
    ErrorCode broadcast(const protocol::Message& msg, UserId exclude = INVALID_USER_ID);
    ErrorCode send_system_message(const std::string& content);

    // User management
    std::vector<std::pair<UserId, std::string>> get_connected_users() const;
    bool kick_user(UserId user_id, const std::string& reason = "Kicked by server");

    // Connection manager access
    ConnectionManager* connection_manager() { return connection_manager_.get(); }
    const ConnectionManager* connection_manager() const { return connection_manager_.get(); }

    // Message router access
    MessageRouter* message_router() { return message_router_.get(); }
    const MessageRouter* message_router() const { return message_router_.get(); }

private:
    ServerConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};

    // IPC resources
    std::unique_ptr<ipc::MessageQueue> server_queue_;
    std::unique_ptr<ipc::ChatSharedMemory> shared_memory_;
    std::unique_ptr<ipc::RWLock> rw_lock_;

    // Components
    std::unique_ptr<sync::ThreadPool> thread_pool_;
    std::unique_ptr<ConnectionManager> connection_manager_;
    std::unique_ptr<MessageRouter> message_router_;

    // Worker threads
    std::thread receiver_thread_;
    std::thread heartbeat_thread_;
    std::thread cleanup_thread_;

    // Statistics
    mutable std::mutex stats_mutex_;
    ServerStats stats_;
    protocol::TimestampGenerator timestamp_generator_;
    protocol::Timestamp start_time_;

    // Callbacks
    MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
    DisconnectionCallback disconnection_callback_;

    // Internal methods
    void receiver_loop();
    void heartbeat_loop();
    void cleanup_loop();
    void process_message(const protocol::Message& msg);
    void handle_connect_request(const protocol::Message& msg);
    void handle_disconnect(const protocol::Message& msg);
    void handle_heartbeat(const protocol::Message& msg);
    void handle_chat_message(const protocol::Message& msg);
    void handle_private_message(const protocol::Message& msg);
    void handle_room_command(const protocol::Message& msg);
    void handle_user_list_request(const protocol::Message& msg);

    void update_stats(const protocol::Message& msg, bool received);
    void log(const std::string& message);

    // Initialize IPC resources
    ErrorCode initialize_ipc();
    void cleanup_ipc();
};

// Server builder for fluent configuration
class ServerBuilder {
public:
    ServerBuilder() = default;

    ServerBuilder& with_ipc_key(key_t key) {
        config_.ipc_base_key = key;
        return *this;
    }

    ServerBuilder& with_max_users(size_t max) {
        config_.max_users = max;
        return *this;
    }

    ServerBuilder& with_thread_pool_size(size_t size) {
        config_.thread_pool_size = size;
        return *this;
    }

    ServerBuilder& with_heartbeat_interval(int64_t ms) {
        config_.heartbeat_interval_ms = ms;
        return *this;
    }

    ServerBuilder& with_connection_timeout(int64_t ms) {
        config_.connection_timeout_ms = ms;
        return *this;
    }

    ServerBuilder& with_target_latency(double ms) {
        config_.target_latency_ms = ms;
        return *this;
    }

    ServerBuilder& with_logging(bool enabled, const std::string& file = "") {
        config_.enable_logging = enabled;
        if (!file.empty()) {
            config_.log_file = file;
        }
        return *this;
    }

    std::unique_ptr<Server> build() {
        return std::make_unique<Server>(config_);
    }

private:
    ServerConfig config_;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_SERVER_HPP
