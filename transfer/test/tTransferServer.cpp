
#include "ByteSource.hpp"
#include "Bytes.hpp"
#include "ServerPtr.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"
#include "transferserver.h"

#include <boost/asio.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <limits.h>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace transfer::test;

namespace {
unsigned short const default_port = 14757;
} // namespace

TEST(TransferServer, CreateAndDestroy) {
    auto [ts, status] = create_server(default_port, 5);
    EXPECT_TRUE(ts);
    EXPECT_EQ(status, TransferServerSuccess);
    EXPECT_EQ(ts_port(ts.get()), default_port);
}

namespace {
tcp::socket connect(boost::asio::io_context& ioContext, TransferServer* ts) {
    tcp::socket   socket(ioContext);
    tcp::resolver resolver(ioContext);
    char          portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", ts_port(ts));
    boost::asio::connect(socket, resolver.resolve("localhost", portStr));
    return socket;
}

Bytes receive(boost::asio::io_context& ioContext, TransferServer* ts) {
    auto                      socket = connect(ioContext, ts);
    boost::system::error_code error;
    Bytes                     out;
    try {
        std::byte                 buffer[1024];
        boost::system::error_code error;
        while (!error) {
            size_t const received = socket.read_some(
                boost::asio::buffer(buffer, std::size(buffer)), error);
            out.insert(out.end(), buffer, buffer + received);
        }
    } catch (std::exception const& exe) {
        std::cerr << exe.what() << std::endl;
        throw;
    }
    return out;
}
} // namespace

TEST(TransferServer, StartStop) {
    boost::asio::io_context ioContext{1};
    auto [ts, statusIgnored] = create_server(default_port, 5);
    ByteSource   source;
    ServerRunner runner{ts, source.base()};
    EXPECT_THAT(runner.finish(), clean_server_exit());
}

TEST(TransferServer, BasicTest) {
    boost::asio::io_context  ioContext{1};
    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{},
        Bytes(10, std::byte{0}),
        random_bytes(gen, 1024 * 1024),
    };
    for (auto const& bytes : bytess) {
        auto [ts, statusIgnored] = create_server(default_port, 5);
        ByteSource   source{bytes};
        ServerRunner runner{ts, source.base()};
        for (size_t i = 0; i < 1; ++i) {
            Bytes const out = receive(ioContext, ts.get());
            EXPECT_EQ(out.size(), bytes.size());
        }
        EXPECT_THAT(runner.finish(), clean_server_exit());
    }
}

TEST(TransferServer, Message) {
    struct Point {
        TransferServerStatus status;
        char const*          pattern;
    };

    Point const points[] = {
        {TransferServerFailedToBind, "failed.*bind"},
        {TransferServerFailedToListen, "failed.*listen"},
        {TransferServerFailedToSelect, "failed.*select"},
        {TransferServerFailedToAccept, "failed.*accept"},
        {TransferServerFailedToRead, "failed.*read"},
        {TransferServerFailedToSend, "failed.*send"},
    };

    for (auto const& point : points) {
        EXPECT_THAT(ts_error_message(point.status),
                    testing::ContainsRegex(point.pattern));
    }
}
