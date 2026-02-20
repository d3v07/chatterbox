#ifndef CHATTERBOX_IPC_SHARED_MEMORY_HPP
#define CHATTERBOX_IPC_SHARED_MEMORY_HPP

#include <chatterbox/common.hpp>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <atomic>
#include <mutex>
#include <cstring>

namespace chatterbox {
namespace ipc {

// Shared state structure for the chat server
struct SharedServerState {
    std::atomic<uint32_t> active_users;
    std::atomic<uint64_t> total_messages;
    std::atomic<uint64_t> server_start_time;
    std::atomic<bool> server_running;

    // User presence bitmap (64 users max)
    std::atomic<uint64_t> user_bitmap;

    // Latest sequence number for ordering
    std::atomic<SequenceNum> global_sequence;

    // Statistics
    std::atomic<uint64_t> messages_sent;
    std::atomic<uint64_t> messages_dropped;
    std::atomic<uint64_t> bytes_transferred;

    // Reserved for future use
    char reserved[256];
};

// User info stored in shared memory
struct SharedUserInfo {
    UserId user_id;
    char username[MAX_USERNAME_LENGTH];
    RoomId current_room;
    uint64_t last_activity;
    std::atomic<bool> is_active;
    key_t client_queue_key;

    // Statistics per user
    uint64_t messages_sent;
    uint64_t messages_received;

    void clear() {
        user_id = INVALID_USER_ID;
        std::memset(username, 0, sizeof(username));
        current_room = LOBBY_ROOM_ID;
        last_activity = 0;
        is_active.store(false);
        client_queue_key = 0;
        messages_sent = 0;
        messages_received = 0;
    }
};

// Shared user table
struct SharedUserTable {
    std::atomic<uint32_t> count;
    SharedUserInfo users[MAX_USERS];

    SharedUserInfo* find_user(UserId id) {
        for (size_t i = 0; i < MAX_USERS; ++i) {
            if (users[i].user_id == id && users[i].is_active.load()) {
                return &users[i];
            }
        }
        return nullptr;
    }

    SharedUserInfo* find_by_name(const char* name) {
        for (size_t i = 0; i < MAX_USERS; ++i) {
            if (users[i].is_active.load() &&
                std::strncmp(users[i].username, name, MAX_USERNAME_LENGTH) == 0) {
                return &users[i];
            }
        }
        return nullptr;
    }

    SharedUserInfo* allocate_slot() {
        for (size_t i = 0; i < MAX_USERS; ++i) {
            bool expected = false;
            if (users[i].is_active.compare_exchange_strong(expected, true)) {
                count.fetch_add(1);
                return &users[i];
            }
        }
        return nullptr;
    }

    void release_slot(UserId id) {
        for (size_t i = 0; i < MAX_USERS; ++i) {
            if (users[i].user_id == id) {
                users[i].clear();
                count.fetch_sub(1);
                return;
            }
        }
    }
};

// Combined shared memory layout
struct SharedMemoryLayout {
    SharedServerState state;
    SharedUserTable users;
};

// RAII wrapper for System V shared memory
class SharedMemory {
public:
    // Create or attach to shared memory
    explicit SharedMemory(key_t key, size_t size, bool create = false, int permissions = 0666);

    // Move semantics
    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;

    // Non-copyable
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    // Destructor
    ~SharedMemory();

    // Get pointer to shared memory
    void* get() { return ptr_; }
    const void* get() const { return ptr_; }

    // Typed access
    template<typename T>
    T* as() { return static_cast<T*>(ptr_); }

    template<typename T>
    const T* as() const { return static_cast<const T*>(ptr_); }

    // Get size
    size_t size() const { return size_; }

    // Check validity
    bool is_valid() const { return ptr_ != nullptr && ptr_ != (void*)-1; }

    // Mark as owner
    void set_owner(bool owner) { is_owner_ = owner; }

    // Detach from shared memory
    void detach();

    // Remove shared memory segment
    static void remove(key_t key);

    // Get segment info
    struct Info {
        size_t size;
        pid_t creator_pid;
        size_t num_attaches;
    };
    Info get_info() const;

private:
    int shm_id_;
    void* ptr_;
    size_t size_;
    key_t key_;
    bool is_owner_;

    void cleanup();
};

// Specialized shared memory manager for ChatterBox
class ChatSharedMemory {
public:
    explicit ChatSharedMemory(key_t key, bool create = false);
    ~ChatSharedMemory();

    // Access server state
    SharedServerState* state() { return &layout_->state; }
    const SharedServerState* state() const { return &layout_->state; }

    // Access user table
    SharedUserTable* users() { return &layout_->users; }
    const SharedUserTable* users() const { return &layout_->users; }

    // Initialize (call after creation)
    void initialize();

    // Check if valid
    bool is_valid() const { return shm_.is_valid(); }

    // Register a new user
    SharedUserInfo* register_user(const std::string& username, key_t client_queue_key);

    // Unregister a user
    void unregister_user(UserId user_id);

    // Get next sequence number
    SequenceNum next_sequence();

    // Update user activity
    void touch_user(UserId user_id);

    // Get active user count
    uint32_t active_user_count() const;

private:
    SharedMemory shm_;
    SharedMemoryLayout* layout_;
};

} // namespace ipc
} // namespace chatterbox

#endif // CHATTERBOX_IPC_SHARED_MEMORY_HPP
