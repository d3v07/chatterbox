#ifndef CHATTERBOX_SYNC_THREAD_POOL_HPP
#define CHATTERBOX_SYNC_THREAD_POOL_HPP

#include <chatterbox/common.hpp>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>

namespace chatterbox {
namespace sync {

// Priority levels for tasks
enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

// Task wrapper with priority
struct Task {
    std::function<void()> func;
    TaskPriority priority;
    uint64_t submit_time;
    uint64_t id;

    bool operator<(const Task& other) const {
        // Higher priority first, then earlier submission time
        if (priority != other.priority) {
            return priority < other.priority;
        }
        return submit_time > other.submit_time;
    }
};

// Thread pool for handling concurrent operations
class ThreadPool {
public:
    // Create pool with specified number of threads
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());

    // Destructor - waits for all tasks to complete
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a task and get a future for the result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    // Submit a task with priority
    template<typename F, typename... Args>
    auto submit_with_priority(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // Submit a fire-and-forget task
    void execute(std::function<void()> task);
    void execute_with_priority(TaskPriority priority, std::function<void()> task);

    // Wait for all tasks to complete
    void wait_all();

    // Shutdown the pool
    void shutdown();

    // Get statistics
    size_t num_threads() const { return workers_.size(); }
    size_t pending_tasks() const;
    size_t completed_tasks() const { return completed_count_.load(); }
    bool is_running() const { return !stop_.load(); }

    // Resize the pool (add or remove workers)
    void resize(size_t num_threads);

private:
    std::vector<std::thread> workers_;
    std::priority_queue<Task> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable completion_condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<uint64_t> completed_count_{0};
    std::atomic<uint64_t> task_id_counter_{0};

    void worker_thread();
    void add_workers(size_t count);
};

// Implementation of template methods
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    return submit_with_priority(TaskPriority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPool::submit_with_priority(TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {

    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }

        Task t;
        t.func = [task]() { (*task)(); };
        t.priority = priority;
        t.submit_time = std::chrono::steady_clock::now().time_since_epoch().count();
        t.id = task_id_counter_.fetch_add(1);

        tasks_.push(std::move(t));
    }

    condition_.notify_one();
    return result;
}

// Worker thread pool with work stealing
class WorkStealingPool {
public:
    explicit WorkStealingPool(size_t num_threads = std::thread::hardware_concurrency());
    ~WorkStealingPool();

    template<typename F>
    void submit(F&& f);

    void shutdown();
    void wait_all();

private:
    struct WorkQueue {
        std::deque<std::function<void()>> tasks;
        std::mutex mutex;
    };

    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkQueue>> queues_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::condition_variable cv_;
    std::mutex cv_mutex_;

    void worker_thread(size_t id);
    bool try_steal(size_t thief_id, std::function<void()>& task);
};

template<typename F>
void WorkStealingPool::submit(F&& f) {
    static thread_local size_t local_queue = 0;
    size_t queue_id = local_queue++ % queues_.size();

    {
        std::lock_guard<std::mutex> lock(queues_[queue_id]->mutex);
        queues_[queue_id]->tasks.push_back(std::forward<F>(f));
    }

    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        cv_.notify_one();
    }
}

// Async task wrapper for easier async operations
template<typename T>
class AsyncTask {
public:
    AsyncTask() = default;

    template<typename F>
    explicit AsyncTask(ThreadPool& pool, F&& f) {
        future_ = pool.submit(std::forward<F>(f));
    }

    bool is_ready() const {
        return future_.valid() &&
               future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    T get() {
        return future_.get();
    }

    void wait() {
        future_.wait();
    }

    bool valid() const {
        return future_.valid();
    }

private:
    std::future<T> future_;
};

// Parallel for loop utility
class ParallelFor {
public:
    template<typename F>
    static void run(ThreadPool& pool, size_t begin, size_t end, F&& f);

    template<typename Iterator, typename F>
    static void run(ThreadPool& pool, Iterator begin, Iterator end, F&& f);
};

template<typename F>
void ParallelFor::run(ThreadPool& pool, size_t begin, size_t end, F&& f) {
    size_t num_threads = pool.num_threads();
    size_t chunk_size = (end - begin + num_threads - 1) / num_threads;

    std::vector<std::future<void>> futures;
    futures.reserve(num_threads);

    for (size_t i = begin; i < end; i += chunk_size) {
        size_t chunk_end = std::min(i + chunk_size, end);
        futures.push_back(pool.submit([=, &f]() {
            for (size_t j = i; j < chunk_end; ++j) {
                f(j);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

template<typename Iterator, typename F>
void ParallelFor::run(ThreadPool& pool, Iterator begin, Iterator end, F&& f) {
    std::vector<std::future<void>> futures;

    for (auto it = begin; it != end; ++it) {
        futures.push_back(pool.submit([=, &f]() {
            f(*it);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace sync
} // namespace chatterbox

#endif // CHATTERBOX_SYNC_THREAD_POOL_HPP
