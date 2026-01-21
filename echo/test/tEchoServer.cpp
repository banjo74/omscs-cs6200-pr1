
#include "ServerPtr.hpp"
#include "echoserver.h"

#include <boost/asio.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits.h>
#include <thread>

using boost::asio::ip::tcp;

namespace {
unsigned short const default_port = 14757;
}

TEST(EchoServer, CreateAndDestroy) {
    auto [es, status] = echo::create_server(default_port, 5);
    EXPECT_TRUE(es);
    EXPECT_EQ(status, EchoServerSuccess);
    EXPECT_EQ(es_port(es.get()), default_port);
}

namespace {
std::thread run_server(EchoServer* es) {
    return std::thread{es_run, es};
}

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

std::string send_receive(boost::asio::io_context& ioContext, EchoServer* es, std::string const& in) {
    auto socket = connect(ioContext, es);
    send(es, socket, in);
    std::vector<char>         buffer(in.size());
    boost::system::error_code error;
    size_t                    received = 0;
    try {
        received = socket.read_some(boost::asio::buffer(buffer.data(), buffer.size()), error);
    } catch (std::exception const& exe) {
        std::cerr << exe.what() << std::endl;
        throw;
    }
    return std::string{buffer.data(), received};
}
} // namespace

TEST(EchoServer, StartStop) {
    boost::asio::io_context ioContext{1};
    auto [es, statusIgnored] = echo::create_server(default_port, 5);
    auto t                   = run_server(es.get());
    es_stop(es.get());
    t.join();
}

TEST(EchoServer, DoIt) {
    boost::asio::io_context ioContext{1};
    auto [es, statusIgnored] = echo::create_server(default_port, 5);
    auto t                   = run_server(es.get());

    std::vector<std::string> messages{"a basic message", "", "a very very very very long message"};
    for (auto const& m : messages) {
        std::string str = send_receive(ioContext, es.get(), m);
        EXPECT_EQ(str, m);
    }

    es_stop(es.get());
    t.join();
}

#if 0
namespace {
enum class ShutdownWhen {
    None,
    AfterAccept,
    AfterReceive
};

std::thread
launch_mock_server(boost::asio::io_context& ioContext, unsigned short const port, ShutdownWhen const shutdown = ShutdownWhen::None) {
    tcp::endpoint const endPoint{tcp::v4(), port};
    auto                acceptor = std::make_shared<tcp::acceptor>(ioContext, endPoint);
    acceptor->listen(1);
    std::thread t{[&ioContext, acceptor = std::move(acceptor), shutdown] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);
        if (shutdown == ShutdownWhen::AfterAccept) {
            return;
        }
        char                      buffer[1024 * 1024];
        boost::system::error_code error;
        auto const                length = socket.read_some(boost::asio::buffer(buffer), error);
        if (shutdown == ShutdownWhen::AfterReceive) {
            return;
        }
        boost::asio::write(socket, boost::asio::buffer(buffer, length));
    }};
    return t;
}

EchoServerStatus expect_no_call(EchoServer* ec, char const* const message) {
    bool called = false;
    auto status = ec_send_and_receive(ec, "some message", [](void* const flag, char const*, size_t) { *static_cast<bool*>(flag) = true; }, &called);
    EXPECT_FALSE(called);
    return status;
}
} // namespace

TEST(EchoServer, SendAndReceive) {
    size_t const bufferSize  = 20;
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, bufferSize);
    boost::asio::io_context ioContext{1};

    std::vector<std::string> messages{"a basic message", "a very very very very long message", std::string{bufferSize, 'c'}};
    for (auto const& m : messages) {
        auto        t = launch_mock_server(ioContext, default_port);
        std::string str;
        EXPECT_EQ(ec_send_and_receive(ec.get(), m.c_str(), [](void* const str, char const* buffer, size_t const n) { static_cast<std::string*>(str)->append(buffer, n); }, &str), EchoServerSuccess);
        EXPECT_EQ(str, m.substr(0, bufferSize));
        t.join();
    }
}

TEST(EchoServer, BadHostName) {
    auto [ec, status] = echo::create_client("unknownhost", default_port, 20);
    EXPECT_THAT(ec, testing::IsNull());
    EXPECT_EQ(status, EchoServerFailedToResolveHost);
}

TEST(EchoServer, CannotConnect) {
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, 20);
    // server not started
    EXPECT_EQ(expect_no_call(ec.get(), "a message"), EchoServerFailedToConnectToSocket);
}

TEST(EchoServer, CannotReceive) {
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, 20);
    boost::asio::io_context ioContext{1};
    auto                    t = launch_mock_server(ioContext, default_port, ShutdownWhen::AfterAccept);
    EXPECT_EQ(expect_no_call(ec.get(), "a message"), EchoServerFailedToReceive);
    t.join();
}

TEST(EchoServer, Message) {
    struct Point {
        EchoServerStatus status;
        char const*      pattern;
    };

    Point const points[] = {
        {EchoServerFailedToResolveHost, "failed.*resolve.*host"},
        {EchoServerFailedToConnectToSocket, "failed.*connect.*socket"},
        {EchoServerFailedToSend, "failed.*send"},
        {EchoServerFailedToReceive, "failed.*receive"},
    };

    for (auto const& point : points) {
        EXPECT_THAT(ec_error_message(point.status), testing::ContainsRegex(point.pattern));
    }
}
#endif
