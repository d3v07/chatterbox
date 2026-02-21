#include <chatterbox/sync/thread_pool.hpp>
#include <stdexcept>

namespace chatterbox {
namespace sync {

// ThreadPool implementation
ThreadPool::ThreadPool(size_t num_threads) {
    add_workers(num_threads);
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_thread() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] {
                return stop_.load() || !tasks_.empty();
            });

            if (stop_.load() && tasks_.empty()) {
                return;
            }

            if (tasks_.empty()) {
                continue;
            }

            task = std::move(const_cast<Task&>(tasks_.top()));
            tasks_.pop();
        }

        active_tasks_.fetch_add(1);
        try {
            task.func();
        } catch (...) {
            // Swallow exceptions to prevent thread termination
        }
        active_tasks_.fetch_sub(1);
        completed_count_.fetch_add(1);

        completion_condition_.notify_all();
    }
}

void ThreadPool::add_workers(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

void ThreadPool::execute(std::function<void()> task) {
    execute_with_priority(TaskPriority::NORMAL, std::move(task));
}

void ThreadPool::execute_with_priority(TaskPriority priority, std::function<void()> func) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }

        Task t;
        t.func = std::move(func);
        t.priority = priority;
        t.submit_time = std::chrono::steady_clock::now().time_since_epoch().count();
        t.id = task_id_counter_.fetch_add(1);

        tasks_.push(std::move(t));
    }
    condition_.notify_one();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    completion_condition_.wait(lock, [this] {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_.store(true);
    }

    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
}

size_t ThreadPool::pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::resize(size_t num_threads) {
    if (num_threads > workers_.size()) {
        add_workers(num_threads - workers_.size());
    }
    // Shrinking is not implemented - workers will exit naturally when stop is set
}

// WorkStealingPool implementation
WorkStealingPool::WorkStealingPool(size_t num_threads) {
    queues_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        queues_.push_back(std::make_unique<WorkQueue>());
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&WorkStealingPool::worker_thread, this, i);
    }
}

WorkStealingPool::~WorkStealingPool() {
    shutdown();
}

void WorkStealingPool::worker_thread(size_t id) {
    while (!stop_.load()) {
        std::function<void()> task;

        // Try to get task from own queue first
        {
            std::lock_guard<std::mutex> lock(queues_[id]->mutex);
            if (!queues_[id]->tasks.empty()) {
                task = std::move(queues_[id]->tasks.front());
                queues_[id]->tasks.pop_front();
            }
        }

        // If no task, try to steal from other queues
        if (!task && !try_steal(id, task)) {
            // No work available, wait
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }

        if (task) {
            active_tasks_.fetch_add(1);
            try {
                task();
            } catch (...) {
                // Swallow exceptions
            }
            active_tasks_.fetch_sub(1);
        }
    }
}

bool WorkStealingPool::try_steal(size_t thief_id, std::function<void()>& task) {
    for (size_t i = 0; i < queues_.size(); ++i) {
        if (i == thief_id) continue;

        std::lock_guard<std::mutex> lock(queues_[i]->mutex);
        if (!queues_[i]->tasks.empty()) {
            // Steal from the back (LIFO for the victim, FIFO for the thief)
            task = std::move(queues_[i]->tasks.back());
            queues_[i]->tasks.pop_back();
            return true;
        }
    }
    return false;
}

void WorkStealingPool::shutdown() {
    stop_.store(true);
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
    queues_.clear();
}

void WorkStealingPool::wait_all() {
    while (true) {
        bool all_empty = true;
        for (auto& queue : queues_) {
            std::lock_guard<std::mutex> lock(queue->mutex);
            if (!queue->tasks.empty()) {
                all_empty = false;
                break;
            }
        }

        if (all_empty && active_tasks_.load() == 0) {
            break;
        }

        std::this_thread::yield();
    }
}

} // namespace sync
} // namespace chatterbox
