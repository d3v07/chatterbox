#ifndef CHATTERBOX_SERVER_MESSAGE_ROUTER_HPP
#define CHATTERBOX_SERVER_MESSAGE_ROUTER_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <chatterbox/sync/condition_var.hpp>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <memory>
#include <thread>

namespace chatterbox {
namespace server {

// Forward declaration
class ConnectionManager;

// Message handler function type
using MessageHandler = std::function<void(const protocol::Message&, UserId sender)>;

// Message filter function type (returns true to allow, false to block)
using MessageFilter = std::function<bool(const protocol::Message&, UserId sender)>;

// Pending acknowledgment tracking
struct PendingAck {
    protocol::Message message;
    protocol::Timestamp sent_at;
    UserId target;
    int retry_count;
    int64_t timeout_ms;
};

// Message queue entry with priority
struct QueuedMessage {
    protocol::Message message;
    UserId sender;
    UserId target; // BROADCAST_USER_ID for broadcast
    protocol::Timestamp queued_at;
    int priority;

    bool operator<(const QueuedMessage& other) const {
        // Higher priority first, then earlier time
        if (priority != other.priority) {
            return priority < other.priority;
        }
        return queued_at > other.queued_at;
    }
};

// Routes messages between clients with reliability guarantees
class MessageRouter {
public:
    explicit MessageRouter(ConnectionManager& connection_manager);
    ~MessageRouter();

    // Non-copyable
    MessageRouter(const MessageRouter&) = delete;
    MessageRouter& operator=(const MessageRouter&) = delete;

    // Start/stop the router
    void start();
    void stop();
    bool is_running() const { return running_.load(); }

    // Register message handlers
    void register_handler(protocol::MessageType type, MessageHandler handler);
    void unregister_handler(protocol::MessageType type);

    // Register message filters (applied before handlers)
    void add_filter(MessageFilter filter);
    void clear_filters();

    // Route a message (main entry point)
    ErrorCode route(const protocol::Message& msg, UserId sender);

    // Direct send operations (bypassing queue)
    ErrorCode send_direct(UserId target, const protocol::Message& msg);
    ErrorCode broadcast_direct(const protocol::Message& msg, UserId exclude = INVALID_USER_ID);

    // Queue operations
    void enqueue(const protocol::Message& msg, UserId sender, UserId target, int priority = 0);
    void process_queue();
    size_t queue_size() const;

    // Acknowledgment handling
    void require_ack(const protocol::Message& msg, UserId target, int64_t timeout_ms = 5000);
    void handle_ack(SequenceNum sequence, UserId sender);
    void handle_nack(SequenceNum sequence, UserId sender, const std::string& reason);
    void check_ack_timeouts();
    size_t pending_acks_count() const;

    // Message ordering
    SequenceNum next_sequence();
    bool is_sequence_valid(UserId sender, SequenceNum seq);

    // Statistics
    struct Stats {
        uint64_t messages_routed;
        uint64_t messages_delivered;
        uint64_t messages_dropped;
        uint64_t messages_retried;
        uint64_t acks_received;
        uint64_t nacks_received;
        uint64_t ack_timeouts;
        double avg_delivery_time_ms;
        double reliability_rate; // delivered / routed
    };
    Stats get_stats() const;

    // Rate limiting
    void set_rate_limit(UserId user_id, int messages_per_second);
    void clear_rate_limit(UserId user_id);
    bool check_rate_limit(UserId user_id);

private:
    ConnectionManager& connection_manager_;
    std::atomic<bool> running_{false};

    // Message handlers
    mutable std::shared_mutex handlers_mutex_;
    std::unordered_map<protocol::MessageType, MessageHandler> handlers_;

    // Message filters
    mutable std::mutex filters_mutex_;
    std::vector<MessageFilter> filters_;

    // Message queue
    mutable std::mutex queue_mutex_;
    std::priority_queue<QueuedMessage> message_queue_;
    sync::Event queue_event_;

    // Pending acknowledgments
    mutable std::mutex acks_mutex_;
    std::unordered_map<SequenceNum, PendingAck> pending_acks_;

    // Sequence tracking per user
    mutable std::mutex sequence_mutex_;
    std::unordered_map<UserId, SequenceNum> last_sequence_;
    std::atomic<SequenceNum> global_sequence_{0};

    // Rate limiting
    mutable std::mutex rate_limit_mutex_;
    struct RateLimitInfo {
        int limit;
        int current_count;
        protocol::MonotonicTimestamp window_start;
    };
    std::unordered_map<UserId, RateLimitInfo> rate_limits_;

    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_{};

    // Processing thread
    std::thread processor_thread_;
    void processor_loop();

    // Internal routing
    ErrorCode deliver_message(UserId target, const protocol::Message& msg);
    bool apply_filters(const protocol::Message& msg, UserId sender);
    void invoke_handler(const protocol::Message& msg, UserId sender);
    void update_stats(bool delivered, double delivery_time_ms = 0.0);
    void retry_message(const PendingAck& pending);
};

// Default message handlers
class DefaultHandlers {
public:
    static void handle_chat_message(
        MessageRouter& router,
        ConnectionManager& connections,
        const protocol::Message& msg,
        UserId sender
    );

    static void handle_private_message(
        MessageRouter& router,
        ConnectionManager& connections,
        const protocol::Message& msg,
        UserId sender
    );

    static void handle_broadcast(
        MessageRouter& router,
        ConnectionManager& connections,
        const protocol::Message& msg,
        UserId sender
    );

    static void handle_user_list_request(
        MessageRouter& router,
        ConnectionManager& connections,
        const protocol::Message& msg,
        UserId sender
    );
};

// Message filter implementations
class SpamFilter : public std::enable_shared_from_this<SpamFilter> {
public:
    explicit SpamFilter(int max_messages_per_second = 10);

    bool operator()(const protocol::Message& msg, UserId sender);

    void set_limit(int limit) { limit_ = limit; }

private:
    int limit_;
    mutable std::mutex mutex_;
    std::unordered_map<UserId, std::vector<protocol::MonotonicTimestamp>> message_times_;
};

class ProfanityFilter {
public:
    explicit ProfanityFilter(const std::vector<std::string>& blocked_words = {});

    bool operator()(const protocol::Message& msg, UserId sender);

    void add_word(const std::string& word);
    void remove_word(const std::string& word);

private:
    std::unordered_set<std::string> blocked_words_;
    mutable std::mutex mutex_;

    bool contains_blocked_word(const std::string& text) const;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_MESSAGE_ROUTER_HPP
