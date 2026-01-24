// Copyright 2025 The MathWorks, Inc.

#include "FileSinkPtr.hpp"
#include "TemporaryDirectory.hpp"
#include "file.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>

using namespace transfer::test;
using std::filesystem::path;

TEST(FileTransferSink, Basic) {
    TemporaryDirectory const td;
    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{}, Bytes(100, std::byte{0}), random_bytes(gen, 1024 * 1024)};
    for (auto const& bytes : bytess) {
        auto const file    = temp_file(td.dir());
        auto       sinkPtr = create_file_sink(file);
        for (size_t const n :
             {size_t{1}, size_t{3}, size_t{100}, size_t{1024 * 1024}}) {
            TransferSink* sink       = fsink_sink(sinkPtr.get());
            void*         session    = sink_start(sink);
            size_t        numWritten = 0;
            while (numWritten < bytes.size()) {
                auto const toSend = std::min(bytes.size() - numWritten, n);
                auto const sent =
                    sink_send(sink, session, bytes.data() + numWritten, toSend);
                ASSERT_EQ(sent, static_cast<ssize_t>(toSend));
                numWritten += sent;
            }
            sink_finish(sink, session);
            EXPECT_EQ(bytes, slurp(file));
        }
    }
}

TEST(FileTransferSink, Cancel) {
    TemporaryDirectory const td;
    std::mt19937             gen{random_seed()};
    for (size_t const n :
         {size_t{0}, size_t{1}, size_t{3}, size_t{100}, size_t{1024 * 1024}}) {
        for (size_t const m :
             {size_t{1}, size_t{3}, size_t{100}, size_t{1024 * 1024}}) {
            auto const    file    = temp_file(td.dir());
            auto          sinkPtr = create_file_sink(file);
            TransferSink* sink    = fsink_sink(sinkPtr.get());
            void*         session = sink_start(sink);
            for (size_t i = 0; i < n; i += m) {
                auto const bytes = random_bytes(gen, m);
                auto const sent =
                    sink_send(sink, session, bytes.data(), bytes.size());
                ASSERT_EQ(sent, static_cast<ssize_t>(m));
            }
            sink_cancel(sink, session);
            EXPECT_FALSE(exists(file));
        }
    }
}
