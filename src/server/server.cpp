#include <chatterbox/server/server.hpp>
#include <chatterbox/protocol/serializer.hpp>
#include <iostream>
#include <fstream>
#include <chrono>

namespace chatterbox {
namespace server {

Server::Server(const ServerConfig& config)
    : config_(config)
{
}

Server::~Server() {
    stop();
}

ErrorCode Server::initialize_ipc() {
    try {
        // Create server message queue
        server_queue_ = std::make_unique<ipc::MessageQueue>(config_.ipc_base_key, true);
        server_queue_->set_owner(true);

        // Create shared memory
        shared_memory_ = std::make_unique<ipc::ChatSharedMemory>(
            config_.ipc_base_key + 1, true
        );

        // Create read-write lock
        rw_lock_ = std::make_unique<ipc::RWLock>(config_.ipc_base_key + 2, true);

        log("IPC resources initialized");
        return ErrorCode::SUCCESS;

    } catch (const std::exception& e) {
        log("Failed to initialize IPC: " + std::string(e.what()));
        return ErrorCode::ERR_IPC_CREATE;
    }
}

void Server::cleanup_ipc() {
    rw_lock_.reset();
    shared_memory_.reset();
    server_queue_.reset();

    // Force cleanup of IPC resources
    ipc::MessageQueue::remove(config_.ipc_base_key);
    ipc::SharedMemory::remove(config_.ipc_base_key + 1);
    ipc::Semaphore::remove(config_.ipc_base_key + 2);

    log("IPC resources cleaned up");
}

ErrorCode Server::start() {
    if (running_.exchange(true)) {
        return ErrorCode::SUCCESS; // Already running
    }

    log("Starting ChatterBox server...");

    // Initialize IPC
    auto err = initialize_ipc();
    if (err != ErrorCode::SUCCESS) {
        running_.store(false);
        return err;
    }

    // Create thread pool
    thread_pool_ = std::make_unique<sync::ThreadPool>(config_.thread_pool_size);

    // Create connection manager
    connection_manager_ = std::make_unique<ConnectionManager>(
        config_.ipc_base_key, config_.max_users
    );

    connection_manager_->set_event_callback(
        [this](UserId user_id, ConnectionEvent event, const std::string& details) {
            if (event == ConnectionEvent::CONNECTED && connection_callback_) {
                auto* conn = connection_manager_->get_connection(user_id);
                if (conn) {
                    connection_callback_(user_id, conn->username);
                }
            } else if (event == ConnectionEvent::DISCONNECTED && disconnection_callback_) {
                disconnection_callback_(user_id, details);
            }
        }
    );

    // Create message router
    message_router_ = std::make_unique<MessageRouter>(*connection_manager_);

    // Register message handlers
    message_router_->register_handler(protocol::MessageType::CONNECT_REQUEST,
        [this](const protocol::Message& msg, UserId sender) {
            (void)sender;
            handle_connect_request(msg);
        }
    );

    message_router_->register_handler(protocol::MessageType::DISCONNECT,
        [this](const protocol::Message& msg, UserId sender) {
            (void)sender;
            handle_disconnect(msg);
        }
    );

    message_router_->register_handler(protocol::MessageType::HEARTBEAT,
        [this](const protocol::Message& msg, UserId sender) {
            (void)sender;
            handle_heartbeat(msg);
        }
    );

    message_router_->register_handler(protocol::MessageType::CHAT_MESSAGE,
        [this](const protocol::Message& msg, UserId sender) {
            handle_chat_message(msg);
            connection_manager_->record_message_received(sender);
        }
    );

    message_router_->register_handler(protocol::MessageType::PRIVATE_MESSAGE,
        [this](const protocol::Message& msg, UserId sender) {
            handle_private_message(msg);
            connection_manager_->record_message_received(sender);
        }
    );

    message_router_->register_handler(protocol::MessageType::USER_LIST,
        [this](const protocol::Message& msg, UserId sender) {
            (void)msg;
            handle_user_list_request(protocol::Message(protocol::MessageType::USER_LIST));
            (void)sender;
        }
    );

    // Add spam filter (10 messages per second max)
    auto spam_filter = std::make_shared<SpamFilter>(10);
    message_router_->add_filter([spam_filter](const protocol::Message& msg, UserId sender) {
        return (*spam_filter)(msg, sender);
    });

    // Start message router
    message_router_->start();

    // Start worker threads
    should_stop_.store(false);
    receiver_thread_ = std::thread(&Server::receiver_loop, this);
    heartbeat_thread_ = std::thread(&Server::heartbeat_loop, this);
    cleanup_thread_ = std::thread(&Server::cleanup_loop, this);

    // Update shared memory state
    if (shared_memory_ && shared_memory_->is_valid()) {
        shared_memory_->state()->server_running.store(true);
    }

    log("ChatterBox server started successfully");
    return ErrorCode::SUCCESS;
}

void Server::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }

    log("Stopping ChatterBox server...");
    should_stop_.store(true);

    // Update shared memory state
    if (shared_memory_ && shared_memory_->is_valid()) {
        shared_memory_->state()->server_running.store(false);
    }

    // Broadcast shutdown message to all clients
    if (connection_manager_) {
        auto shutdown_msg = protocol::Message::create_system_message("Server shutting down");
        connection_manager_->broadcast(shutdown_msg);
        connection_manager_->close_all_connections("Server shutdown");
    }

    // Stop message router
    if (message_router_) {
        message_router_->stop();
    }

    // Wait for threads
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }

    // Shutdown thread pool
    if (thread_pool_) {
        thread_pool_->shutdown();
    }

    // Cleanup resources
    message_router_.reset();
    connection_manager_.reset();
    thread_pool_.reset();
    cleanup_ipc();

    log("ChatterBox server stopped");
}

void Server::wait() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

ServerStats Server::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ServerStats s = stats_;

    if (connection_manager_) {
        s.connected_users = connection_manager_->connection_count();
    }

    if (shared_memory_ && shared_memory_->is_valid()) {
        auto now = protocol::time_utils::current_time_ms();
        auto start = shared_memory_->state()->server_start_time.load();
        s.uptime_seconds = (now - start) / 1000;
    }

    return s;
}

ErrorCode Server::send_to_user(UserId user_id, const protocol::Message& msg) {
    if (!connection_manager_) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }
    return connection_manager_->send_to_user(user_id, msg);
}

ErrorCode Server::broadcast(const protocol::Message& msg, UserId exclude) {
    if (!connection_manager_) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }
    return connection_manager_->broadcast(msg, exclude);
}

ErrorCode Server::send_system_message(const std::string& content) {
    auto msg = protocol::Message::create_system_message(content);
    return broadcast(msg);
}

std::vector<std::pair<UserId, std::string>> Server::get_connected_users() const {
    if (!connection_manager_) {
        return {};
    }
    return connection_manager_->get_all_users();
}

bool Server::kick_user(UserId user_id, const std::string& reason) {
    if (!connection_manager_) {
        return false;
    }

    if (!connection_manager_->is_connected(user_id)) {
        return false;
    }

    // Send kick message
    auto msg = protocol::Message::create_disconnect(user_id, reason);
    connection_manager_->send_to_user(user_id, msg);

    // Close connection
    connection_manager_->close_connection(user_id, reason);

    return true;
}

void Server::receiver_loop() {
    log("Receiver thread started");

    std::vector<uint8_t> buffer(MAX_MESSAGE_SIZE);

    while (!should_stop_.load()) {
        size_t size = buffer.size();
        auto err = server_queue_->receive(0, buffer.data(), size, false);

        if (err == ErrorCode::SUCCESS) {
            try {
                auto msg = protocol::Message::deserialize(buffer.data(), size);
                if (msg.is_valid()) {
                    // Process message in thread pool
                    thread_pool_->execute([this, msg = std::move(msg)]() mutable {
                        process_message(msg);
                    });
                } else {
                    log("Received invalid message (checksum mismatch)");
                }
            } catch (const std::exception& e) {
                log("Failed to deserialize message: " + std::string(e.what()));
            }
        } else if (err == ErrorCode::ERR_TIMEOUT) {
            // No message available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            log("Error receiving message: " + std::string(error_to_string(err)));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    log("Receiver thread stopped");
}

void Server::heartbeat_loop() {
    log("Heartbeat thread started");

    while (!should_stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeat_interval_ms));

        if (should_stop_.load()) break;

        // Check for timeouts
        auto timed_out = connection_manager_->check_heartbeat_timeouts(3);
        for (UserId id : timed_out) {
            log("User " + std::to_string(id) + " timed out (missed heartbeats)");
            connection_manager_->close_connection(id, "Heartbeat timeout");
        }
    }

    log("Heartbeat thread stopped");
}

void Server::cleanup_loop() {
    log("Cleanup thread started");

    while (!should_stop_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        if (should_stop_.load()) break;

        // Check for inactive connections
        auto inactive = connection_manager_->check_timeouts(config_.connection_timeout_ms);
        for (UserId id : inactive) {
            log("User " + std::to_string(id) + " timed out (inactive)");
            connection_manager_->close_connection(id, "Inactivity timeout");
        }

        // Log statistics
        auto stats = get_stats();
        log("Stats: " + std::to_string(stats.connected_users) + " active connections, " +
            std::to_string(stats.messages_received) + " messages received");
    }

    log("Cleanup thread stopped");
}

void Server::process_message(const protocol::Message& msg) {
    update_stats(msg, true);

    // Route to appropriate handler
    message_router_->route(msg, msg.sender_id());
}

void Server::handle_connect_request(const protocol::Message& msg) {
    std::string username = protocol::PayloadSerializer::deserialize_connect_request(msg.payload());

    log("Connection request from: " + username);

    // Extract client queue key from message (stored in reserved field or calculate from timestamp)
    key_t client_queue_key = config_.ipc_base_key + static_cast<key_t>(msg.header().timestamp % 10000) + 1000;

    // Try to accept connection
    UserId user_id = connection_manager_->accept_connection(username, client_queue_key);

    protocol::Message response;
    if (user_id != INVALID_USER_ID) {
        response = protocol::Message::create_connect_response(user_id, true, "Welcome to ChatterBox!");
        log("User connected: " + username + " (ID: " + std::to_string(user_id) + ")");

        // Broadcast join message
        auto join_msg = protocol::Message::create_system_message(username + " has joined the chat");
        connection_manager_->broadcast(join_msg, user_id);

        // Update stats
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.connections_total++;
        }
    } else {
        if (connection_manager_->is_username_taken(username)) {
            response = protocol::Message::create_connect_response(INVALID_USER_ID, false, "Username already taken");
        } else if (connection_manager_->connection_count() >= config_.max_users) {
            response = protocol::Message::create_connect_response(INVALID_USER_ID, false, "Server is full");
        } else {
            response = protocol::Message::create_connect_response(INVALID_USER_ID, false, "Connection failed");
        }
        log("Connection rejected for: " + username);
    }

    // Send response (try to send via client queue)
    try {
        ipc::MessageQueue client_queue(client_queue_key, false);
        auto data = response.serialize();
        client_queue.send(1, data.data(), data.size(), false);
    } catch (...) {
        log("Failed to send connect response to client");
    }
}

void Server::handle_disconnect(const protocol::Message& msg) {
    UserId user_id = msg.sender_id();
    std::string reason = msg.payload_as_string();

    auto* conn = connection_manager_->get_connection(user_id);
    std::string username = conn ? conn->username : "Unknown";

    log("User disconnecting: " + username + " (reason: " + reason + ")");

    connection_manager_->close_connection(user_id, reason);

    // Broadcast leave message
    auto leave_msg = protocol::Message::create_system_message(username + " has left the chat");
    connection_manager_->broadcast(leave_msg);
}

void Server::handle_heartbeat(const protocol::Message& msg) {
    UserId user_id = msg.sender_id();
    connection_manager_->update_heartbeat(user_id);

    // Send heartbeat ack
    auto ack = protocol::Message::create_heartbeat_ack(user_id);
    connection_manager_->send_to_user(user_id, ack);
}

void Server::handle_chat_message(const protocol::Message& msg) {
    UserId sender = msg.sender_id();
    auto* conn = connection_manager_->get_connection(sender);
    if (!conn) return;

    std::string content = protocol::PayloadSerializer::deserialize_chat_message(msg.payload());

    // Create broadcast message with sender info
    protocol::BinaryWriter writer;
    writer.write_u32(sender);
    writer.write_string(conn->username);
    writer.write_string(content);

    protocol::Message broadcast_msg(protocol::MessageType::CHAT_BROADCAST);
    broadcast_msg.set_sender_id(sender);
    broadcast_msg.set_payload(writer.data());
    broadcast_msg.set_timestamp(msg.timestamp());
    broadcast_msg.set_sequence(message_router_->next_sequence());
    broadcast_msg.update_checksum();

    // Broadcast to room
    connection_manager_->broadcast_to_room(conn->current_room, broadcast_msg);

    // Update stats
    connection_manager_->record_message_sent(sender);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_received++;
        stats_.messages_sent += connection_manager_->get_users_in_room(conn->current_room).size();
    }

    // Invoke callback if set
    if (message_callback_) {
        message_callback_(msg, sender);
    }
}

void Server::handle_private_message(const protocol::Message& msg) {
    UserId sender = msg.sender_id();
    auto pm = protocol::PayloadSerializer::deserialize_private_message(msg.payload());

    auto* sender_conn = connection_manager_->get_connection(sender);
    if (!sender_conn) return;

    // Create message with sender info
    protocol::BinaryWriter writer;
    writer.write_u32(sender);
    writer.write_string(sender_conn->username);
    writer.write_string(pm.content);

    protocol::Message forward_msg(protocol::MessageType::PRIVATE_MESSAGE);
    forward_msg.set_sender_id(sender);
    forward_msg.set_payload(writer.data());
    forward_msg.set_timestamp(msg.timestamp());
    forward_msg.set_sequence(message_router_->next_sequence());
    forward_msg.update_checksum();

    // Send to recipient
    connection_manager_->send_to_user(pm.recipient, forward_msg);

    // Echo to sender
    connection_manager_->send_to_user(sender, forward_msg);

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_received++;
        stats_.messages_sent += 2;
    }
}

void Server::handle_room_command(const protocol::Message& msg) {
    // Room management (join, leave, create, list)
    (void)msg; // TODO: Implement room commands
}

void Server::handle_user_list_request(const protocol::Message& msg) {
    UserId sender = msg.sender_id();

    auto users = connection_manager_->get_all_users();
    auto response = protocol::Message::create_user_list(users);
    response.set_sequence(message_router_->next_sequence());
    response.update_checksum();

    connection_manager_->send_to_user(sender, response);
}

void Server::update_stats(const protocol::Message& msg, bool received) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    if (received) {
        stats_.messages_received++;
        stats_.bytes_received += msg.total_size();
    } else {
        stats_.messages_sent++;
        stats_.bytes_sent += msg.total_size();
    }
}

void Server::log(const std::string& message) {
    if (!config_.enable_logging) return;

    auto now = protocol::Timestamp::now();
    std::string log_line = "[" + now.to_readable_string() + "] " + message;

    std::cout << log_line << std::endl;

    // Write to file if configured
    if (!config_.log_file.empty()) {
        std::ofstream file(config_.log_file, std::ios::app);
        if (file) {
            file << log_line << "\n";
        }
    }
}

} // namespace server
} // namespace chatterbox
