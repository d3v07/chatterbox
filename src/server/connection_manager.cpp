#include <chatterbox/server/connection_manager.hpp>
#include <algorithm>

namespace chatterbox {
namespace server {

ConnectionManager::ConnectionManager(key_t base_key, size_t max_connections)
    : base_key_(base_key)
    , max_connections_(max_connections)
{
    // Initialize lobby room
    room_members_[LOBBY_ROOM_ID] = {};
}

ConnectionManager::~ConnectionManager() {
    close_all_connections("Connection manager destroyed");
}

UserId ConnectionManager::generate_user_id() {
    return next_user_id_.fetch_add(1);
}

key_t ConnectionManager::generate_client_queue_key(UserId user_id) {
    return base_key_ + static_cast<key_t>(user_id) + 1000;
}

UserId ConnectionManager::accept_connection(const std::string& username, key_t client_queue_key) {
    std::unique_lock<std::shared_mutex> lock(connections_mutex_);

    // Check capacity
    if (connections_.size() >= max_connections_) {
        return INVALID_USER_ID;
    }

    // Check if username is taken
    if (username_to_id_.find(username) != username_to_id_.end()) {
        return INVALID_USER_ID;
    }

    // Generate new user ID
    UserId user_id = generate_user_id();

    // Create connection
    auto conn = std::make_unique<ClientConnection>();
    conn->user_id = user_id;
    conn->username = username;
    conn->client_queue_key = client_queue_key;
    conn->state = ConnectionState::CONNECTED;
    conn->connected_at = protocol::Timestamp::now();
    conn->last_activity = conn->connected_at;
    conn->last_heartbeat = conn->connected_at;
    conn->current_room = LOBBY_ROOM_ID;

    // Try to open client's message queue
    try {
        conn->client_queue = std::make_unique<ipc::MessageQueue>(client_queue_key, false);
    } catch (const std::exception& e) {
        // Failed to connect to client queue
        return INVALID_USER_ID;
    }

    // Store connection
    connections_[user_id] = std::move(conn);
    username_to_id_[username] = user_id;
    room_members_[LOBBY_ROOM_ID].insert(user_id);

    // Update stats
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_connections++;
        stats_.active_connections = connections_.size();
    }

    // Emit event
    emit_event(user_id, ConnectionEvent::CONNECTED, username + " connected");

    return user_id;
}

void ConnectionManager::close_connection(UserId user_id, const std::string& reason) {
    std::unique_lock<std::shared_mutex> lock(connections_mutex_);

    auto it = connections_.find(user_id);
    if (it == connections_.end()) {
        return;
    }

    ClientConnection* conn = it->second.get();
    std::string username = conn->username;

    // Update state
    conn->state = ConnectionState::DISCONNECTING;

    // Remove from room
    if (room_members_.count(conn->current_room)) {
        room_members_[conn->current_room].erase(user_id);
    }

    // Remove from username map
    username_to_id_.erase(conn->username);

    // Cleanup connection
    cleanup_connection(conn);

    // Remove connection
    connections_.erase(it);

    // Update stats
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_disconnections++;
        stats_.active_connections = connections_.size();
    }

    // Emit event (after releasing lock would be safer, but we need the username)
    lock.unlock();
    emit_event(user_id, ConnectionEvent::DISCONNECTED, username + " disconnected: " + reason);
}

void ConnectionManager::close_all_connections(const std::string& reason) {
    std::unique_lock<std::shared_mutex> lock(connections_mutex_);

    std::vector<UserId> user_ids;
    for (const auto& [id, _] : connections_) {
        user_ids.push_back(id);
    }

    lock.unlock();

    for (UserId id : user_ids) {
        close_connection(id, reason);
    }
}

void ConnectionManager::cleanup_connection(ClientConnection* conn) {
    if (!conn) return;

    // Close client queue
    conn->client_queue.reset();
    conn->state = ConnectionState::DISCONNECTED;
}

bool ConnectionManager::is_connected(UserId user_id) const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    return it != connections_.end() && it->second->state == ConnectionState::CONNECTED;
}

bool ConnectionManager::is_username_taken(const std::string& username) const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    return username_to_id_.find(username) != username_to_id_.end();
}

ClientConnection* ConnectionManager::get_connection(UserId user_id) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

const ClientConnection* ConnectionManager::get_connection(UserId user_id) const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    return it != connections_.end() ? it->second.get() : nullptr;
}

std::vector<UserId> ConnectionManager::get_all_user_ids() const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    std::vector<UserId> ids;
    ids.reserve(connections_.size());
    for (const auto& [id, _] : connections_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::pair<UserId, std::string>> ConnectionManager::get_all_users() const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    std::vector<std::pair<UserId, std::string>> users;
    users.reserve(connections_.size());
    for (const auto& [id, conn] : connections_) {
        if (conn->state == ConnectionState::CONNECTED) {
            users.emplace_back(id, conn->username);
        }
    }
    return users;
}

size_t ConnectionManager::connection_count() const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    return connections_.size();
}

void ConnectionManager::update_activity(UserId user_id) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    if (it != connections_.end()) {
        it->second->last_activity = protocol::Timestamp::now();
    }
}

void ConnectionManager::update_heartbeat(UserId user_id) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    if (it != connections_.end()) {
        it->second->last_heartbeat = protocol::Timestamp::now();
        it->second->missed_heartbeats = 0;
    }
}

void ConnectionManager::record_message_sent(UserId user_id) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    if (it != connections_.end()) {
        it->second->messages_sent++;
        it->second->last_activity = protocol::Timestamp::now();
    }

    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_messages_sent++;
}

void ConnectionManager::record_message_received(UserId user_id) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    if (it != connections_.end()) {
        it->second->messages_received++;
    }

    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_messages_received++;
}

void ConnectionManager::record_latency(UserId user_id, double latency_ms) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    if (it != connections_.end()) {
        // Exponential moving average
        double alpha = 0.1;
        it->second->avg_latency_ms = alpha * latency_ms + (1.0 - alpha) * it->second->avg_latency_ms;
    }
}

std::vector<UserId> ConnectionManager::check_timeouts(int64_t timeout_ms) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    std::vector<UserId> timed_out;

    auto now = protocol::Timestamp::now();
    for (const auto& [id, conn] : connections_) {
        if (conn->state == ConnectionState::CONNECTED) {
            int64_t elapsed = (now - conn->last_activity) / 1000; // Convert to ms
            if (elapsed > timeout_ms) {
                timed_out.push_back(id);
            }
        }
    }

    return timed_out;
}

std::vector<UserId> ConnectionManager::check_heartbeat_timeouts(int max_missed) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    std::vector<UserId> timed_out;

    for (const auto& [id, conn] : connections_) {
        if (conn->state == ConnectionState::CONNECTED) {
            conn->missed_heartbeats++;
            if (conn->missed_heartbeats > max_missed) {
                timed_out.push_back(id);
            }
        }
    }

    return timed_out;
}

void ConnectionManager::set_user_room(UserId user_id, RoomId room_id) {
    std::unique_lock<std::shared_mutex> lock(connections_mutex_);

    auto it = connections_.find(user_id);
    if (it == connections_.end()) return;

    RoomId old_room = it->second->current_room;
    if (old_room == room_id) return;

    // Remove from old room
    if (room_members_.count(old_room)) {
        room_members_[old_room].erase(user_id);
    }

    // Add to new room
    room_members_[room_id].insert(user_id);
    it->second->current_room = room_id;
}

RoomId ConnectionManager::get_user_room(UserId user_id) const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);
    auto it = connections_.find(user_id);
    return it != connections_.end() ? it->second->current_room : INVALID_ROOM_ID;
}

std::vector<UserId> ConnectionManager::get_users_in_room(RoomId room_id) const {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);

    auto it = room_members_.find(room_id);
    if (it == room_members_.end()) {
        return {};
    }

    return std::vector<UserId>(it->second.begin(), it->second.end());
}

ErrorCode ConnectionManager::send_to_user(UserId user_id, const protocol::Message& msg) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);

    auto it = connections_.find(user_id);
    if (it == connections_.end() || !it->second->client_queue) {
        return ErrorCode::ERR_INVALID_USER;
    }

    auto data = msg.serialize();
    return it->second->client_queue->send(1, data.data(), data.size(), false);
}

ErrorCode ConnectionManager::broadcast(const protocol::Message& msg, UserId exclude) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);

    auto data = msg.serialize();
    ErrorCode result = ErrorCode::SUCCESS;

    for (const auto& [id, conn] : connections_) {
        if (id != exclude && conn->state == ConnectionState::CONNECTED && conn->client_queue) {
            auto err = conn->client_queue->send(1, data.data(), data.size(), false);
            if (err != ErrorCode::SUCCESS) {
                result = err; // Return last error
            }
        }
    }

    return result;
}

ErrorCode ConnectionManager::broadcast_to_room(RoomId room_id, const protocol::Message& msg, UserId exclude) {
    std::shared_lock<std::shared_mutex> lock(connections_mutex_);

    auto room_it = room_members_.find(room_id);
    if (room_it == room_members_.end()) {
        return ErrorCode::ERR_INVALID_USER;
    }

    auto data = msg.serialize();
    ErrorCode result = ErrorCode::SUCCESS;

    for (UserId id : room_it->second) {
        if (id != exclude) {
            auto conn_it = connections_.find(id);
            if (conn_it != connections_.end() &&
                conn_it->second->state == ConnectionState::CONNECTED &&
                conn_it->second->client_queue) {
                auto err = conn_it->second->client_queue->send(1, data.data(), data.size(), false);
                if (err != ErrorCode::SUCCESS) {
                    result = err;
                }
            }
        }
    }

    return result;
}

void ConnectionManager::emit_event(UserId user_id, ConnectionEvent event, const std::string& details) {
    if (event_callback_) {
        event_callback_(user_id, event, details);
    }
}

ConnectionManager::Stats ConnectionManager::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace server
} // namespace chatterbox
