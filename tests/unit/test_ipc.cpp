#include <chatterbox/ipc/message_queue.hpp>
#include <chatterbox/ipc/shared_memory.hpp>
#include <chatterbox/ipc/semaphore.hpp>
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>

using namespace chatterbox;
using namespace chatterbox::ipc;

// Test keys (use unique values to avoid conflicts)
constexpr key_t TEST_QUEUE_KEY = 0x54455354;   // "TEST"
constexpr key_t TEST_SHM_KEY = 0x54455352;     // "TESR"
constexpr key_t TEST_SEM_KEY = 0x54455353;     // "TESS"

void cleanup_ipc() {
    // Clean up any leftover IPC resources
    MessageQueue::remove(TEST_QUEUE_KEY);
    SharedMemory::remove(TEST_SHM_KEY);
    Semaphore::remove(TEST_SEM_KEY);
}

void test_message_queue_basic() {
    std::cout << "Testing MessageQueue basic operations..." << std::endl;

    cleanup_ipc();

    // Create queue
    MessageQueue queue(TEST_QUEUE_KEY, true);
    assert(queue.is_valid());

    // Send message
    const char* test_data = "Hello, IPC!";
    auto err = queue.send(1, test_data, strlen(test_data) + 1);
    assert(err == ErrorCode::SUCCESS);

    // Check stats
    auto stats = queue.get_stats();
    assert(stats.message_count >= 1);

    // Receive message
    char buffer[256];
    size_t size = sizeof(buffer);
    err = queue.receive(1, buffer, size);
    assert(err == ErrorCode::SUCCESS);
    assert(strcmp(buffer, test_data) == 0);

    std::cout << "  MessageQueue basic tests passed!" << std::endl;
}

void test_message_queue_nonblocking() {
    std::cout << "Testing MessageQueue non-blocking operations..." << std::endl;

    cleanup_ipc();

    MessageQueue queue(TEST_QUEUE_KEY, true);

    // Non-blocking receive on empty queue
    char buffer[256];
    size_t size = sizeof(buffer);
    auto err = queue.receive(1, buffer, size, false);
    assert(err == ErrorCode::ERR_TIMEOUT);

    // Send and non-blocking receive
    const char* data = "Test";
    queue.send(1, data, 5);

    err = queue.receive(1, buffer, size, false);
    assert(err == ErrorCode::SUCCESS);

    // try_receive
    queue.send(1, data, 5);
    auto result = queue.try_receive(1);
    assert(result.has_value());

    result = queue.try_receive(1);
    assert(!result.has_value());

    std::cout << "  MessageQueue non-blocking tests passed!" << std::endl;
}

void test_message_queue_types() {
    std::cout << "Testing MessageQueue message types..." << std::endl;

    cleanup_ipc();

    MessageQueue queue(TEST_QUEUE_KEY, true);

    // Send messages with different types
    queue.send(1, "Type1", 6);
    queue.send(2, "Type2", 6);
    queue.send(3, "Type3", 6);

    // Receive specific type
    char buffer[256];
    size_t size = sizeof(buffer);

    queue.receive(2, buffer, size);
    assert(strcmp(buffer, "Type2") == 0);

    queue.receive(1, buffer, size);
    assert(strcmp(buffer, "Type1") == 0);

    queue.receive(3, buffer, size);
    assert(strcmp(buffer, "Type3") == 0);

    std::cout << "  MessageQueue type tests passed!" << std::endl;
}

void test_shared_memory_basic() {
    std::cout << "Testing SharedMemory basic operations..." << std::endl;

    cleanup_ipc();

    // Create shared memory
    const size_t size = 4096;
    SharedMemory shm(TEST_SHM_KEY, size, true);
    assert(shm.is_valid());
    assert(shm.size() == size);

    // Write data
    int* data = shm.as<int>();
    data[0] = 42;
    data[1] = 123;

    // Read data
    assert(data[0] == 42);
    assert(data[1] == 123);

    // Get info
    auto info = shm.get_info();
    assert(info.size >= size);
    assert(info.num_attaches >= 1);

    std::cout << "  SharedMemory basic tests passed!" << std::endl;
}

void test_chat_shared_memory() {
    std::cout << "Testing ChatSharedMemory..." << std::endl;

    cleanup_ipc();

    // Create
    ChatSharedMemory csm(TEST_SHM_KEY, true);
    assert(csm.is_valid());

    // Check initial state
    assert(csm.active_user_count() == 0);
    assert(csm.state()->server_running.load() == true);

    // Register users
    auto* user1 = csm.register_user("Alice", 1000);
    assert(user1 != nullptr);
    assert(user1->user_id != INVALID_USER_ID);
    assert(strcmp(user1->username, "Alice") == 0);
    assert(csm.active_user_count() == 1);

    auto* user2 = csm.register_user("Bob", 1001);
    assert(user2 != nullptr);
    assert(csm.active_user_count() == 2);

    // Duplicate username should fail
    auto* dup = csm.register_user("Alice", 1002);
    assert(dup == nullptr);

    // Find user
    auto* found = csm.users()->find_user(user1->user_id);
    assert(found == user1);

    found = csm.users()->find_by_name("Bob");
    assert(found == user2);

    // Sequence numbers
    auto seq1 = csm.next_sequence();
    auto seq2 = csm.next_sequence();
    assert(seq2 > seq1);

    // Unregister
    csm.unregister_user(user1->user_id);
    assert(csm.active_user_count() == 1);

    std::cout << "  ChatSharedMemory tests passed!" << std::endl;
}

void test_semaphore_basic() {
    std::cout << "Testing Semaphore basic operations..." << std::endl;

    cleanup_ipc();

    // Create semaphore
    Semaphore sem(TEST_SEM_KEY, 1, true);
    assert(sem.is_valid());

    // Initial value should be 1
    assert(sem.get_value(0) == 1);

    // Wait (decrement)
    auto err = sem.wait(0);
    assert(err == ErrorCode::SUCCESS);
    assert(sem.get_value(0) == 0);

    // Try wait should fail
    assert(sem.try_wait(0) == false);

    // Signal (increment)
    err = sem.signal(0);
    assert(err == ErrorCode::SUCCESS);
    assert(sem.get_value(0) == 1);

    // Try wait should succeed
    assert(sem.try_wait(0) == true);

    std::cout << "  Semaphore basic tests passed!" << std::endl;
}

void test_semaphore_guard() {
    std::cout << "Testing SemaphoreGuard..." << std::endl;

    cleanup_ipc();

    Semaphore sem(TEST_SEM_KEY, 1, true);

    {
        SemaphoreGuard guard(sem, 0);
        assert(guard.owns_lock());
        assert(sem.get_value(0) == 0);
    }

    // After guard destruction, semaphore should be released
    assert(sem.get_value(0) == 1);

    std::cout << "  SemaphoreGuard tests passed!" << std::endl;
}

void test_rwlock() {
    std::cout << "Testing RWLock..." << std::endl;

    cleanup_ipc();

    RWLock lock(TEST_SEM_KEY, true);
    assert(lock.is_valid());

    // Read lock
    lock.read_lock();
    // Multiple readers should be allowed (but we can't test in single thread)
    lock.read_unlock();

    // Write lock
    lock.write_lock();
    lock.write_unlock();

    // Try locks
    assert(lock.try_read_lock());
    lock.read_unlock();

    assert(lock.try_write_lock());
    lock.write_unlock();

    std::cout << "  RWLock tests passed!" << std::endl;
}

void test_concurrent_queue() {
    std::cout << "Testing concurrent MessageQueue access..." << std::endl;

    cleanup_ipc();

    MessageQueue queue(TEST_QUEUE_KEY, true);
    std::atomic<int> sent_count{0};
    std::atomic<int> recv_count{0};
    const int num_messages = 100;

    // Producer threads
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&queue, &sent_count, num_messages, i]() {
            for (int j = 0; j < num_messages; ++j) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Thread %d Message %d", i, j);
                if (queue.send(1, msg, strlen(msg) + 1) == ErrorCode::SUCCESS) {
                    sent_count.fetch_add(1);
                }
            }
        });
    }

    // Consumer thread
    std::thread consumer([&queue, &recv_count]() {
        char buffer[256];
        for (int i = 0; i < 1000; ++i) {
            size_t size = sizeof(buffer);
            if (queue.receive(1, buffer, size, false) == ErrorCode::SUCCESS) {
                recv_count.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    std::cout << "  Sent: " << sent_count.load() << ", Received: " << recv_count.load() << std::endl;
    assert(sent_count.load() == 4 * num_messages);
    assert(recv_count.load() > 0);

    std::cout << "  Concurrent MessageQueue tests passed!" << std::endl;
}

void test_concurrent_semaphore() {
    std::cout << "Testing concurrent Semaphore access..." << std::endl;

    cleanup_ipc();

    Semaphore sem(TEST_SEM_KEY, 1, true);
    std::atomic<int> counter{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current_concurrent{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&sem, &counter, &max_concurrent, &current_concurrent]() {
            for (int j = 0; j < 100; ++j) {
                sem.wait(0);

                current_concurrent.fetch_add(1);
                int curr = current_concurrent.load();
                int max = max_concurrent.load();
                while (curr > max && !max_concurrent.compare_exchange_weak(max, curr)) {
                    max = max_concurrent.load();
                }

                counter.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(10));

                current_concurrent.fetch_sub(1);
                sem.signal(0);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(counter.load() == 1000);
    assert(max_concurrent.load() == 1);  // Semaphore should enforce mutual exclusion

    std::cout << "  Concurrent Semaphore tests passed!" << std::endl;
}

int main() {
    std::cout << "=== ChatterBox IPC Unit Tests ===" << std::endl;

    try {
        test_message_queue_basic();
        test_message_queue_nonblocking();
        test_message_queue_types();
        test_shared_memory_basic();
        test_chat_shared_memory();
        test_semaphore_basic();
        test_semaphore_guard();
        test_rwlock();
        test_concurrent_queue();
        test_concurrent_semaphore();

        cleanup_ipc();

        std::cout << "\n=== All IPC tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        cleanup_ipc();
        return 1;
    }
}
