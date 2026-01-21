
#include "ClientPtr.hpp"
#include "echoclient.h"

#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/version.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits.h>
#include <thread>

using boost::asio::ip::tcp;

namespace {
unsigned short const default_port = 14757;
}

TEST(EchoClient, CreateAndDestroy) {
    auto [ec, status] = echo::create_client("localhost", default_port, 20);
    EXPECT_TRUE(ec);
    EXPECT_EQ(status, EchoClientSuccess);
}

namespace {
enum class ShutdownWhen {
    None,
    AfterAccept,
    AfterReceive
};

std::thread
launch_mock_server(boost::asio::io_context& ioContext, unsigned short const port, size_t const bufferSize, ShutdownWhen const shutdown = ShutdownWhen::None) {
    tcp::endpoint const endPoint{tcp::v4(), port};
    auto                acceptor = std::make_shared<tcp::acceptor>(ioContext);
    acceptor->open(endPoint.protocol());
    acceptor->set_option(tcp::acceptor::reuse_address(true));
    acceptor->bind(endPoint);
    acceptor->listen(1);
    std::thread t{[&ioContext, acceptor = std::move(acceptor), bufferSize, shutdown] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);
        if (shutdown == ShutdownWhen::AfterAccept) {
            return;
        }
        std::vector<char>         buffer(bufferSize);
        boost::system::error_code error;
        size_t                    read = socket.read_some(boost::asio::buffer(buffer, std::size(buffer)), error);
        if (shutdown == ShutdownWhen::AfterReceive) {
            return;
        }
        boost::asio::write(socket, boost::asio::buffer(buffer, read));
    }};
    return t;
}

EchoClientStatus expect_no_call(EchoClient* ec, char const* const message) {
    bool called = false;
    auto status = ec_send_and_receive(ec, "some message", [](void* const flag, char const*, size_t) { *static_cast<bool*>(flag) = true; }, &called);
    EXPECT_FALSE(called);
    return status;
}
} // namespace

TEST(EchoClient, SendAndReceive) {
    size_t const bufferSize  = 20;
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, bufferSize);
    boost::asio::io_context ioContext{1};

    std::vector<std::string> messages{"a basic message", "a very very very very long message", std::string{bufferSize, 'c'}};
    for (auto const& m : messages) {
        auto        t = launch_mock_server(ioContext, default_port, bufferSize);
        std::string str;
        EXPECT_EQ(ec_send_and_receive(ec.get(), m.c_str(), [](void* const str, char const* buffer, size_t const n) { static_cast<std::string*>(str)->append(buffer, n); }, &str), EchoClientSuccess);
        EXPECT_EQ(str, m.substr(0, bufferSize));
        t.join();
    }
}

TEST(EchoClient, BadHostName) {
    auto [ec, status] = echo::create_client("unknownhost", default_port, 20);
    EXPECT_THAT(ec, testing::IsNull());
    EXPECT_EQ(status, EchoClientFailedToResolveHost);
}

TEST(EchoClient, CannotConnect) {
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, 20);
    // server not started
    EXPECT_EQ(expect_no_call(ec.get(), "a message"), EchoClientFailedToConnectToSocket);
}

TEST(EchoClient, CannotReceive) {
    auto [ec, statusIgnored] = echo::create_client("localhost", default_port, 20);
    boost::asio::io_context ioContext{1};
    auto                    t = launch_mock_server(ioContext, default_port, 20, ShutdownWhen::AfterAccept);
    EXPECT_EQ(expect_no_call(ec.get(), "a message"), EchoClientFailedToReceive);
    t.join();
}

TEST(EchoClient, Message) {
    struct Point {
        EchoClientStatus status;
        char const*      pattern;
    };

    Point const points[] = {
        {EchoClientFailedToResolveHost, "failed.*resolve.*host"},
        {EchoClientFailedToConnectToSocket, "failed.*connect.*socket"},
        {EchoClientFailedToSend, "failed.*send"},
        {EchoClientFailedToReceive, "failed.*receive"},
    };

    for (auto const& point : points) {
        EXPECT_THAT(ec_error_message(point.status), testing::ContainsRegex(point.pattern));
    }
}
