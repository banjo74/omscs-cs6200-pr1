
#include "ClientPtr.hpp"
#include "ServerPtr.hpp"
#include "block_execute.hpp"
#include "echoclient.h"
#include "echoserver.h"
#include "max_buffer.hpp"
#include "random_seed.hpp"
#include "random_string.hpp"
#include "string_receive_fcn.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits.h>
#include <random>
#include <thread>

using namespace echo::test;

namespace {
unsigned short const default_port = 14757;
}

TEST(Integration, SendAndReceiveOneThread) {
    std::mt19937                   gen{random_seed()};
    std::vector<std::string> const messages{
        "a basic message",
        "a very very very very long message",
        random_string(gen, max_buffer)};

    for (auto const bufferSize :
         {size_t{16}, std::size_t{128}, std::size_t{1024}, max_buffer}) {
        auto [es, _] = create_server(bufferSize, default_port, 5);
        ServerRunner runner{es};
        for (size_t i = 0; i < 1024; ++i) {
            auto [ec, __] = create_client("localhost", default_port);
            for (auto const& m : messages) {
                std::string str;
                EXPECT_EQ(ec_send_and_receive(
                              ec.get(), m.c_str(), string_receive_fcn, &str),
                          EchoClientSuccess);
                EXPECT_EQ(str, m.substr(0, bufferSize));
            }
        }
        EXPECT_THAT(runner.finish(), clean_server_exit());
    }
}

namespace {
template <typename Rng>
auto random_strings(Rng&         rng,
                    size_t const numStrings,
                    size_t const minSize,
                    size_t const maxSize) {
    std::uniform_int_distribution<size_t> sizeDist(minSize, maxSize - 1);
    std::vector<std::string>              out{numStrings};
    std::ranges::generate(
        out, [&rng, &sizeDist] { return random_string(rng, sizeDist(rng)); });
    return out;
}

auto create_clients(unsigned short port, size_t const numClients) {
    std::vector<ClientPtr> out{numClients};
    std::ranges::generate(
        out, [port] { return std::get<0>(create_client("localhost", port)); });
    return out;
}
} // namespace

TEST(Integration, SendAndReceiveManyThreads) {
    std::mt19937 gen{random_seed()};
    auto const   messages   = random_strings(gen, 1024, 1, 1024);
    size_t const numThreads = 16;
    size_t const bufferSize = 1024;

    auto [es, _] = create_server(bufferSize, default_port, numThreads);

    // just one client per thread
    auto clients = create_clients(default_port, numThreads);

    std::vector<std::string> results(messages.size());
    assert(results.size() == messages.size());

    // start the server
    ServerRunner runner{es};

    block_execute(
        messages.size(),
        numThreads,
        [&messages, &clients, &results](auto const i, auto const threadIndex) {
            ec_send_and_receive(clients[threadIndex].get(),
                                messages[i].c_str(),
                                string_receive_fcn,
                                &results[i]);
        });

    EXPECT_THAT(runner.finish(), clean_server_exit());

    for (size_t i = 0; i < messages.size(); ++i) {
        EXPECT_EQ(messages[i].substr(0, bufferSize), results[i])
            << bufferSize << ":" << i;
    }
}
