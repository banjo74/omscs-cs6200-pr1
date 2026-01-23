
#include "ClientPtr.hpp"
#include "echoclient.h"
#include "max_buffer.hpp"
#include "random_seed.hpp"
#include "random_string.hpp"
#include "string_receive_fcn.hpp"

#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/version.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits.h>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace echo::test;

namespace {
unsigned short const default_port = 14757;
}

TEST(EchoClient, CreateAndDestroy) {
    auto [ec, status] = create_client("localhost", default_port);
    EXPECT_TRUE(ec);
    EXPECT_EQ(status, EchoClientSuccess);
}

namespace {
enum class ShutdownWhen { None, AfterAccept, AfterReceive };

std::thread launch_mock_server(
    boost::asio::io_context& ioContext,
    unsigned short const     port,
    ShutdownWhen const       shutdown = ShutdownWhen::None) {
    tcp::endpoint const endPoint{tcp::v4(), port};
    auto                acceptor = std::make_shared<tcp::acceptor>(ioContext);
    acceptor->open(endPoint.protocol());
    acceptor->set_option(tcp::acceptor::reuse_address(true));
    acceptor->bind(endPoint);
    acceptor->listen(1);
    std::thread t{[&ioContext, acceptor = std::move(acceptor), shutdown] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);
        if (shutdown == ShutdownWhen::AfterAccept) {
            return;
        }
        char         buffer[max_buffer];
        size_t const read =
            socket.read_some(boost::asio::buffer(buffer, std::size(buffer)));
        if (shutdown == ShutdownWhen::AfterReceive) {
            return;
        }
        boost::asio::write(socket, boost::asio::buffer(buffer, read));
    }};
    return t;
}

EchoClientStatus expect_no_call(EchoClient* ec, char const* const message) {
    bool called = false;
    auto status = ec_send_and_receive(
        ec,
        "some message",
        [](void* const flag, char const*, size_t) {
            *static_cast<bool*>(flag) = true;
        },
        &called);
    EXPECT_FALSE(called);
    return status;
}
} // namespace

TEST(EchoClient, SendAndReceive) {
    auto [ec, statusIgnored] = create_client("localhost", default_port);
    boost::asio::io_context ioContext{1};

    std::mt19937                   gen{random_seed()};
    std::vector<std::string> const messages{
        "a basic message",
        "a very very very very long message",
        random_string(gen, max_buffer)};

    for (size_t i = 0; i < 1024; ++i) {
        for (auto const& m : messages) {
            auto        t = launch_mock_server(ioContext, default_port);
            std::string str;
            EXPECT_EQ(ec_send_and_receive(
                          ec.get(), m.c_str(), string_receive_fcn, &str),
                      EchoClientSuccess);
            EXPECT_EQ(str, m);
            t.join();
        }
    }
}

TEST(EchoClient, BadHostName) {
    auto [ec, status] = create_client("unknownhost", default_port);
    EXPECT_THAT(ec, testing::IsNull());
    EXPECT_EQ(status, EchoClientFailedToResolveHost);
}

TEST(EchoClient, CannotConnect) {
    auto [ec, statusIgnored] = create_client("localhost", default_port);
    // server not started
    EXPECT_EQ(expect_no_call(ec.get(), "a message"),
              EchoClientFailedToConnectToSocket);
}

TEST(EchoClient, CannotReceive) {
    auto [ec, statusIgnored] = create_client("localhost", default_port);
    boost::asio::io_context ioContext{1};
    auto                    t =
        launch_mock_server(ioContext, default_port, ShutdownWhen::AfterAccept);
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
        EXPECT_THAT(ec_error_message(point.status),
                    testing::ContainsRegex(point.pattern));
    }
}
