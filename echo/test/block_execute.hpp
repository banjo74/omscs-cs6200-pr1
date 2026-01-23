#ifndef echo_test_block_execute_hpp
#define echo_test_block_execute_hpp

#include <algorithm>
#include <assert.h>
#include <barrier>
#include <concepts>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace echo::test {
/*!
 Runs f(i, t) for each i in [0, numThreads).  t is a thread index on the range
 [0, numThreads). Will use as many as numThreads threads to run the function
 concurrently. Does it's best to start processing "simultaneously".
 */
template <std::invocable<size_t, size_t> Fcn>
void block_execute(size_t const numElements,
                   size_t       numThreads,
                   Fcn const&   fcn) {
    if (numThreads > numElements) {
        // if too many threads, then one thread per element
        numThreads = numElements;
    }

    // once all threads arrive at this barrier, they're "all released".
    std::barrier barrier{static_cast<ptrdiff_t>(numThreads)};

    auto const worker = [&barrier, &fcn](auto const   indices,
                                         size_t const threadIndex) {
        barrier.arrive_and_wait();
        std::ranges::for_each(indices, [threadIndex, &fcn](auto const i) {
            fcn(i, threadIndex);
        });
    };
    std::vector<std::thread> workers;
    size_t const             tail      = numElements % numThreads;
    size_t const             perThread = (numElements - tail) / numThreads;
    for (size_t i = 0; i < numElements / perThread; ++i) {
        workers.emplace_back(
            worker, std::views::iota(i * perThread, (i + 1) * perThread), i);
    }
    if (tail > 0) {
        assert(workers.size() == numThreads - 1);
        assert(workers.size() * perThread + tail == numElements);
        workers.emplace_back(
            worker,
            std::views::iota(workers.size() * perThread,
                             workers.size() * perThread + tail),
            workers.size());
    }
    for (auto& t : workers) {
        t.join();
    }
}
} // namespace echo::test

#endif // include guard
