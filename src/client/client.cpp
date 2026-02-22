#include <chatterbox/client/client.hpp>
#include <chatterbox/protocol/serializer.hpp>
#include <random>
#include <iostream>

namespace chatterbox {
namespace client {

Client::Client(const ClientConfig& config)
    : config_(config)
    , client_queue_key_(0)
{
    if (config_.enable_ui) {
        ui_ = std::make_unique<TerminalUI>();
    }
}

Client::~Client() {
    disconnect("Client destroyed");
    stop_ui();
}

key_t Client::generate_client_queue_key() {
    // Generate a unique key based on PID and random number
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    return config_.server_queue_key + dis(gen) + 1000;
}

ErrorCode Client::initialize_ipc() {
    try {
        // Open server queue (must already exist)
        server_queue_ = std::make_unique<ipc::MessageQueue>(config_.server_queue_key, false);

        // Create our client queue
        client_queue_key_ = generate_client_queue_key();
        client_queue_ = std::make_unique<ipc::MessageQueue>(client_queue_key_, true);
        client_queue_->set_owner(true);

        return ErrorCode::SUCCESS;

    } catch (const std::exception& e) {
        if (ui_) {
            ui_->display_error("Failed to initialize IPC: " + std::string(e.what()));
        }
        return ErrorCode::ERR_IPC_CREATE;
    }
}

void Client::cleanup_ipc() {
    client_queue_.reset();
    server_queue_.reset();

    if (client_queue_key_ != 0) {
        ipc::MessageQueue::remove(client_queue_key_);
        client_queue_key_ = 0;
    }
}

ErrorCode Client::connect(const std::string& username) {
    if (state_.load() == ClientState::CONNECTED) {
        return ErrorCode::SUCCESS;
    }

    if (!username.empty()) {
        config_.username = username;
    }

    if (config_.username.empty()) {
        emit_error(ErrorCode::ERR_INVALID_USER, "Username is required");
        return ErrorCode::ERR_INVALID_USER;
    }

    set_state(ClientState::CONNECTING, "Connecting...");

    // Initialize IPC
    auto err = initialize_ipc();
    if (err != ErrorCode::SUCCESS) {
        set_state(ClientState::DISCONNECTED, "IPC initialization failed");
        return err;
    }

    // Send connect request
    auto connect_msg = protocol::Message::create_connect_request(config_.username);
    // Store client queue key in timestamp field for server to extract
    connect_msg.header().timestamp = static_cast<uint64_t>(client_queue_key_ - config_.server_queue_key - 1000) % 10000
                                    + protocol::Timestamp::now().value();
    connect_msg.update_checksum();

    err = send_raw(connect_msg);
    if (err != ErrorCode::SUCCESS) {
        cleanup_ipc();
        set_state(ClientState::DISCONNECTED, "Failed to send connect request");
        return err;
    }

    // Wait for response
    std::vector<uint8_t> buffer(MAX_MESSAGE_SIZE);
    size_t size = buffer.size();

    // Try to receive response with timeout
    auto start = protocol::MonotonicTimestamp::now();
    bool received = false;

    while (start.elapsed_ms() < config_.connection_timeout_ms) {
        err = client_queue_->receive(0, buffer.data(), size, false);
        if (err == ErrorCode::SUCCESS) {
            received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!received) {
        cleanup_ipc();
        set_state(ClientState::DISCONNECTED, "Connection timeout");
        return ErrorCode::ERR_TIMEOUT;
    }

    try {
        auto response = protocol::Message::deserialize(buffer.data(), size);
        handle_connect_response(response);

        if (state_.load() == ClientState::CONNECTED) {
            // Start worker threads
            running_.store(true);
            receiver_thread_ = std::thread(&Client::receiver_loop, this);
            heartbeat_thread_ = std::thread(&Client::heartbeat_loop, this);
            connect_time_ = protocol::Timestamp::now();

            return ErrorCode::SUCCESS;
        }
    } catch (const std::exception& e) {
        cleanup_ipc();
        set_state(ClientState::DISCONNECTED, "Invalid response: " + std::string(e.what()));
        return ErrorCode::ERR_INVALID_MESSAGE;
    }

    cleanup_ipc();
    return ErrorCode::ERR_CONNECTION_LOST;
}

ErrorCode Client::disconnect(const std::string& reason) {
    if (state_.load() == ClientState::DISCONNECTED) {
        return ErrorCode::SUCCESS;
    }

    // Send disconnect message
    if (server_queue_ && user_id_.load() != INVALID_USER_ID) {
        auto msg = protocol::Message::create_disconnect(user_id_.load(), reason);
        send_raw(msg);
    }

    // Stop threads
    running_.store(false);

    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // Cleanup
    user_id_.store(INVALID_USER_ID);
    cleanup_ipc();
    set_state(ClientState::DISCONNECTED, reason);

    return ErrorCode::SUCCESS;
}

ErrorCode Client::reconnect() {
    if (!config_.auto_reconnect) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    set_state(ClientState::RECONNECTING, "Reconnecting...");

    int attempts = 0;
    while (attempts < config_.max_reconnect_attempts) {
        attempts++;

        if (ui_) {
            ui_->display_system_message("Reconnection attempt " + std::to_string(attempts) + "...");
        }

        // Wait before retry
        std::this_thread::sleep_for(std::chrono::seconds(attempts));

        auto err = connect();
        if (err == ErrorCode::SUCCESS) {
            if (ui_) {
                ui_->display_system_message("Reconnected successfully!");
            }

            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.reconnect_count++;
            return ErrorCode::SUCCESS;
        }
    }

    set_state(ClientState::ERROR, "Reconnection failed after " + std::to_string(attempts) + " attempts");
    return ErrorCode::ERR_CONNECTION_LOST;
}

ErrorCode Client::send_message(const std::string& content) {
    if (state_.load() != ClientState::CONNECTED) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    auto msg = protocol::Message::create_chat_message(user_id_.load(), content);
    msg.set_sequence(next_sequence_.fetch_add(1));
    msg.update_checksum();

    auto err = send_raw(msg);
    if (err == ErrorCode::SUCCESS) {
        update_stats(msg, true);
    }
    return err;
}

ErrorCode Client::send_private_message(UserId recipient, const std::string& content) {
    if (state_.load() != ClientState::CONNECTED) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    auto msg = protocol::Message::create_private_message(user_id_.load(), recipient, content);
    msg.set_sequence(next_sequence_.fetch_add(1));
    msg.update_checksum();

    auto err = send_raw(msg);
    if (err == ErrorCode::SUCCESS) {
        update_stats(msg, true);
    }
    return err;
}

ErrorCode Client::send_private_message(const std::string& username, const std::string& content) {
    // Look up user ID from cached list
    std::lock_guard<std::mutex> lock(user_list_mutex_);
    for (const auto& [id, name] : user_list_cache_) {
        if (name == username) {
            return send_private_message(id, content);
        }
    }

    if (ui_) {
        ui_->display_error("User not found: " + username);
    }
    return ErrorCode::ERR_INVALID_USER;
}

ErrorCode Client::join_room(RoomId room_id) {
    if (state_.load() != ClientState::CONNECTED) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    protocol::Message msg(protocol::MessageType::ROOM_JOIN);
    msg.set_sender_id(user_id_.load());

    protocol::BinaryWriter writer;
    writer.write_u32(room_id);
    msg.set_payload(writer.data());
    msg.set_sequence(next_sequence_.fetch_add(1));
    msg.update_checksum();

    auto err = send_raw(msg);
    if (err == ErrorCode::SUCCESS) {
        current_room_.store(room_id);
    }
    return err;
}

ErrorCode Client::leave_room() {
    return join_room(LOBBY_ROOM_ID);
}

ErrorCode Client::create_room(const std::string& name) {
    if (state_.load() != ClientState::CONNECTED) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    protocol::Message msg(protocol::MessageType::ROOM_CREATE);
    msg.set_sender_id(user_id_.load());
    msg.set_payload(name);
    msg.set_sequence(next_sequence_.fetch_add(1));
    msg.update_checksum();

    return send_raw(msg);
}

ErrorCode Client::request_user_list() {
    if (state_.load() != ClientState::CONNECTED) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    protocol::Message msg(protocol::MessageType::USER_LIST);
    msg.set_sender_id(user_id_.load());
    msg.set_sequence(next_sequence_.fetch_add(1));
    msg.update_checksum();

    return send_raw(msg);
}

std::vector<std::pair<UserId, std::string>> Client::get_cached_user_list() const {
    std::lock_guard<std::mutex> lock(user_list_mutex_);
    return user_list_cache_;
}

void Client::run_ui() {
    if (!ui_) return;

    ui_->set_input_callback([this](const std::string& input) {
        if (!input.empty()) {
            if (input[0] == '/') {
                // Command
                if (input_handler_) {
                    input_handler_->process_input(input);
                }
            } else {
                // Regular message
                send_message(input);
            }
        }
    });

    // Create input handler
    input_handler_ = std::make_unique<InputHandler>(*this);
    input_handler_->register_default_commands();

    ui_->start();

    // Main UI loop
    while (ui_->is_running()) {
        std::string input = ui_->get_input();
        if (!input.empty()) {
            if (input[0] == '/') {
                input_handler_->process_input(input);
            } else if (is_connected()) {
                send_message(input);
            } else {
                ui_->display_error("Not connected. Use /connect to connect.");
            }
        }
    }
}

void Client::stop_ui() {
    if (ui_) {
        ui_->stop();
    }
}

void Client::wait() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Client::Stats Client::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s = stats_;

    if (state_.load() == ClientState::CONNECTED && connect_time_.is_valid()) {
        s.connected_duration_ms = connect_time_.elapsed_ms();
    }

    return s;
}

void Client::receiver_loop() {
    std::vector<uint8_t> buffer(MAX_MESSAGE_SIZE);

    while (running_.load()) {
        size_t size = buffer.size();
        auto err = client_queue_->receive(0, buffer.data(), size, false);

        if (err == ErrorCode::SUCCESS) {
            try {
                auto msg = protocol::Message::deserialize(buffer.data(), size);
                if (msg.is_valid()) {
                    process_message(msg);
                }
            } catch (const std::exception& e) {
                // Invalid message, ignore
            }
        } else if (err == ErrorCode::ERR_TIMEOUT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            // Error receiving, might be disconnected
            if (running_.load()) {
                set_state(ClientState::ERROR, "Receive error");
                if (config_.auto_reconnect) {
                    reconnect();
                }
            }
            break;
        }
    }
}

void Client::heartbeat_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeat_interval_ms));

        if (!running_.load()) break;

        if (state_.load() == ClientState::CONNECTED) {
            auto hb = protocol::Message::create_heartbeat(user_id_.load());
            send_raw(hb);
        }
    }
}

void Client::process_message(const protocol::Message& msg) {
    update_stats(msg, false);

    switch (msg.type()) {
        case protocol::MessageType::CONNECT_RESPONSE:
            handle_connect_response(msg);
            break;

        case protocol::MessageType::DISCONNECT:
            handle_disconnect(msg);
            break;

        case protocol::MessageType::HEARTBEAT_ACK:
            handle_heartbeat_ack(msg);
            break;

        case protocol::MessageType::CHAT_MESSAGE:
        case protocol::MessageType::CHAT_BROADCAST:
            handle_chat_message(msg);
            break;

        case protocol::MessageType::PRIVATE_MESSAGE:
            handle_private_message(msg);
            break;

        case protocol::MessageType::USER_LIST:
            handle_user_list(msg);
            break;

        case protocol::MessageType::SYSTEM_MESSAGE:
            handle_system_message(msg);
            break;

        case protocol::MessageType::ERROR_MSG:
            handle_error(msg);
            break;

        default:
            break;
    }

    // Invoke callback
    if (message_callback_) {
        message_callback_(msg);
    }
}

void Client::handle_connect_response(const protocol::Message& msg) {
    auto response = protocol::PayloadSerializer::deserialize_connect_response(msg.payload());

    if (response.success) {
        user_id_.store(response.user_id);
        set_state(ClientState::CONNECTED, response.message);

        if (ui_) {
            ui_->display_system_message("Connected as " + config_.username +
                                       " (ID: " + std::to_string(response.user_id) + ")");
            ui_->display_system_message(response.message);
            ui_->set_connection_status("Connected");
        }
    } else {
        set_state(ClientState::DISCONNECTED, response.message);

        if (ui_) {
            ui_->display_error("Connection failed: " + response.message);
        }
    }
}

void Client::handle_disconnect(const protocol::Message& msg) {
    std::string reason = msg.payload_as_string();
    running_.store(false);
    set_state(ClientState::DISCONNECTED, reason);

    if (ui_) {
        ui_->display_system_message("Disconnected: " + reason);
        ui_->set_connection_status("Disconnected");
    }
}

void Client::handle_heartbeat_ack(const protocol::Message& msg) {
    // Calculate latency
    auto sent_time = protocol::Timestamp(msg.timestamp());
    double latency = sent_time.elapsed_ms();

    std::lock_guard<std::mutex> lock(stats_mutex_);
    // Exponential moving average
    double alpha = 0.1;
    stats_.avg_latency_ms = alpha * latency + (1.0 - alpha) * stats_.avg_latency_ms;
}

void Client::handle_chat_message(const protocol::Message& msg) {
    if (!ui_) return;

    // Parse broadcast message format
    protocol::BinaryReader reader(msg.payload());
    UserId sender_id = reader.read_u32();
    std::string sender_name = reader.read_string();
    std::string content = reader.read_string();

    bool is_self = (sender_id == user_id_.load());
    ui_->display_message(sender_name, content, false, is_self);
}

void Client::handle_private_message(const protocol::Message& msg) {
    if (!ui_) return;

    protocol::BinaryReader reader(msg.payload());
    UserId sender_id = reader.read_u32();
    std::string sender_name = reader.read_string();
    std::string content = reader.read_string();

    bool is_self = (sender_id == user_id_.load());
    ui_->display_message(sender_name, content, true, is_self);
}

void Client::handle_user_list(const protocol::Message& msg) {
    auto users = protocol::PayloadSerializer::deserialize_user_list(msg.payload());

    {
        std::lock_guard<std::mutex> lock(user_list_mutex_);
        user_list_cache_ = users;
        user_list_timestamp_ = protocol::Timestamp::now();
    }

    if (ui_) {
        ui_->display_user_list(users);
    }
}

void Client::handle_system_message(const protocol::Message& msg) {
    if (ui_) {
        ui_->display_system_message(msg.payload_as_string());
    }
}

void Client::handle_error(const protocol::Message& msg) {
    auto err = protocol::PayloadSerializer::deserialize_error(msg.payload());

    if (ui_) {
        ui_->display_error(err.message);
    }

    emit_error(err.code, err.message);
}

ErrorCode Client::send_raw(const protocol::Message& msg) {
    if (!server_queue_) {
        return ErrorCode::ERR_CONNECTION_LOST;
    }

    auto data = msg.serialize();
    return server_queue_->send(1, data.data(), data.size(), false);
}

void Client::set_state(ClientState new_state, const std::string& reason) {
    ClientState old_state = state_.exchange(new_state);

    if (old_state != new_state && state_callback_) {
        state_callback_(new_state, reason);
    }
}

void Client::emit_error(ErrorCode code, const std::string& message) {
    if (error_callback_) {
        error_callback_(code, message);
    }
}

void Client::update_stats(const protocol::Message& msg, bool sent) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    if (sent) {
        stats_.messages_sent++;
        stats_.bytes_sent += msg.total_size();
    } else {
        stats_.messages_received++;
        stats_.bytes_received += msg.total_size();
    }
}

} // namespace client
} // namespace chatterbox
