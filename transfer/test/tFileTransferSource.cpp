// Copyright 2025 The MathWorks, Inc.

#include "FileSourcePtr.hpp"
#include "TemporaryDirectory.hpp"
#include "file.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>

using namespace transfer::test;
using std::filesystem::path;

TEST(FileTransferSource, Basic) {
    TemporaryDirectory const td;
    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{}, Bytes(100, std::byte{0}), random_bytes(gen, 1024 * 1024)};
    for (auto const& bytes : bytess) {
        auto const file = temp_file(td.dir());
        spew(file, bytes);
        auto sourcePtr = create_file_source(file);
        for (size_t const n :
             {size_t{1}, size_t{3}, size_t{100}, size_t{1024 * 1024}}) {
            Bytes           r;
            Bytes           buffer(n);
            TransferSource* source  = fsource_source(sourcePtr.get());
            void*           session = source_start(source);
            ssize_t         numRead = 0;
            ASSERT_TRUE(session);
            while ((numRead = source_read(source, session, buffer.data(), n)) >
                   0) {
                r.insert(r.end(), buffer.begin(), buffer.begin() + numRead);
            }
            source_finish(source, session);
            EXPECT_EQ(bytes, r);
        }
    }
}
