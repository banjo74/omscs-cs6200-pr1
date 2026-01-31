#include "../gf-student.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iterator>
#include <ranges>
#include <vector>

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

auto ints(int const n) {
    return ints(-n, n);
}
} // namespace

TEST(WorkerPool, Cascading) {
    for (size_t numWorkers1 : {1, 2, 64}) {
        for (size_t numWorkers2 : {1, 2, 64}) {
            using Bin         = std::vector<int>;
            WorkerPool* pool2 = wp_start(
                numWorkers2,
                [](void* value, void* bin_) {
                    auto* bin = reinterpret_cast<Bin*>(bin_);
                    bin->emplace_back(to_int(value));
                },
                [](void*) -> void* { return new Bin{}; },
                NULL);

            WorkerPool* pool1 = wp_start(
                numWorkers1,
                [](void* value, void* pool_) {
                    auto* pool = reinterpret_cast<WorkerPool*>(pool_);
                    wp_add_task(pool, value);
                },
                [](void* pool_) { return pool_; },
                pool2);

            auto const in = ints(1024 * 128);
            for (auto const i : in) {
                wp_add_task(pool1, to_ptr(i));
            }
            wp_finish(pool1, NULL, NULL);

            std::vector<int> all;
            wp_finish(
                pool2,
                [](void* bin_, void* all_) {
                    auto* const bin = reinterpret_cast<Bin*>(bin_);
                    auto* const all = reinterpret_cast<std::vector<int>*>(all_);
                    all->insert(all->end(), bin->begin(), bin->end());
                    delete bin;
                },
                &all);

            std::ranges::sort(all);
            EXPECT_THAT(all, testing::ElementsAreArray(in))
                << numWorkers1 << ":" << numWorkers2;
        }
    }
}
