#include "../gf-student.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <iterator>
#include <ranges>
#include <thread>
#include <vector>

namespace {
struct QueueDestroyer {
    void operator()(Queue* q) const {
        queue_destroy(q);
    }
};

using QueuePtr = std::unique_ptr<Queue, QueueDestroyer>;

QueuePtr create_queue() {
    return QueuePtr{queue_create()};
}
} // namespace

TEST(Queue, CreateDestroy) {
    auto q = create_queue();
}

namespace {
union IntToPtr {
    int   i;
    void* ptr;
};

static_assert(sizeof(IntToPtr) == sizeof(void*));

void* to_ptr(int const i) {
    IntToPtr i2p;
    i2p.i = i;
    return i2p.ptr;
}

int to_int(void* const ptr) {
    IntToPtr i2p;
    i2p.ptr = ptr;
    return i2p.i;
}

auto ints(int const low, int const high) {
    std::vector<int> out;
    std::ranges::copy(std::views::iota(low, high + 1), std::back_inserter(out));
    return out;
}
} // namespace

TEST(Queue, SingleThreadedFifo) {
    auto       q  = create_queue();
    auto const in = ints(-1028, 1028);
    for (auto const i : in) {
        queue_enqueue(q.get(), to_ptr(i));
    }
    std::vector<int> out;
    while (!queue_empty(q.get())) {
        out.push_back(to_int(queue_dequeue(q.get())));
    }
    EXPECT_EQ(in, out);
}

TEST(Queue, ConcurrentOneThread) {
    auto             q  = create_queue();
    auto             in = ints(-1028, 1028);
    std::vector<int> out;

    std::thread t{[&q, &out, &in] {
        while (out.size() < in.size()) {
            out.push_back(to_int(queue_dequeue(q.get())));
        }
    }};
    for (auto const i : in) {
        queue_enqueue(q.get(), to_ptr(i));
    }
    t.join();
    EXPECT_EQ(in, out);
}

TEST(Queue, ManyReadersManyWriters) {
    auto             q  = create_queue();
    int const        n  = 1024 * 128;
    auto const       in = ints(-n, n);
    std::vector<int> out;

    for (size_t numReaders : {1, 2, 64}) {
        for (size_t numWriters : {1, 2, 64}) {
            static int const pill = std::numeric_limits<int>::max();
            std::barrier     readerBarrier{static_cast<ptrdiff_t>(numReaders)};
            std::vector<std::thread>      readers;
            std::vector<std::vector<int>> bins;
            bins.reserve(numReaders);
            while (readers.size() < numReaders) {
                bins.emplace_back();
                auto& bin = bins.back();
                readers.emplace_back([&bin, &readerBarrier, &q] {
                    readerBarrier.arrive_and_wait();
                    int item = pill;
                    // keep going until the poisoned pill
                    while ((item = to_int(queue_dequeue(q.get()))) != pill) {
                        bin.push_back(item);
                    }
                });
            }

            std::barrier writerBarrier{static_cast<ptrdiff_t>(numWriters)};
            std::vector<std::thread> writers;
            std::atomic<size_t>      numWritten{0};
            while (writers.size() < numWriters) {
                writers.emplace_back([&in, &numWritten, &writerBarrier, &q] {
                    writerBarrier.arrive_and_wait();
                    while (numWritten < in.size()) {
                        queue_enqueue(q.get(), to_ptr(in[numWritten++]));
                    }
                });
            }
            // join the writers
            std::ranges::for_each(writers, &std::thread::join);
            {
                std::vector<QueueItem> pills(numReaders, to_ptr(pill));
                assert(pills.size() == numReaders);
                queue_enqueue_n(q.get(), pills.data(), pills.size());
            }
            // join the readers

            std::ranges::for_each(readers, &std::thread::join);

            std::vector<int> all;
            std::ranges::copy(bins | std::views::join, std::back_inserter(all));
            std::ranges::sort(all);
            EXPECT_THAT(all, testing::ElementsAreArray(in))
                << numReaders << ":" << numWriters;
        }
    }
}
