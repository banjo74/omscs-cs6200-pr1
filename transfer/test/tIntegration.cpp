
#include "Bytes.hpp"
#include "ClientPtr.hpp"
#include "FileSinkPtr.hpp"
#include "FileSourcePtr.hpp"
#include "ServerPtr.hpp"
#include "TemporaryDirectory.hpp"
#include "block_execute.hpp"
#include "file.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"
#include "transferclient.h"
#include "transferserver.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <limits.h>
#include <random>
#include <thread>

using namespace transfer::test;

namespace {
unsigned short const default_port = 14757;
} // namespace

TEST(Integration, SingleThreaded) {
    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{},
        Bytes(10, std::byte{0}),
        random_bytes(gen, 1024 * 1024),
    };
    TemporaryDirectory const td;

    auto [server, _] = create_server(default_port, 1);
    assert(server);
    for (auto const& bytes : bytess) {
        auto const serverFile = temp_file(td.dir());
        spew(serverFile, bytes);
        auto sourcePtr = create_file_source(serverFile);
        for (size_t j = 0; j < 4; ++j) {
            ServerRunner runner{server, fsource_source(sourcePtr.get())};
            for (size_t k = 0; k < 32; ++k) {
                auto const  toWrite  = temp_file(td.dir());
                FileSinkPtr sinkPtr  = create_file_sink(toWrite);
                auto [clientPtr, __] = create_client("localhost", default_port);
                EXPECT_EQ(
                    tc_receive(clientPtr.get(), fsink_sink(sinkPtr.get())),
                    TransferClientSuccess);
                EXPECT_EQ(slurp(toWrite), bytes);
            }
            EXPECT_THAT(runner.finish(), clean_server_exit());
        }
    }
}

namespace {
auto create_clients(unsigned short port, size_t const numClients) {
    std::vector<ClientPtr> out{numClients};
    std::ranges::generate(
        out, [port] { return std::get<0>(create_client("localhost", port)); });
    return out;
}
} // namespace

TEST(Integration, MultiThreaded) {
    std::mt19937             gen{random_seed()};
    TemporaryDirectory const td;

    auto const bytes      = random_bytes(gen, 1023 * 1024);
    auto const serverFile = temp_file(td.dir());
    spew(serverFile, bytes);

    size_t const numThreads = 16;
    size_t const numRuns    = 1024;

    auto [server, _] = create_server(default_port, numThreads);
    auto sourcePtr   = create_file_source(serverFile);

    auto const clients = create_clients(default_port, numThreads);

    std::vector<std::filesystem::path> writtenFiles;
    std::vector<FileSinkPtr>           sinks;
    for (size_t i = 0; i < numRuns; ++i) {
        writtenFiles.push_back(temp_file(td.dir()));
        sinks.push_back(create_file_sink(writtenFiles.back()));
    }

    ServerRunner runner{server, fsource_source(sourcePtr.get())};
    block_execute(sinks.size(),
                  numThreads,
                  [&clients, &sinks](auto const i, auto const threadIndex) {
                      tc_receive(clients[threadIndex].get(),
                                 fsink_sink(sinks[i].get()));
                  });
    EXPECT_THAT(runner.finish(), clean_server_exit());
    for (auto const& file : writtenFiles) {
        EXPECT_EQ(bytes, slurp(file)) << file;
    }
}
