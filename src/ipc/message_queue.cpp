#include <chatterbox/ipc/message_queue.hpp>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace chatterbox {
namespace ipc {

// MessageQueue implementation
MessageQueue::MessageQueue(key_t key, bool create, int permissions)
    : queue_id_(-1)
    , key_(key)
    , is_owner_(create)
{
    int flags = permissions;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }

    queue_id_ = msgget(key, flags);

    if (queue_id_ < 0 && create && errno == EEXIST) {
        // Queue already exists, try to remove and recreate
        int existing_id = msgget(key, 0);
        if (existing_id >= 0) {
            msgctl(existing_id, IPC_RMID, nullptr);
        }
        queue_id_ = msgget(key, permissions | IPC_CREAT);
    }

    if (queue_id_ < 0 && !create) {
        // Try to open existing queue
        queue_id_ = msgget(key, 0);
    }

    if (queue_id_ < 0) {
        throw std::runtime_error("Failed to create/open message queue: " +
                                std::string(strerror(errno)));
    }
}

MessageQueue::MessageQueue(MessageQueue&& other) noexcept
    : queue_id_(other.queue_id_)
    , key_(other.key_)
    , is_owner_(other.is_owner_)
{
    other.queue_id_ = -1;
    other.is_owner_ = false;
}

MessageQueue& MessageQueue::operator=(MessageQueue&& other) noexcept {
    if (this != &other) {
        cleanup();
        queue_id_ = other.queue_id_;
        key_ = other.key_;
        is_owner_ = other.is_owner_;
        other.queue_id_ = -1;
        other.is_owner_ = false;
    }
    return *this;
}

MessageQueue::~MessageQueue() {
    cleanup();
}

void MessageQueue::cleanup() {
    if (is_owner_ && queue_id_ >= 0) {
        msgctl(queue_id_, IPC_RMID, nullptr);
    }
    queue_id_ = -1;
}

ErrorCode MessageQueue::send(long type, const void* data, size_t size, bool blocking) {
    if (queue_id_ < 0) {
        return ErrorCode::ERR_IPC_SEND;
    }

    if (size > MAX_MESSAGE_SIZE) {
        return ErrorCode::ERR_INVALID_MESSAGE;
    }

    MessageBuffer buffer;
    buffer.mtype = type > 0 ? type : 1;
    buffer.length = size;
    std::memcpy(buffer.mtext, data, size);

    int flags = blocking ? 0 : IPC_NOWAIT;

    std::lock_guard<std::mutex> lock(mutex_);

    if (msgsnd(queue_id_, &buffer, sizeof(buffer.mtext) + sizeof(buffer.length), flags) < 0) {
        if (errno == EAGAIN) {
            return ErrorCode::ERR_TIMEOUT;
        }
        return ErrorCode::ERR_IPC_SEND;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode MessageQueue::receive(long type, void* buffer, size_t& size, bool blocking) {
    if (queue_id_ < 0) {
        return ErrorCode::ERR_IPC_RECV;
    }

    MessageBuffer msg_buffer;
    int flags = blocking ? 0 : IPC_NOWAIT;

    std::lock_guard<std::mutex> lock(mutex_);

    ssize_t received = msgrcv(queue_id_, &msg_buffer, sizeof(msg_buffer.mtext) + sizeof(msg_buffer.length),
                              type, flags);

    if (received < 0) {
        if (errno == ENOMSG || errno == EAGAIN) {
            return ErrorCode::ERR_TIMEOUT;
        }
        return ErrorCode::ERR_IPC_RECV;
    }

    size = std::min(msg_buffer.length, size);
    std::memcpy(buffer, msg_buffer.mtext, size);

    return ErrorCode::SUCCESS;
}

std::optional<std::vector<uint8_t>> MessageQueue::try_receive(long type) {
    std::vector<uint8_t> buffer(MAX_MESSAGE_SIZE);
    size_t size = buffer.size();

    if (receive(type, buffer.data(), size, false) == ErrorCode::SUCCESS) {
        buffer.resize(size);
        return buffer;
    }

    return std::nullopt;
}

bool MessageQueue::has_messages(long type) const {
    struct msqid_ds stats;
    if (msgctl(queue_id_, IPC_STAT, &stats) < 0) {
        return false;
    }
    return stats.msg_qnum > 0;
}

MessageQueue::Stats MessageQueue::get_stats() const {
    Stats stats = {};
    struct msqid_ds ds;

    if (msgctl(queue_id_, IPC_STAT, &ds) == 0) {
        stats.message_count = ds.msg_qnum;
        stats.total_bytes = ds.msg_cbytes;
        stats.last_send_pid = ds.msg_lspid;
        stats.last_recv_pid = ds.msg_lrpid;
    }

    return stats;
}

void MessageQueue::remove(key_t key) {
    int queue_id = msgget(key, 0);
    if (queue_id >= 0) {
        msgctl(queue_id, IPC_RMID, nullptr);
    }
}

// MessageQueuePool implementation
MessageQueuePool::MessageQueuePool(key_t base_key, size_t pool_size)
    : base_key_(base_key)
    , pool_size_(pool_size)
{
    queues_.resize(pool_size);
}

MessageQueuePool::~MessageQueuePool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    queues_.clear();
    user_to_queue_.clear();
}

MessageQueue* MessageQueuePool::get_queue(UserId user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    auto it = user_to_queue_.find(user_id);
    if (it == user_to_queue_.end()) {
        return nullptr;
    }

    return queues_[it->second].get();
}

MessageQueue* MessageQueuePool::create_queue(UserId user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    // Check if user already has a queue
    auto it = user_to_queue_.find(user_id);
    if (it != user_to_queue_.end()) {
        return queues_[it->second].get();
    }

    // Find an available slot
    size_t index = next_queue_index_.fetch_add(1) % pool_size_;
    for (size_t i = 0; i < pool_size_; ++i) {
        size_t slot = (index + i) % pool_size_;
        if (!queues_[slot]) {
            key_t queue_key = base_key_ + static_cast<key_t>(slot) + 100;
            try {
                queues_[slot] = std::make_unique<MessageQueue>(queue_key, true);
                user_to_queue_[user_id] = slot;
                return queues_[slot].get();
            } catch (...) {
                continue;
            }
        }
    }

    return nullptr;
}

void MessageQueuePool::remove_queue(UserId user_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    auto it = user_to_queue_.find(user_id);
    if (it != user_to_queue_.end()) {
        queues_[it->second].reset();
        user_to_queue_.erase(it);
    }
}

void MessageQueuePool::broadcast(const void* data, size_t size, UserId exclude) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    for (const auto& [user_id, queue_index] : user_to_queue_) {
        if (user_id != exclude && queues_[queue_index]) {
            queues_[queue_index]->send(1, data, size, false);
        }
    }
}

size_t MessageQueuePool::active_count() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return user_to_queue_.size();
}

} // namespace ipc
} // namespace chatterbox
