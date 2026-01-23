
#include "ByteSink.hpp"
#include "Bytes.hpp"
#include "ClientPtr.hpp"
#include "random_seed.hpp"
#include "transferclient.h"

#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/version.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits.h>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace transfer::test;

namespace {
unsigned short const default_port = 14757;

template <typename Rng>
Bytes random_bytes(Rng& rng, size_t const s) {
    std::uniform_int_distribution<uint8_t> dist;
    Bytes                                  out(s);
    std::ranges::generate(out, [&dist, &rng] { return std::byte{dist(rng)}; });
    return out;
}
} // namespace

TEST(TransferClient, CreateAndDestroy) {
    auto [ec, status] = create_client("localhost", default_port);
    EXPECT_TRUE(ec);
    EXPECT_EQ(status, TransferClientSuccess);
}

namespace {
std::thread launch_mock_server(boost::asio::io_context& ioContext,
                               unsigned short const     port,
                               Bytes                    toSend) {
    tcp::endpoint const endPoint{tcp::v4(), port};
    auto                acceptor = std::make_shared<tcp::acceptor>(ioContext);
    acceptor->open(endPoint.protocol());
    acceptor->set_option(tcp::acceptor::reuse_address(true));
    acceptor->bind(endPoint);
    acceptor->listen(1);
    std::thread t{[&ioContext,
                   acceptor = std::move(acceptor),
                   toSend   = std::move(toSend)] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);
        boost::asio::write(socket, boost::asio::buffer(toSend));
    }};
    return t;
}
#if 0
TransferClientStatus expect_no_call(TransferClient*   ec,
                                    char const* const message) {
    ByteSink client;
    auto              status = tc_receive(ec, client.client());
    EXPECT_THAT(client.data(), testing::IsEmpty());
    return status;
}
#endif
} // namespace

TEST(TransferClient, Receive) {
    auto [ec, statusIgnored] = create_client("localhost", default_port);
    boost::asio::io_context ioContext{1};

    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{},
        Bytes(10, std::byte{0}),
        random_bytes(gen, 1024 * 1024),
    };

    for (size_t i = 0; i < 1024; ++i) {
        for (auto const& bytes : bytess) {
            auto     t = launch_mock_server(ioContext, default_port, bytes);
            ByteSink sink;
            EXPECT_EQ(tc_receive(ec.get(), sink.base()), TransferClientSuccess);
            EXPECT_EQ(sink.data(), bytes);
            t.join();
        }
    }
}
