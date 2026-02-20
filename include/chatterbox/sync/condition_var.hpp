#ifndef CHATTERBOX_SYNC_CONDITION_VAR_HPP
#define CHATTERBOX_SYNC_CONDITION_VAR_HPP

#include <chatterbox/common.hpp>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <queue>

namespace chatterbox {
namespace sync {

// Enhanced condition variable with timeout and statistics
class TrackedConditionVariable {
public:
    TrackedConditionVariable() = default;
    ~TrackedConditionVariable() = default;

    TrackedConditionVariable(const TrackedConditionVariable&) = delete;
    TrackedConditionVariable& operator=(const TrackedConditionVariable&) = delete;

    void notify_one() {
        notify_count_.fetch_add(1);
        cv_.notify_one();
    }

    void notify_all() {
        notify_count_.fetch_add(1);
        cv_.notify_all();
    }

    template<typename Lock>
    void wait(Lock& lock) {
        wait_count_.fetch_add(1);
        cv_.wait(lock);
    }

    template<typename Lock, typename Predicate>
    void wait(Lock& lock, Predicate pred) {
        wait_count_.fetch_add(1);
        cv_.wait(lock, pred);
    }

    template<typename Lock, typename Rep, typename Period>
    std::cv_status wait_for(Lock& lock, const std::chrono::duration<Rep, Period>& rel_time) {
        wait_count_.fetch_add(1);
        auto result = cv_.wait_for(lock, rel_time);
        if (result == std::cv_status::timeout) {
            timeout_count_.fetch_add(1);
        }
        return result;
    }

    template<typename Lock, typename Rep, typename Period, typename Predicate>
    bool wait_for(Lock& lock, const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
        wait_count_.fetch_add(1);
        bool result = cv_.wait_for(lock, rel_time, pred);
        if (!result) {
            timeout_count_.fetch_add(1);
        }
        return result;
    }

    // Statistics
    uint64_t wait_count() const { return wait_count_.load(); }
    uint64_t notify_count() const { return notify_count_.load(); }
    uint64_t timeout_count() const { return timeout_count_.load(); }

private:
    std::condition_variable cv_;
    std::atomic<uint64_t> wait_count_{0};
    std::atomic<uint64_t> notify_count_{0};
    std::atomic<uint64_t> timeout_count_{0};
};

// Event flag for simple signaling
class Event {
public:
    Event(bool initial_state = false) : signaled_(initial_state) {}

    void set() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        signaled_ = false;
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return signaled_; });
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return signaled_; });
    }

    bool is_set() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return signaled_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_;
};

// Auto-reset event (resets after one waiter is released)
class AutoResetEvent {
public:
    AutoResetEvent(bool initial_state = false) : signaled_(initial_state) {}

    void set() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return signaled_; });
        signaled_ = false; // Auto-reset
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool result = cv_.wait_for(lock, timeout, [this] { return signaled_; });
        if (result) {
            signaled_ = false; // Auto-reset
        }
        return result;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_;
};

// Countdown latch for waiting on multiple events
class CountdownLatch {
public:
    explicit CountdownLatch(int count) : count_(count) {}

    void count_down() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ > 0) {
            --count_;
            if (count_ == 0) {
                cv_.notify_all();
            }
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ == 0; });
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return count_ == 0; });
    }

    int get_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

// Barrier for synchronizing multiple threads
class Barrier {
public:
    explicit Barrier(size_t count)
        : threshold_(count), count_(count), generation_(0) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        auto gen = generation_;

        if (--count_ == 0) {
            ++generation_;
            count_ = threshold_;
            cv_.notify_all();
        } else {
            cv_.wait(lock, [this, gen] { return gen != generation_; });
        }
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto gen = generation_;

        if (--count_ == 0) {
            ++generation_;
            count_ = threshold_;
            cv_.notify_all();
            return true;
        } else {
            return cv_.wait_for(lock, timeout, [this, gen] { return gen != generation_; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t threshold_;
    size_t count_;
    size_t generation_;
};

// Semaphore implementation (for pre-C++20)
class Semaphore {
public:
    explicit Semaphore(int initial_count = 0) : count_(initial_count) {}

    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

    bool try_acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }

    bool try_acquire_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this] { return count_ > 0; })) {
            --count_;
            return true;
        }
        return false;
    }

    void release(int count = 1) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            count_ += count;
        }
        for (int i = 0; i < count; ++i) {
            cv_.notify_one();
        }
    }

    int count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

// Thread-safe blocking queue
template<typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t max_size = 0) : max_size_(max_size), closed_(false) {}

    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (max_size_ > 0) {
            not_full_.wait(lock, [this] { return queue_.size() < max_size_ || closed_; });
        }
        if (closed_) {
            throw std::runtime_error("Queue is closed");
        }
        queue_.push(std::move(item));
        not_empty_.notify_one();
    }

    bool try_push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || (max_size_ > 0 && queue_.size() >= max_size_)) {
            return false;
        }
        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) {
            throw std::runtime_error("Queue is closed and empty");
        }
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    bool try_pop_for(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_.wait_for(lock, timeout, [this] { return !queue_.empty() || closed_; })) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;
    size_t max_size_;
    bool closed_;
};

} // namespace sync
} // namespace chatterbox

#endif // CHATTERBOX_SYNC_CONDITION_VAR_HPP
