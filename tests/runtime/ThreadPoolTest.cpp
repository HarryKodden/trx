#include "TestUtils.h"

#include "trx/runtime/ThreadPool.h"

#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <set>

namespace trx::test {

bool runThreadPoolTest() {
    std::cout << "Running ThreadPool test...\n";

    // Test basic task execution
    {
        ThreadPool pool(2);
        std::atomic<int> counter{0};
        std::vector<std::thread::id> threadIds;
        std::mutex threadIdsMutex;

        // Enqueue 10 tasks
        for (int i = 0; i < 10; ++i) {
            pool.enqueueTask([&counter, &threadIds, &threadIdsMutex]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Simulate work
                counter++;
                std::lock_guard<std::mutex> lock(threadIdsMutex);
                threadIds.push_back(std::this_thread::get_id());
            });
        }

        // Wait for all tasks to complete (pool destructor will wait for tasks)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (counter != 10) {
            std::cerr << "Expected counter to be 10, got " << counter << "\n";
            return false;
        }

        // Check that multiple threads were used
        std::set<std::thread::id> uniqueThreads(threadIds.begin(), threadIds.end());
        if (uniqueThreads.size() < 2) {
            std::cerr << "Expected tasks to run on multiple threads, but only " << uniqueThreads.size() << " unique thread(s) used\n";
            return false;
        }

        std::cout << "Basic task execution test passed - used " << uniqueThreads.size() << " threads\n";
    }

    // Test concurrent execution timing
    {
        ThreadPool pool(4);
        std::atomic<int> completed{0};
        auto start = std::chrono::high_resolution_clock::now();

        // Enqueue tasks that take 100ms each
        for (int i = 0; i < 4; ++i) {
            pool.enqueueTask([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                completed++;
            });
        }

        // Wait for completion
        while (completed < 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // With 4 threads, tasks should complete in ~100ms, not ~400ms
        if (duration.count() > 250) { // Allow some overhead
            std::cerr << "Concurrent execution test failed - took " << duration.count() << "ms, expected ~100ms\n";
            return false;
        }

        std::cout << "Concurrent execution test passed - completed in " << duration.count() << "ms\n";
    }

    // Test task ordering and synchronization
    {
        ThreadPool pool(3);
        std::vector<int> results;
        std::mutex resultsMutex;

        // Enqueue tasks that append to a shared vector
        for (int i = 0; i < 20; ++i) {
            pool.enqueueTask([i, &results, &resultsMutex]() {
                std::lock_guard<std::mutex> lock(resultsMutex);
                results.push_back(i);
            });
        }

        // Wait for completion
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (results.size() != 20) {
            std::cerr << "Expected 20 results, got " << results.size() << "\n";
            return false;
        }

        // Sort and verify all values are present (order may vary due to concurrency)
        std::sort(results.begin(), results.end());
        for (int i = 0; i < 20; ++i) {
            if (results[i] != i) {
                std::cerr << "Missing or incorrect value at index " << i << ": " << results[i] << "\n";
                return false;
            }
        }

        std::cout << "Task ordering test passed - all 20 tasks completed\n";
    }

    std::cout << "All ThreadPool tests passed!\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runThreadPoolTest()) {
        std::cerr << "ThreadPool tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}