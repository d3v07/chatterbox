#include <chatterbox/sync/mutex_guard.hpp>
#include <chrono>

namespace chatterbox {
namespace sync {

// TrackedMutex implementation
void TrackedMutex::lock() {
    auto start = std::chrono::steady_clock::now();
    bool was_contended = !mutex_.try_lock();

    if (was_contended) {
        contention_count_.fetch_add(1);
        mutex_.lock();
    }

    auto end = std::chrono::steady_clock::now();
    auto wait_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    total_wait_time_ns_.fetch_add(wait_time);
    lock_count_.fetch_add(1);
}

bool TrackedMutex::try_lock() {
    if (mutex_.try_lock()) {
        lock_count_.fetch_add(1);
        return true;
    }
    return false;
}

void TrackedMutex::unlock() {
    mutex_.unlock();
}

// TrackedSharedMutex implementation
void TrackedSharedMutex::lock() {
    mutex_.lock();
    write_lock_count_.fetch_add(1);
}

bool TrackedSharedMutex::try_lock() {
    if (mutex_.try_lock()) {
        write_lock_count_.fetch_add(1);
        return true;
    }
    return false;
}

void TrackedSharedMutex::unlock() {
    mutex_.unlock();
}

void TrackedSharedMutex::lock_shared() {
    mutex_.lock_shared();
    read_lock_count_.fetch_add(1);
}

bool TrackedSharedMutex::try_lock_shared() {
    if (mutex_.try_lock_shared()) {
        read_lock_count_.fetch_add(1);
        return true;
    }
    return false;
}

void TrackedSharedMutex::unlock_shared() {
    mutex_.unlock_shared();
}

// HierarchicalMutex static member
thread_local unsigned long HierarchicalMutex::this_thread_hierarchy_value_ = ULONG_MAX;

} // namespace sync
} // namespace chatterbox
