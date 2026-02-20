#ifndef CHATTERBOX_IPC_MESSAGE_QUEUE_HPP
#define CHATTERBOX_IPC_MESSAGE_QUEUE_HPP

#include <chatterbox/common.hpp>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <vector>
#include <optional>
#include <mutex>
#include <atomic>

namespace chatterbox {
namespace ipc {

// Message buffer structure for System V message queues
struct MessageBuffer {
    long mtype;                         // Message type (used for routing)
    char mtext[MAX_MESSAGE_SIZE];       // Message data
    size_t length;                      // Actual message length
};

// RAII wrapper for System V message queue
class MessageQueue {
public:
    // Create or open a message queue
    explicit MessageQueue(key_t key, bool create = false, int permissions = 0666);

    // Move semantics
    MessageQueue(MessageQueue&& other) noexcept;
    MessageQueue& operator=(MessageQueue&& other) noexcept;

    // Non-copyable
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    // Destructor - cleans up if owner
    ~MessageQueue();

    // Send a message with specified type
    ErrorCode send(long type, const void* data, size_t size, bool blocking = true);

    // Receive a message of specified type (0 = any type)
    ErrorCode receive(long type, void* buffer, size_t& size, bool blocking = true);

    // Non-blocking receive with optional result
    std::optional<std::vector<uint8_t>> try_receive(long type);

    // Check if messages are available
    bool has_messages(long type = 0) const;

    // Get queue statistics
    struct Stats {
        size_t message_count;
        size_t total_bytes;
        pid_t last_send_pid;
        pid_t last_recv_pid;
    };
    Stats get_stats() const;

    // Get the queue ID
    int get_id() const { return queue_id_; }

    // Check if valid
    bool is_valid() const { return queue_id_ >= 0; }

    // Mark as owner (will delete on destruction)
    void set_owner(bool owner) { is_owner_ = owner; }

    // Remove the queue (static version for cleanup)
    static void remove(key_t key);

private:
    int queue_id_;
    key_t key_;
    bool is_owner_;
    mutable std::mutex mutex_;

    void cleanup();
};

// Message queue pool for handling multiple clients
class MessageQueuePool {
public:
    explicit MessageQueuePool(key_t base_key, size_t pool_size);
    ~MessageQueuePool();

    // Get a queue for a specific client
    MessageQueue* get_queue(UserId user_id);

    // Create a new queue for a user
    MessageQueue* create_queue(UserId user_id);

    // Remove a user's queue
    void remove_queue(UserId user_id);

    // Broadcast to all queues
    void broadcast(const void* data, size_t size, UserId exclude = INVALID_USER_ID);

    // Get number of active queues
    size_t active_count() const;

private:
    key_t base_key_;
    size_t pool_size_;
    std::vector<std::unique_ptr<MessageQueue>> queues_;
    std::unordered_map<UserId, size_t> user_to_queue_;
    mutable std::mutex pool_mutex_;
    std::atomic<size_t> next_queue_index_{0};
};

} // namespace ipc
} // namespace chatterbox

#endif // CHATTERBOX_IPC_MESSAGE_QUEUE_HPP
