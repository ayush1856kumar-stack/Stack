#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <immintrin.h> // For _mm_pause
#include "SPStack.h"
#include <iostream>
#include <vector>
#include <thread>
#include <numeric>
#include <algorithm>
using namespace stone;
void run_integrity_hammer(size_t num_items)
{
    SPStack<int> stack;
    std::vector<int> results;
    results.reserve(num_items);
    std::atomic<bool> start_signal{false};

    std::thread consumer([&]()
                         {
        while (!start_signal.load(std::memory_order_acquire));

        size_t total_popped = 0;
        const size_t BATCH_SIZE = 8; // Consumer still batches for speed
        int buffer[BATCH_SIZE];

        while (total_popped < num_items) {
            size_t taken = stack.batch_pop(buffer, BATCH_SIZE);
            if (taken > 0) {
                for (size_t i = 0; i < taken; ++i) {
                    results.push_back(buffer[i]);
                }
                total_popped += taken;
            }
        } });

    // PRODUCER
    start_signal.store(true, std::memory_order_release);
    for (int i = 1; i <= (int)num_items; ++i)
    {
        while (!stack.push(i))
            ; // Keep trying until successful
    }

    consumer.join();

    // Verification
    std::sort(results.begin(), results.end());
    for (size_t i = 1; i < results.size(); ++i)
    {
        if (results[i] == results[i - 1])
        {
            std::cout << "FOUND DUPLICATE ID: " << results[i] << std::endl;
        }
    }
    if (results.size() != num_items)
    {
        std::cout << "FAILED: Expected " << num_items << " but got " << results.size() << std::endl;
    }
    else
    {
        // Simple checksum verification
        long long sum = std::accumulate(results.begin(), results.end(), 0LL);
        long long expected = (long long)num_items * (num_items + 1) / 2;
        if (sum == expected)
        {
            std::cout << "SUCCESS: All data intact." << std::endl;
        }
        else
        {
            std::cout << "FAILED: Data corruption detected (Sum mismatch)." << std::endl;
        }
    }
}
double run_throughput_test(int num_iterations)
{
    SPStack<int> stack;
    // Use a single start flag for both
    alignas(64) std::atomic<bool> start_signal(false);
    alignas(64) std::atomic<bool> producer_done(false);

    std::thread consumer([&]()
                         {
        // Wait for producer to give the green light
        while (!start_signal.load(std::memory_order_acquire));

        size_t total_popped = 0;
        const size_t BATCH_SIZE = 100; 
        int local_buffer[BATCH_SIZE];

        while (total_popped < num_iterations) {
            size_t taken = stack.batch_pop(local_buffer, BATCH_SIZE);
           [[likely]] if (taken > 0) {
                total_popped += taken;
            } else {
                // Critical: prevents the CPU from over-heating while spinning
                _mm_pause(); 
            }
        } });

    // Warm-up (Optional but recommended)
    for (int i = 0; i < 100; ++i)
    {
        stack.push(i);
        stack.pop();
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_release);

    for (int i = 0; i < num_iterations; ++i)
    {
        // In SPSC, we only fail push if the block is full/allocation fails
        while (!stack.push(i))
        {
            _mm_pause();
        }
    }

    consumer.join();
    auto end_time = std::chrono::high_resolution_clock::now();

    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    double ops_per_sec = (double)num_iterations / (duration_ns / 1e9);
    double latency_per_op = (double)duration_ns / num_iterations;

    std::cout << "Throughput: " << (ops_per_sec / 1e6) << " Million ops/sec | "
              << "Avg Latency: " << latency_per_op << " ns" << std::endl;

    return latency_per_op;
}

int main()
{
    // double total_lat = 0;
    // int runs = 10; // 50 runs is a lot; 10 is usually enough for an average
    // for (int i = 0; i < runs; i++) {
    //     total_lat += run_throughput_test(1000000);
    // }
    // std::cout << "\nFinal Average Latency: " << (total_lat / runs) << " ns" << std::endl;
    run_integrity_hammer(1000);
    return 0;
}