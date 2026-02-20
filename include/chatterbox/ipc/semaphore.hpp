#ifndef CHATTERBOX_IPC_SEMAPHORE_HPP
#define CHATTERBOX_IPC_SEMAPHORE_HPP

#include <chatterbox/common.hpp>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <mutex>
#include <atomic>

namespace chatterbox {
namespace ipc {

// Union for semctl operations
#if defined(__linux__)
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
    struct seminfo* __buf;
};
#elif defined(__APPLE__)
// macOS defines this in sys/sem.h but we need to declare it
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};
#endif

// RAII wrapper for System V semaphore
class Semaphore {
public:
    // Create or open a semaphore set
    explicit Semaphore(key_t key, int num_sems = 1, bool create = false, int permissions = 0666);

    // Move semantics
    Semaphore(Semaphore&& other) noexcept;
    Semaphore& operator=(Semaphore&& other) noexcept;

    // Non-copyable
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    // Destructor
    ~Semaphore();

    // Wait (decrement/P operation)
    ErrorCode wait(int sem_num = 0, bool blocking = true);

    // Signal (increment/V operation)
    ErrorCode signal(int sem_num = 0);

    // Try wait (non-blocking)
    bool try_wait(int sem_num = 0);

    // Get current value
    int get_value(int sem_num = 0) const;

    // Set value
    void set_value(int sem_num, int value);

    // Check validity
    bool is_valid() const { return sem_id_ >= 0; }

    // Mark as owner
    void set_owner(bool owner) { is_owner_ = owner; }

    // Get semaphore ID
    int get_id() const { return sem_id_; }

    // Remove semaphore
    static void remove(key_t key);

private:
    int sem_id_;
    int num_sems_;
    key_t key_;
    bool is_owner_;

    void cleanup();
};

// RAII lock guard for semaphore
class SemaphoreGuard {
public:
    explicit SemaphoreGuard(Semaphore& sem, int sem_num = 0);
    ~SemaphoreGuard();

    // Non-copyable, non-movable
    SemaphoreGuard(const SemaphoreGuard&) = delete;
    SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;
    SemaphoreGuard(SemaphoreGuard&&) = delete;
    SemaphoreGuard& operator=(SemaphoreGuard&&) = delete;

    // Release early
    void release();

    // Check if locked
    bool owns_lock() const { return locked_; }

private:
    Semaphore& sem_;
    int sem_num_;
    bool locked_;
};

// Read-Write semaphore for shared memory access
class RWLock {
public:
    explicit RWLock(key_t key, bool create = false);
    ~RWLock();

    // Acquire read lock (shared)
    void read_lock();
    void read_unlock();
    bool try_read_lock();

    // Acquire write lock (exclusive)
    void write_lock();
    void write_unlock();
    bool try_write_lock();

    // Check validity
    bool is_valid() const { return sem_.is_valid(); }

private:
    Semaphore sem_;
    // Semaphore 0: reader count
    // Semaphore 1: writer lock
    // Semaphore 2: mutex for reader count
    static constexpr int SEM_READERS = 0;
    static constexpr int SEM_WRITER = 1;
    static constexpr int SEM_MUTEX = 2;
};

// RAII read lock guard
class ReadLockGuard {
public:
    explicit ReadLockGuard(RWLock& lock);
    ~ReadLockGuard();

    ReadLockGuard(const ReadLockGuard&) = delete;
    ReadLockGuard& operator=(const ReadLockGuard&) = delete;

private:
    RWLock& lock_;
};

// RAII write lock guard
class WriteLockGuard {
public:
    explicit WriteLockGuard(RWLock& lock);
    ~WriteLockGuard();

    WriteLockGuard(const WriteLockGuard&) = delete;
    WriteLockGuard& operator=(const WriteLockGuard&) = delete;

private:
    RWLock& lock_;
};

// Named semaphore for cross-process synchronization
class NamedSemaphore {
public:
    explicit NamedSemaphore(const std::string& name, bool create = false, unsigned int initial_value = 1);
    ~NamedSemaphore();

    void wait();
    void signal();
    bool try_wait();

    bool is_valid() const { return valid_; }

    static void unlink(const std::string& name);

private:
    std::string name_;
    void* sem_; // sem_t*
    bool is_owner_;
    bool valid_;
};

} // namespace ipc
} // namespace chatterbox

#endif // CHATTERBOX_IPC_SEMAPHORE_HPP
