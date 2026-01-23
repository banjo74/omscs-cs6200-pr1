
#include "ServerPtr.hpp"
#include "echoserver.h"
#include "max_buffer.hpp"
#include "random_seed.hpp"
#include "random_string.hpp"

#include <boost/asio.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <limits.h>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace echo::test;

namespace {
unsigned short const default_port = 14757;
}

TEST(EchoServer, CreateAndDestroy) {
    auto [es, status] = create_server(20, default_port, 5);
    EXPECT_TRUE(es);
    EXPECT_EQ(status, EchoServerSuccess);
    EXPECT_EQ(es_port(es.get()), default_port);
}

namespace {
tcp::socket connect(boost::asio::io_context& ioContext, EchoServer* es) {
    tcp::socket   socket(ioContext);
    tcp::resolver resolver(ioContext);
    char          portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", es_port(es));
    boost::asio::connect(socket, resolver.resolve("localhost", portStr));
    return socket;
}

void send(EchoServer const* es, tcp::socket& socket, std::string msg) {
    boost::asio::write(socket, boost::asio::buffer(msg.data(), msg.size()));
}

std::string send_receive(boost::asio::io_context& ioContext,
                         EchoServer*              es,
                         std::string const&       in) {
    auto socket = connect(ioContext, es);
    send(es, socket, in);
    std::vector<char>         buffer(in.size());
    boost::system::error_code error;
    size_t                    received = 0;
    try {
        received = socket.read_some(
            boost::asio::buffer(buffer.data(), buffer.size()), error);
    } catch (std::exception const& exe) {
        std::cerr << exe.what() << std::endl;
        throw;
    }
    return std::string{buffer.data(), received};
}
} // namespace

TEST(EchoServer, StartStop) {
    boost::asio::io_context ioContext{1};
    auto [es, statusIgnored] = create_server(20, default_port, 5);
    ServerRunner runner{es};
    EXPECT_THAT(runner.finish(), clean_server_exit());
}

TEST(EchoServer, BasicTest) {
    boost::asio::io_context ioContext{1};
    std::mt19937            gen{random_seed()};
    for (auto const bufferSize :
         {size_t{16}, std::size_t{128}, std::size_t{1024}, max_buffer}) {
        std::vector<std::string> const messages{
            "1",
            "a basic message",
            "",
            "a very very very very long message",
            random_string(gen, bufferSize),
            random_string(gen, bufferSize + 1),
            random_string(gen, max_buffer)};
        auto [es, statusIgnored] = create_server(bufferSize, default_port, 5);
        ServerRunner runner{es};
        for (size_t i = 0; i < 1024; ++i) {
            for (auto const& m : messages) {
                std::string const str = send_receive(ioContext, es.get(), m);
                EXPECT_EQ(str, m.substr(0, bufferSize));
            }
        }
        EXPECT_THAT(runner.finish(), clean_server_exit());
    }
}

TEST(EchoServer, Message) {
    struct Point {
        EchoServerStatus status;
        char const*      pattern;
    };

    Point const points[] = {
        {EchoServerFailedToBind, "failed.*bind"},
        {EchoServerFailedToListen, "failed.*listen"},
        {EchoServerFailedToSelect, "failed.*select"},
        {EchoServerFailedToAccept, "failed.*accept"},
        {EchoServerFailedToRead, "failed.*read"},
        {EchoServerFailedToSend, "failed.*send"},
    };

    for (auto const& point : points) {
        EXPECT_THAT(es_error_message(point.status),
                    testing::ContainsRegex(point.pattern));
    }
}
