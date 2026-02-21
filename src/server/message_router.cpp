#include <chatterbox/server/message_router.hpp>
#include <chatterbox/server/connection_manager.hpp>
#include <chatterbox/protocol/serializer.hpp>
#include <algorithm>

namespace chatterbox {
namespace server {

MessageRouter::MessageRouter(ConnectionManager& connection_manager)
    : connection_manager_(connection_manager)
{
}

MessageRouter::~MessageRouter() {
    stop();
}

void MessageRouter::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }

    processor_thread_ = std::thread(&MessageRouter::processor_loop, this);
}

void MessageRouter::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }

    queue_event_.set();

    if (processor_thread_.joinable()) {
        processor_thread_.join();
    }
}

void MessageRouter::register_handler(protocol::MessageType type, MessageHandler handler) {
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    handlers_[type] = std::move(handler);
}

void MessageRouter::unregister_handler(protocol::MessageType type) {
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    handlers_.erase(type);
}

void MessageRouter::add_filter(MessageFilter filter) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    filters_.push_back(std::move(filter));
}

void MessageRouter::clear_filters() {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    filters_.clear();
}

ErrorCode MessageRouter::route(const protocol::Message& msg, UserId sender) {
    auto start = protocol::MonotonicTimestamp::now();

    // Apply filters
    if (!apply_filters(msg, sender)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_dropped++;
        return ErrorCode::ERR_PERMISSION_DENIED;
    }

    // Track sequence
    {
        std::lock_guard<std::mutex> lock(sequence_mutex_);
        last_sequence_[sender] = msg.sequence();
    }

    // Check rate limit
    if (!check_rate_limit(sender)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_dropped++;
        return ErrorCode::ERR_PERMISSION_DENIED;
    }

    // Invoke handler
    invoke_handler(msg, sender);

    // Update statistics
    auto elapsed = start.elapsed_us() / 1000.0;
    update_stats(true, elapsed);

    return ErrorCode::SUCCESS;
}

ErrorCode MessageRouter::send_direct(UserId target, const protocol::Message& msg) {
    return deliver_message(target, msg);
}

ErrorCode MessageRouter::broadcast_direct(const protocol::Message& msg, UserId exclude) {
    return connection_manager_.broadcast(msg, exclude);
}

void MessageRouter::enqueue(const protocol::Message& msg, UserId sender, UserId target, int priority) {
    QueuedMessage qm;
    qm.message = msg;
    qm.sender = sender;
    qm.target = target;
    qm.queued_at = protocol::Timestamp::now();
    qm.priority = priority;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push(std::move(qm));
    }

    queue_event_.set();
}

void MessageRouter::process_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    while (!message_queue_.empty()) {
        QueuedMessage qm = std::move(const_cast<QueuedMessage&>(message_queue_.top()));
        message_queue_.pop();

        if (qm.target == BROADCAST_USER_ID) {
            broadcast_direct(qm.message, qm.sender);
        } else {
            deliver_message(qm.target, qm.message);
        }
    }
}

size_t MessageRouter::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return message_queue_.size();
}

void MessageRouter::require_ack(const protocol::Message& msg, UserId target, int64_t timeout_ms) {
    PendingAck pending;
    pending.message = msg;
    pending.sent_at = protocol::Timestamp::now();
    pending.target = target;
    pending.retry_count = 0;
    pending.timeout_ms = timeout_ms;

    std::lock_guard<std::mutex> lock(acks_mutex_);
    pending_acks_[msg.sequence()] = std::move(pending);
}

void MessageRouter::handle_ack(SequenceNum sequence, UserId sender) {
    std::lock_guard<std::mutex> lock(acks_mutex_);

    auto it = pending_acks_.find(sequence);
    if (it != pending_acks_.end() && it->second.target == sender) {
        pending_acks_.erase(it);

        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.acks_received++;
    }
}

void MessageRouter::handle_nack(SequenceNum sequence, UserId sender, const std::string& reason) {
    std::lock_guard<std::mutex> lock(acks_mutex_);

    auto it = pending_acks_.find(sequence);
    if (it != pending_acks_.end() && it->second.target == sender) {
        // Retry the message
        if (it->second.retry_count < MAX_RETRY_ATTEMPTS) {
            retry_message(it->second);
            it->second.retry_count++;
            it->second.sent_at = protocol::Timestamp::now();
        } else {
            pending_acks_.erase(it);
        }

        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.nacks_received++;
    }

    (void)reason; // Log reason if needed
}

void MessageRouter::check_ack_timeouts() {
    std::vector<PendingAck> to_retry;

    {
        std::lock_guard<std::mutex> lock(acks_mutex_);

        auto now = protocol::Timestamp::now();
        auto it = pending_acks_.begin();

        while (it != pending_acks_.end()) {
            int64_t elapsed = (now - it->second.sent_at) / 1000;
            if (elapsed > it->second.timeout_ms) {
                if (it->second.retry_count < MAX_RETRY_ATTEMPTS) {
                    to_retry.push_back(it->second);
                    it->second.retry_count++;
                    it->second.sent_at = now;
                    ++it;
                } else {
                    it = pending_acks_.erase(it);

                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.ack_timeouts++;
                }
            } else {
                ++it;
            }
        }
    }

    // Retry messages outside the lock
    for (const auto& pending : to_retry) {
        retry_message(pending);
    }
}

size_t MessageRouter::pending_acks_count() const {
    std::lock_guard<std::mutex> lock(acks_mutex_);
    return pending_acks_.size();
}

SequenceNum MessageRouter::next_sequence() {
    return global_sequence_.fetch_add(1);
}

bool MessageRouter::is_sequence_valid(UserId sender, SequenceNum seq) {
    std::lock_guard<std::mutex> lock(sequence_mutex_);

    auto it = last_sequence_.find(sender);
    if (it == last_sequence_.end()) {
        return true; // First message from this sender
    }

    // Allow some flexibility for out-of-order delivery
    const SequenceNum MAX_GAP = 100;
    if (seq > it->second || it->second - seq < MAX_GAP) {
        return true;
    }

    return false;
}

MessageRouter::Stats MessageRouter::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s = stats_;

    // Calculate reliability rate
    if (s.messages_routed > 0) {
        s.reliability_rate = static_cast<double>(s.messages_delivered) /
                            static_cast<double>(s.messages_routed);
    }

    return s;
}

void MessageRouter::set_rate_limit(UserId user_id, int messages_per_second) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    rate_limits_[user_id] = {messages_per_second, 0, protocol::MonotonicTimestamp::now()};
}

void MessageRouter::clear_rate_limit(UserId user_id) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    rate_limits_.erase(user_id);
}

bool MessageRouter::check_rate_limit(UserId user_id) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);

    auto it = rate_limits_.find(user_id);
    if (it == rate_limits_.end()) {
        return true; // No rate limit
    }

    auto& info = it->second;
    auto now = protocol::MonotonicTimestamp::now();
    int64_t elapsed = now - info.window_start;

    // Reset window every second
    if (elapsed >= 1000000) { // 1 second in microseconds
        info.current_count = 0;
        info.window_start = now;
    }

    if (info.current_count >= info.limit) {
        return false;
    }

    info.current_count++;
    return true;
}

void MessageRouter::processor_loop() {
    while (running_.load()) {
        // Wait for messages or timeout
        queue_event_.wait_for(std::chrono::milliseconds(100));
        queue_event_.reset();

        if (!running_.load()) break;

        // Process queued messages
        process_queue();

        // Check for ACK timeouts
        check_ack_timeouts();
    }
}

ErrorCode MessageRouter::deliver_message(UserId target, const protocol::Message& msg) {
    auto result = connection_manager_.send_to_user(target, msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (result == ErrorCode::SUCCESS) {
            stats_.messages_delivered++;
        } else {
            stats_.messages_dropped++;
        }
    }

    return result;
}

bool MessageRouter::apply_filters(const protocol::Message& msg, UserId sender) {
    std::lock_guard<std::mutex> lock(filters_mutex_);

    for (const auto& filter : filters_) {
        if (!filter(msg, sender)) {
            return false;
        }
    }

    return true;
}

void MessageRouter::invoke_handler(const protocol::Message& msg, UserId sender) {
    std::shared_lock<std::shared_mutex> lock(handlers_mutex_);

    auto it = handlers_.find(msg.type());
    if (it != handlers_.end()) {
        it->second(msg, sender);
    }

    {
        std::lock_guard<std::mutex> lock2(stats_mutex_);
        stats_.messages_routed++;
    }
}

void MessageRouter::update_stats(bool delivered, double delivery_time_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    if (delivered) {
        stats_.messages_delivered++;

        // Update average delivery time (exponential moving average)
        double alpha = 0.1;
        stats_.avg_delivery_time_ms = alpha * delivery_time_ms +
                                      (1.0 - alpha) * stats_.avg_delivery_time_ms;
    }
}

void MessageRouter::retry_message(const PendingAck& pending) {
    deliver_message(pending.target, pending.message);

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_retried++;
}

// DefaultHandlers implementation
void DefaultHandlers::handle_chat_message(
    MessageRouter& router,
    ConnectionManager& connections,
    const protocol::Message& msg,
    UserId sender) {

    // Get sender info
    auto* conn = connections.get_connection(sender);
    if (!conn) return;

    // Create broadcast message
    auto content = protocol::PayloadSerializer::deserialize_chat_message(msg.payload());
    auto broadcast = protocol::Message::create_chat_message(sender, content);
    broadcast.set_timestamp(msg.timestamp());
    broadcast.set_sequence(router.next_sequence());
    broadcast.update_checksum();

    // Broadcast to all users in the same room
    connections.broadcast_to_room(conn->current_room, broadcast, INVALID_USER_ID);
}

void DefaultHandlers::handle_private_message(
    MessageRouter& router,
    ConnectionManager& connections,
    const protocol::Message& msg,
    UserId sender) {

    auto pm = protocol::PayloadSerializer::deserialize_private_message(msg.payload());

    // Create message for recipient
    auto forward = protocol::Message::create_private_message(sender, pm.recipient, pm.content);
    forward.set_timestamp(msg.timestamp());
    forward.set_sequence(router.next_sequence());
    forward.update_checksum();

    // Send to recipient
    router.send_direct(pm.recipient, forward);

    // Echo back to sender for confirmation
    router.send_direct(sender, forward);
}

void DefaultHandlers::handle_broadcast(
    MessageRouter& router,
    ConnectionManager& connections,
    const protocol::Message& msg,
    UserId sender) {

    (void)connections; // Unused
    router.broadcast_direct(msg, sender);
}

void DefaultHandlers::handle_user_list_request(
    MessageRouter& router,
    ConnectionManager& connections,
    const protocol::Message& msg,
    UserId sender) {

    (void)msg; // Unused

    auto users = connections.get_all_users();
    auto response = protocol::Message::create_user_list(users);
    response.set_sequence(router.next_sequence());
    response.update_checksum();

    router.send_direct(sender, response);
}

// SpamFilter implementation
SpamFilter::SpamFilter(int max_messages_per_second)
    : limit_(max_messages_per_second)
{
}

bool SpamFilter::operator()(const protocol::Message& msg, UserId sender) {
    (void)msg; // Unused

    std::lock_guard<std::mutex> lock(mutex_);

    auto now = protocol::MonotonicTimestamp::now();
    auto& times = message_times_[sender];

    // Remove old timestamps (older than 1 second)
    while (!times.empty() && (now - times.front()) > 1000000) {
        times.erase(times.begin());
    }

    if (static_cast<int>(times.size()) >= limit_) {
        return false; // Rate limited
    }

    times.push_back(now);
    return true;
}

// ProfanityFilter implementation
ProfanityFilter::ProfanityFilter(const std::vector<std::string>& blocked_words) {
    for (const auto& word : blocked_words) {
        blocked_words_.insert(word);
    }
}

bool ProfanityFilter::operator()(const protocol::Message& msg, UserId sender) {
    (void)sender; // Unused

    if (msg.type() == protocol::MessageType::CHAT_MESSAGE ||
        msg.type() == protocol::MessageType::PRIVATE_MESSAGE) {
        std::string content = msg.payload_as_string();
        return !contains_blocked_word(content);
    }

    return true;
}

void ProfanityFilter::add_word(const std::string& word) {
    std::lock_guard<std::mutex> lock(mutex_);
    blocked_words_.insert(word);
}

void ProfanityFilter::remove_word(const std::string& word) {
    std::lock_guard<std::mutex> lock(mutex_);
    blocked_words_.erase(word);
}

bool ProfanityFilter::contains_blocked_word(const std::string& text) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Convert to lowercase for case-insensitive matching
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

    for (const auto& word : blocked_words_) {
        std::string lower_word = word;
        std::transform(lower_word.begin(), lower_word.end(), lower_word.begin(), ::tolower);

        if (lower_text.find(lower_word) != std::string::npos) {
            return true;
        }
    }

    return false;
}

} // namespace server
} // namespace chatterbox
