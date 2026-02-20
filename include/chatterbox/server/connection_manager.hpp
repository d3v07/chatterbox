#ifndef CHATTERBOX_SERVER_CONNECTION_MANAGER_HPP
#define CHATTERBOX_SERVER_CONNECTION_MANAGER_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/ipc/message_queue.hpp>
#include <chatterbox/ipc/shared_memory.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>

namespace chatterbox {
namespace server {

// Connection state
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

// Information about a connected client
struct ClientConnection {
    UserId user_id;
    std::string username;
    key_t client_queue_key;
    std::unique_ptr<ipc::MessageQueue> client_queue;
    ConnectionState state;
    protocol::Timestamp connected_at;
    protocol::Timestamp last_activity;
    protocol::Timestamp last_heartbeat;
    RoomId current_room;
    uint64_t messages_sent;
    uint64_t messages_received;
    double avg_latency_ms;
    int missed_heartbeats;

    ClientConnection()
        : user_id(INVALID_USER_ID)
        , state(ConnectionState::DISCONNECTED)
        , current_room(LOBBY_ROOM_ID)
        , messages_sent(0)
        , messages_received(0)
        , avg_latency_ms(0.0)
        , missed_heartbeats(0)
        , client_queue_key(0)
    {}
};

// Connection event types
enum class ConnectionEvent {
    CONNECTED,
    DISCONNECTED,
    TIMEOUT,
    ERROR
};

// Callback for connection events
using ConnectionEventCallback = std::function<void(UserId, ConnectionEvent, const std::string&)>;

// Manages all client connections
class ConnectionManager {
public:
    explicit ConnectionManager(key_t base_key, size_t max_connections = MAX_USERS);
    ~ConnectionManager();

    // Non-copyable
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    // Connection lifecycle
    UserId accept_connection(const std::string& username, key_t client_queue_key);
    void close_connection(UserId user_id, const std::string& reason = "");
    void close_all_connections(const std::string& reason = "Server shutdown");

    // Connection queries
    bool is_connected(UserId user_id) const;
    bool is_username_taken(const std::string& username) const;
    ClientConnection* get_connection(UserId user_id);
    const ClientConnection* get_connection(UserId user_id) const;
    std::vector<UserId> get_all_user_ids() const;
    std::vector<std::pair<UserId, std::string>> get_all_users() const;
    size_t connection_count() const;

    // Activity tracking
    void update_activity(UserId user_id);
    void update_heartbeat(UserId user_id);
    void record_message_sent(UserId user_id);
    void record_message_received(UserId user_id);
    void record_latency(UserId user_id, double latency_ms);

    // Timeout management
    std::vector<UserId> check_timeouts(int64_t timeout_ms);
    std::vector<UserId> check_heartbeat_timeouts(int max_missed);

    // Room management
    void set_user_room(UserId user_id, RoomId room_id);
    RoomId get_user_room(UserId user_id) const;
    std::vector<UserId> get_users_in_room(RoomId room_id) const;

    // Message sending
    ErrorCode send_to_user(UserId user_id, const protocol::Message& msg);
    ErrorCode broadcast(const protocol::Message& msg, UserId exclude = INVALID_USER_ID);
    ErrorCode broadcast_to_room(RoomId room_id, const protocol::Message& msg, UserId exclude = INVALID_USER_ID);

    // Event callbacks
    void set_event_callback(ConnectionEventCallback callback) {
        event_callback_ = std::move(callback);
    }

    // Statistics
    struct Stats {
        size_t total_connections;
        size_t active_connections;
        size_t total_disconnections;
        uint64_t total_messages_sent;
        uint64_t total_messages_received;
        double avg_connection_duration_s;
    };
    Stats get_stats() const;

    // ID generation
    UserId generate_user_id();

private:
    key_t base_key_;
    size_t max_connections_;
    std::atomic<UserId> next_user_id_{SERVER_USER_ID + 1};

    // Connection storage
    mutable std::shared_mutex connections_mutex_;
    std::unordered_map<UserId, std::unique_ptr<ClientConnection>> connections_;
    std::unordered_map<std::string, UserId> username_to_id_;
    std::unordered_map<RoomId, std::unordered_set<UserId>> room_members_;

    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_{};

    // Callbacks
    ConnectionEventCallback event_callback_;

    // Internal helpers
    key_t generate_client_queue_key(UserId user_id);
    void emit_event(UserId user_id, ConnectionEvent event, const std::string& details);
    void cleanup_connection(ClientConnection* conn);
};

// Connection guard for automatic cleanup
class ConnectionGuard {
public:
    ConnectionGuard(ConnectionManager& manager, UserId user_id)
        : manager_(manager), user_id_(user_id), released_(false) {}

    ~ConnectionGuard() {
        if (!released_) {
            manager_.close_connection(user_id_, "Guard destroyed");
        }
    }

    void release() { released_ = true; }

    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    ConnectionManager& manager_;
    UserId user_id_;
    bool released_;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_CONNECTION_MANAGER_HPP
