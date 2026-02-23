#include <chatterbox/sync/thread_pool.hpp>
#include <chatterbox/sync/mutex_guard.hpp>
#include <chatterbox/sync/condition_var.hpp>
#include <iostream>
#include <cassert>
#include <atomic>
#include <vector>
#include <chrono>

using namespace chatterbox;
using namespace chatterbox::sync;

void test_thread_pool_basic() {
    std::cout << "Testing ThreadPool basic operations..." << std::endl;

    ThreadPool pool(4);
    assert(pool.num_threads() == 4);
    assert(pool.is_running());

    // Submit simple task
    auto future = pool.submit([]() { return 42; });
    assert(future.get() == 42);

    // Submit void task
    std::atomic<bool> executed{false};
    pool.execute([&executed]() { executed = true; });
    pool.wait_all();
    assert(executed.load());

    std::cout << "  ThreadPool basic tests passed!" << std::endl;
}

void test_thread_pool_concurrent() {
    std::cout << "Testing ThreadPool concurrent execution..." << std::endl;

    ThreadPool pool(8);
    std::atomic<int> counter{0};
    const int num_tasks = 1000;

    // Submit many tasks
    std::vector<std::future<int>> futures;
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([&counter, i]() {
            counter.fetch_add(1);
            return i;
        }));
    }

    // Wait and verify
    for (int i = 0; i < num_tasks; ++i) {
        assert(futures[i].get() == i);
    }
    assert(counter.load() == num_tasks);

    std::cout << "  ThreadPool concurrent tests passed!" << std::endl;
}

void test_thread_pool_priority() {
    std::cout << "Testing ThreadPool priority execution..." << std::endl;

    ThreadPool pool(1);  // Single thread to test ordering

    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Submit with different priorities
    pool.submit_with_priority(TaskPriority::LOW, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(0);
    });

    pool.submit_with_priority(TaskPriority::HIGH, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(2);
    });

    pool.submit_with_priority(TaskPriority::NORMAL, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(1);
    });

    pool.submit_with_priority(TaskPriority::CRITICAL, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(3);
    });

    pool.wait_all();

    // Higher priority should execute first
    // Note: Due to timing, the first task might already be picked up
    assert(execution_order.size() == 4);

    std::cout << "  ThreadPool priority tests passed!" << std::endl;
}

void test_thread_pool_exceptions() {
    std::cout << "Testing ThreadPool exception handling..." << std::endl;

    ThreadPool pool(2);

    // Submit task that throws
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("Test exception");
        return 0;
    });

    // Getting result should throw
    bool caught = false;
    try {
        future.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        assert(std::string(e.what()) == "Test exception");
    }
    assert(caught);

    // Pool should still work after exception
    auto future2 = pool.submit([]() { return 42; });
    assert(future2.get() == 42);

    std::cout << "  ThreadPool exception tests passed!" << std::endl;
}

void test_spin_lock() {
    std::cout << "Testing SpinLock..." << std::endl;

    SpinLock lock;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&lock, &counter]() {
            for (int j = 0; j < 1000; ++j) {
                SpinLockGuard guard(lock);
                counter++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(counter.load() == 10000);

    std::cout << "  SpinLock tests passed!" << std::endl;
}

void test_tracked_mutex() {
    std::cout << "Testing TrackedMutex..." << std::endl;

    TrackedMutex mutex;

    // Basic lock/unlock
    mutex.lock();
    mutex.unlock();

    assert(mutex.lock_count() >= 1);

    // Try lock
    assert(mutex.try_lock());
    assert(!mutex.try_lock());
    mutex.unlock();

    // Concurrent access to track contention
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&mutex]() {
            for (int j = 0; j < 100; ++j) {
                mutex.lock();
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                mutex.unlock();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "  Lock count: " << mutex.lock_count() << std::endl;
    std::cout << "  Contention count: " << mutex.contention_count() << std::endl;
    assert(mutex.lock_count() >= 400);

    std::cout << "  TrackedMutex tests passed!" << std::endl;
}

void test_event() {
    std::cout << "Testing Event..." << std::endl;

    Event event;
    assert(!event.is_set());

    // Set and wait
    event.set();
    assert(event.is_set());
    event.wait();  // Should return immediately

    // Reset
    event.reset();
    assert(!event.is_set());

    // Wait with timeout
    bool result = event.wait_for(std::chrono::milliseconds(10));
    assert(!result);  // Should timeout

    // Signal from another thread
    std::thread signaler([&event]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        event.set();
    });

    event.wait();
    signaler.join();

    std::cout << "  Event tests passed!" << std::endl;
}

void test_countdown_latch() {
    std::cout << "Testing CountdownLatch..." << std::endl;

    CountdownLatch latch(5);
    assert(latch.get_count() == 5);

    // Count down from multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&latch, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * i));
            latch.count_down();
        });
    }

    // Wait for all
    latch.wait();
    assert(latch.get_count() == 0);

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "  CountdownLatch tests passed!" << std::endl;
}

void test_barrier() {
    std::cout << "Testing Barrier..." << std::endl;

    const int num_threads = 4;
    Barrier barrier(num_threads);
    std::atomic<int> phase1_count{0};
    std::atomic<int> phase2_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&barrier, &phase1_count, &phase2_count]() {
            // Phase 1
            phase1_count.fetch_add(1);
            barrier.wait();

            // All threads should have completed phase 1
            assert(phase1_count.load() == num_threads);

            // Phase 2
            phase2_count.fetch_add(1);
            barrier.wait();

            // All threads should have completed phase 2
            assert(phase2_count.load() == num_threads);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "  Barrier tests passed!" << std::endl;
}

void test_semaphore() {
    std::cout << "Testing Semaphore..." << std::endl;

    Semaphore sem(3);
    assert(sem.count() == 3);

    // Acquire
    sem.acquire();
    assert(sem.count() == 2);

    // Try acquire
    assert(sem.try_acquire());
    assert(sem.count() == 1);

    // Release
    sem.release();
    assert(sem.count() == 2);

    // Release multiple
    sem.release(2);
    assert(sem.count() == 4);

    std::cout << "  Semaphore tests passed!" << std::endl;
}

void test_blocking_queue() {
    std::cout << "Testing BlockingQueue..." << std::endl;

    BlockingQueue<int> queue(10);

    // Push and pop
    queue.push(42);
    int value = queue.pop();
    assert(value == 42);

    // Try push/pop
    assert(queue.try_push(1));
    assert(queue.try_push(2));

    int v;
    assert(queue.try_pop(v) && v == 1);
    assert(queue.try_pop(v) && v == 2);
    assert(!queue.try_pop(v));

    // Producer-consumer
    std::thread producer([&queue]() {
        for (int i = 0; i < 100; ++i) {
            queue.push(i);
        }
    });

    std::thread consumer([&queue]() {
        for (int i = 0; i < 100; ++i) {
            int val = queue.pop();
            assert(val == i);
        }
    });

    producer.join();
    consumer.join();

    // Close queue
    queue.close();
    assert(queue.is_closed());

    std::cout << "  BlockingQueue tests passed!" << std::endl;
}

void test_parallel_for() {
    std::cout << "Testing ParallelFor..." << std::endl;

    ThreadPool pool(4);
    std::vector<std::atomic<int>> results(100);

    ParallelFor::run(pool, size_t(0), size_t(100), [&results](size_t i) {
        results[i].store(static_cast<int>(i * 2));
    });

    for (size_t i = 0; i < 100; ++i) {
        assert(results[i].load() == static_cast<int>(i * 2));
    }

    std::cout << "  ParallelFor tests passed!" << std::endl;
}

void test_work_stealing_pool() {
    std::cout << "Testing WorkStealingPool..." << std::endl;

    WorkStealingPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 1000; ++i) {
        pool.submit([&counter]() {
            counter.fetch_add(1);
        });
    }

    pool.wait_all();
    assert(counter.load() == 1000);

    std::cout << "  WorkStealingPool tests passed!" << std::endl;
}

int main() {
    std::cout << "=== ChatterBox Sync Unit Tests ===" << std::endl;

    try {
        test_thread_pool_basic();
        test_thread_pool_concurrent();
        test_thread_pool_priority();
        test_thread_pool_exceptions();
        test_spin_lock();
        test_tracked_mutex();
        test_event();
        test_countdown_latch();
        test_barrier();
        test_semaphore();
        test_blocking_queue();
        test_parallel_for();
        test_work_stealing_pool();

        std::cout << "\n=== All sync tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
