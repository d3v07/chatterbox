#ifndef CHATTERBOX_SYNC_MUTEX_GUARD_HPP
#define CHATTERBOX_SYNC_MUTEX_GUARD_HPP

#include <chatterbox/common.hpp>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

// Platform-specific CPU pause instruction
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define CPU_PAUSE() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
    #define CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
#else
    #define CPU_PAUSE() std::this_thread::yield()
#endif

namespace chatterbox {
namespace sync {

// Enhanced mutex with debugging and statistics
class TrackedMutex {
public:
    TrackedMutex() = default;
    ~TrackedMutex() = default;

    TrackedMutex(const TrackedMutex&) = delete;
    TrackedMutex& operator=(const TrackedMutex&) = delete;

    void lock();
    bool try_lock();
    void unlock();

    // Statistics
    uint64_t lock_count() const { return lock_count_.load(); }
    uint64_t contention_count() const { return contention_count_.load(); }
    uint64_t total_wait_time_ns() const { return total_wait_time_ns_.load(); }

private:
    std::mutex mutex_;
    std::atomic<uint64_t> lock_count_{0};
    std::atomic<uint64_t> contention_count_{0};
    std::atomic<uint64_t> total_wait_time_ns_{0};
};

// Read-write mutex with statistics
class TrackedSharedMutex {
public:
    TrackedSharedMutex() = default;
    ~TrackedSharedMutex() = default;

    TrackedSharedMutex(const TrackedSharedMutex&) = delete;
    TrackedSharedMutex& operator=(const TrackedSharedMutex&) = delete;

    // Exclusive lock
    void lock();
    bool try_lock();
    void unlock();

    // Shared lock
    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();

    // Statistics
    uint64_t write_lock_count() const { return write_lock_count_.load(); }
    uint64_t read_lock_count() const { return read_lock_count_.load(); }

private:
    std::shared_mutex mutex_;
    std::atomic<uint64_t> write_lock_count_{0};
    std::atomic<uint64_t> read_lock_count_{0};
};

// Scoped lock with timeout support
template<typename Mutex>
class TimedLockGuard {
public:
    explicit TimedLockGuard(Mutex& mutex, std::chrono::milliseconds timeout)
        : mutex_(mutex), locked_(false) {
        auto start = std::chrono::steady_clock::now();
        while (!locked_) {
            locked_ = mutex_.try_lock();
            if (locked_) break;

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                throw std::runtime_error("Lock acquisition timeout");
            }
            std::this_thread::yield();
        }
    }

    ~TimedLockGuard() {
        if (locked_) {
            mutex_.unlock();
        }
    }

    TimedLockGuard(const TimedLockGuard&) = delete;
    TimedLockGuard& operator=(const TimedLockGuard&) = delete;

    bool owns_lock() const { return locked_; }

private:
    Mutex& mutex_;
    bool locked_;
};

// Spin lock for short critical sections
class SpinLock {
public:
    SpinLock() = default;
    ~SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Spin with exponential backoff
            for (int i = 0; i < spin_count_; ++i) {
                CPU_PAUSE(); // CPU pause instruction
            }
            spin_count_ = std::min(spin_count_ * 2, 1024);
        }
        spin_count_ = 1;
    }

    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    int spin_count_ = 1;
};

// Scoped spin lock guard
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
        lock_.lock();
    }

    ~SpinLockGuard() {
        lock_.unlock();
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

private:
    SpinLock& lock_;
};

// Recursive spin lock
class RecursiveSpinLock {
public:
    RecursiveSpinLock() = default;

    void lock() {
        auto tid = std::this_thread::get_id();
        if (owner_ == tid) {
            ++recursion_count_;
            return;
        }

        while (flag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        owner_ = tid;
        recursion_count_ = 1;
    }

    bool try_lock() {
        auto tid = std::this_thread::get_id();
        if (owner_ == tid) {
            ++recursion_count_;
            return true;
        }

        if (!flag_.test_and_set(std::memory_order_acquire)) {
            owner_ = tid;
            recursion_count_ = 1;
            return true;
        }
        return false;
    }

    void unlock() {
        if (--recursion_count_ == 0) {
            owner_ = std::thread::id();
            flag_.clear(std::memory_order_release);
        }
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    std::thread::id owner_;
    int recursion_count_ = 0;
};

// Lock-free data structures support

// Double-checked locking helper
template<typename T, typename Mutex = std::mutex>
class LazyInit {
public:
    template<typename Factory>
    T& get(Factory&& factory) {
        if (!initialized_.load(std::memory_order_acquire)) {
            std::lock_guard<Mutex> lock(mutex_);
            if (!initialized_.load(std::memory_order_relaxed)) {
                value_ = factory();
                initialized_.store(true, std::memory_order_release);
            }
        }
        return value_;
    }

    bool is_initialized() const {
        return initialized_.load(std::memory_order_acquire);
    }

    void reset() {
        std::lock_guard<Mutex> lock(mutex_);
        initialized_.store(false, std::memory_order_release);
        value_ = T();
    }

private:
    T value_;
    std::atomic<bool> initialized_{false};
    Mutex mutex_;
};

// Mutex hierarchy to prevent deadlocks
class HierarchicalMutex {
public:
    explicit HierarchicalMutex(unsigned long hierarchy_value)
        : hierarchy_value_(hierarchy_value), previous_hierarchy_value_(0) {}

    void lock() {
        check_for_hierarchy_violation();
        internal_mutex_.lock();
        update_hierarchy_value();
    }

    void unlock() {
        if (this_thread_hierarchy_value_ != hierarchy_value_) {
            throw std::logic_error("Mutex hierarchy violated on unlock");
        }
        this_thread_hierarchy_value_ = previous_hierarchy_value_;
        internal_mutex_.unlock();
    }

    bool try_lock() {
        check_for_hierarchy_violation();
        if (!internal_mutex_.try_lock()) {
            return false;
        }
        update_hierarchy_value();
        return true;
    }

private:
    std::mutex internal_mutex_;
    unsigned long const hierarchy_value_;
    unsigned long previous_hierarchy_value_;
    static thread_local unsigned long this_thread_hierarchy_value_;

    void check_for_hierarchy_violation() {
        if (this_thread_hierarchy_value_ <= hierarchy_value_) {
            throw std::logic_error("Mutex hierarchy violated");
        }
    }

    void update_hierarchy_value() {
        previous_hierarchy_value_ = this_thread_hierarchy_value_;
        this_thread_hierarchy_value_ = hierarchy_value_;
    }
};

} // namespace sync
} // namespace chatterbox

#endif // CHATTERBOX_SYNC_MUTEX_GUARD_HPP
