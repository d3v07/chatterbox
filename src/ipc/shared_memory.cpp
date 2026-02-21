#include <chatterbox/ipc/shared_memory.hpp>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <chrono>

namespace chatterbox {
namespace ipc {

// SharedMemory implementation
SharedMemory::SharedMemory(key_t key, size_t size, bool create, int permissions)
    : shm_id_(-1)
    , ptr_(nullptr)
    , size_(size)
    , key_(key)
    , is_owner_(create)
{
    int flags = permissions;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }

    shm_id_ = shmget(key, size, flags);

    if (shm_id_ < 0 && create && errno == EEXIST) {
        // Segment already exists, try to remove and recreate
        int existing_id = shmget(key, 0, 0);
        if (existing_id >= 0) {
            shmctl(existing_id, IPC_RMID, nullptr);
        }
        shm_id_ = shmget(key, size, permissions | IPC_CREAT);
    }

    if (shm_id_ < 0 && !create) {
        // Try to open existing segment
        shm_id_ = shmget(key, 0, 0);
    }

    if (shm_id_ < 0) {
        throw std::runtime_error("Failed to create/open shared memory: " +
                                std::string(strerror(errno)));
    }

    // Attach to the segment
    ptr_ = shmat(shm_id_, nullptr, 0);
    if (ptr_ == (void*)-1) {
        ptr_ = nullptr;
        if (is_owner_) {
            shmctl(shm_id_, IPC_RMID, nullptr);
        }
        throw std::runtime_error("Failed to attach shared memory: " +
                                std::string(strerror(errno)));
    }

    // Initialize memory if we created it
    if (create) {
        std::memset(ptr_, 0, size);
    }
}

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : shm_id_(other.shm_id_)
    , ptr_(other.ptr_)
    , size_(other.size_)
    , key_(other.key_)
    , is_owner_(other.is_owner_)
{
    other.shm_id_ = -1;
    other.ptr_ = nullptr;
    other.is_owner_ = false;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
    if (this != &other) {
        cleanup();
        shm_id_ = other.shm_id_;
        ptr_ = other.ptr_;
        size_ = other.size_;
        key_ = other.key_;
        is_owner_ = other.is_owner_;
        other.shm_id_ = -1;
        other.ptr_ = nullptr;
        other.is_owner_ = false;
    }
    return *this;
}

SharedMemory::~SharedMemory() {
    cleanup();
}

void SharedMemory::cleanup() {
    if (ptr_ != nullptr && ptr_ != (void*)-1) {
        shmdt(ptr_);
        ptr_ = nullptr;
    }

    if (is_owner_ && shm_id_ >= 0) {
        shmctl(shm_id_, IPC_RMID, nullptr);
    }

    shm_id_ = -1;
}

void SharedMemory::detach() {
    if (ptr_ != nullptr && ptr_ != (void*)-1) {
        shmdt(ptr_);
        ptr_ = nullptr;
    }
}

void SharedMemory::remove(key_t key) {
    int shm_id = shmget(key, 0, 0);
    if (shm_id >= 0) {
        shmctl(shm_id, IPC_RMID, nullptr);
    }
}

SharedMemory::Info SharedMemory::get_info() const {
    Info info = {};
    struct shmid_ds ds;

    if (shmctl(shm_id_, IPC_STAT, &ds) == 0) {
        info.size = ds.shm_segsz;
        info.creator_pid = ds.shm_cpid;
        info.num_attaches = ds.shm_nattch;
    }

    return info;
}

// ChatSharedMemory implementation
ChatSharedMemory::ChatSharedMemory(key_t key, bool create)
    : shm_(key, sizeof(SharedMemoryLayout), create)
    , layout_(nullptr)
{
    if (shm_.is_valid()) {
        layout_ = shm_.as<SharedMemoryLayout>();
        if (create) {
            initialize();
        }
    }
}

ChatSharedMemory::~ChatSharedMemory() {
    // Cleanup is handled by SharedMemory destructor
}

void ChatSharedMemory::initialize() {
    if (!layout_) return;

    // Initialize server state
    layout_->state.active_users.store(0);
    layout_->state.total_messages.store(0);
    layout_->state.server_start_time.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    layout_->state.server_running.store(true);
    layout_->state.user_bitmap.store(0);
    layout_->state.global_sequence.store(0);
    layout_->state.messages_sent.store(0);
    layout_->state.messages_dropped.store(0);
    layout_->state.bytes_transferred.store(0);

    // Initialize user table
    layout_->users.count.store(0);
    for (size_t i = 0; i < MAX_USERS; ++i) {
        layout_->users.users[i].clear();
    }
}

SharedUserInfo* ChatSharedMemory::register_user(const std::string& username, key_t client_queue_key) {
    if (!layout_) return nullptr;

    // Check if username is taken
    if (layout_->users.find_by_name(username.c_str()) != nullptr) {
        return nullptr;
    }

    // Allocate a slot
    SharedUserInfo* user = layout_->users.allocate_slot();
    if (!user) return nullptr;

    // Assign user ID
    static std::atomic<UserId> next_id{SERVER_USER_ID + 1};
    user->user_id = next_id.fetch_add(1);

    // Set user info
    std::strncpy(user->username, username.c_str(), MAX_USERNAME_LENGTH - 1);
    user->username[MAX_USERNAME_LENGTH - 1] = '\0';
    user->current_room = LOBBY_ROOM_ID;
    user->last_activity = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    user->client_queue_key = client_queue_key;
    user->messages_sent = 0;
    user->messages_received = 0;

    // Update user bitmap
    for (size_t i = 0; i < MAX_USERS; ++i) {
        if (&layout_->users.users[i] == user) {
            uint64_t mask = 1ULL << i;
            layout_->state.user_bitmap.fetch_or(mask);
            break;
        }
    }

    layout_->state.active_users.fetch_add(1);

    return user;
}

void ChatSharedMemory::unregister_user(UserId user_id) {
    if (!layout_) return;

    for (size_t i = 0; i < MAX_USERS; ++i) {
        if (layout_->users.users[i].user_id == user_id) {
            layout_->users.users[i].clear();

            // Clear user bitmap
            uint64_t mask = ~(1ULL << i);
            layout_->state.user_bitmap.fetch_and(mask);

            layout_->state.active_users.fetch_sub(1);
            layout_->users.count.fetch_sub(1);
            break;
        }
    }
}

SequenceNum ChatSharedMemory::next_sequence() {
    if (!layout_) return 0;
    return layout_->state.global_sequence.fetch_add(1);
}

void ChatSharedMemory::touch_user(UserId user_id) {
    if (!layout_) return;

    SharedUserInfo* user = layout_->users.find_user(user_id);
    if (user) {
        user->last_activity = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
}

uint32_t ChatSharedMemory::active_user_count() const {
    if (!layout_) return 0;
    return layout_->state.active_users.load();
}

} // namespace ipc
} // namespace chatterbox
