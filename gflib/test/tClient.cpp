
#include "../gfclient.h"
#include "Bytes.hpp"
#include "RequestPtr.hpp"
#include "TokenizerPtr.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"

#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/version.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <limits.h>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace gf::test;

namespace {
unsigned short const default_port = 14757;
std::string const    term{"\r\n\r\n"};
} // namespace

TEST(Client, CreateAndDestroy) {
    auto req = create_request();
    EXPECT_EQ(get_bytesreceived(req), 0u);
    EXPECT_EQ(get_filelen(req), 0u);
    EXPECT_EQ(get_status(req), 0);
}

namespace {
auto setup_acceptor(boost::asio::io_context& ioContext,
                    unsigned short const     port) {
    tcp::endpoint const endPoint{tcp::v4(), port};
    auto                acceptor = std::make_shared<tcp::acceptor>(ioContext);
    acceptor->open(endPoint.protocol());
    acceptor->set_option(tcp::acceptor::reuse_address(true));
    acceptor->bind(endPoint);
    acceptor->listen(1);
    return acceptor;
}

void shutdown(tcp::socket& socket) {
    boost::system::error_code error;
    socket.shutdown(tcp::socket::shutdown_send, error);
    char buffer[1024];
    while (socket.read_some(boost::asio::buffer(buffer, std::size(buffer)),
                            error) > 0) {
    }
}

RequestPtr setup_request(std::string const&   path,
                         unsigned short const port = default_port) {
    auto req = create_request();
    set_port(req, port);
    set_server(req, "localhost");
    set_path(req, path.c_str());
    return req;
}

void setup_write_fcn(RequestPtr& req, Bytes& sink) {
    set_writefunc(req, [](void* buffer, size_t n, void* br) {
        auto&            bytes    = *reinterpret_cast<Bytes*>(br);
        std::byte const* toInsert = reinterpret_cast<std::byte const*>(buffer);
        bytes.insert(bytes.end(), toInsert, toInsert + n);
    });
    set_writearg(req, &sink);
}

std::thread launch_mock_server(
    boost::asio::io_context& ioContext,
    unsigned short const     port,
    Bytes                    toSend,
    size_t const             chunkSize,
    std::string&             pathRequested,
    size_t const             numToSend = std::numeric_limits<size_t>::max()) {
    std::thread t{[&ioContext,
                   acceptor = setup_acceptor(ioContext, port),
                   toSend   = std::move(toSend),
                   chunkSize,
                   &pathRequested,
                   numToSend] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);
        auto tok = create_tokenizer();
        while (!tok_done(tok.get()) && !tok_invalid(tok.get())) {
            char         buffer[1024];
            size_t const read = socket.read_some(
                boost::asio::buffer(buffer, std::size(buffer)));
            tok_process(tok.get(), buffer, read);
        }
        RequestGet request;
        unpack_request_get(tok.get(), &request);
        pathRequested = request.path;

        std::string const header =
            "GETFILE OK " + std::to_string(toSend.size()) + term;
        Bytes bytes(header.size());
        memcpy(bytes.data(), header.data(), header.size());
        bytes.insert(bytes.end(),
                     toSend.begin(),
                     toSend.begin() + std::min(toSend.size(), numToSend));
        if (numToSend > toSend.size()) {
            // add on some more data that the client should ignore
            for (uint8_t i = 0; i < 128; ++i) {
                bytes.push_back(std::byte{i});
            }
        }
        // now send the bytes in chunks
        for (size_t sent = 0; sent < bytes.size(); sent += chunkSize) {
            using namespace std::chrono_literals;
            boost::system::error_code error;
            Bytes                     localBytes(
                bytes.begin() + sent,
                bytes.begin() + std::min(sent + chunkSize, bytes.size()));
            boost::asio::write(socket, boost::asio::buffer(localBytes), error);
            std::this_thread::sleep_for(5ms);
        }
        shutdown(socket);
    }};
    return t;
}
} // namespace

TEST(Client, Perform) {
    boost::asio::io_context ioContext{1};

    std::mt19937             gen{random_seed()};
    std::vector<Bytes> const bytess{
        Bytes{},
        Bytes(10, std::byte{0}),
        random_bytes(gen, 1025),
    };

    for (size_t const chunkSize : {1024, 5, 3, 1}) {
        for (auto const& bytes : bytess) {
            std::string       pathRequested;
            std::string const pathSent{"/a/b/c/d/d"};
            auto              t = launch_mock_server(
                ioContext, default_port, bytes, chunkSize, pathRequested);
            auto  req = setup_request(pathSent);
            Bytes bytesReceived;
            setup_write_fcn(req, bytesReceived);
            EXPECT_EQ(perform(req), 0);
            EXPECT_EQ(bytesReceived, bytes);
            EXPECT_EQ(get_filelen(req), bytes.size());
            EXPECT_EQ(get_bytesreceived(req), bytes.size());
            EXPECT_EQ(get_status(req), GF_OK);
            t.join();
            EXPECT_EQ(pathRequested, pathSent);
        }
    }
}

namespace {
std::string to_string(gfstatus_t const status) {
    return gfc_strstatus(status);
}

std::thread launch_mock_server_with_bad_status(
    boost::asio::io_context& ioContext,
    unsigned short const     port,
    gfstatus_t               status,
    size_t const             chunkSize) {
    std::thread t{[&ioContext,
                   acceptor = setup_acceptor(ioContext, port),
                   chunkSize,
                   status] {
        tcp::socket socket{ioContext};
        acceptor->accept(socket);

        std::string const header = "GETFILE " + to_string(status) + term;
        Bytes             bytes(header.size());
        memcpy(bytes.data(), header.data(), header.size());
        for (size_t sent = 0; sent < bytes.size(); sent += chunkSize) {
            using namespace std::chrono_literals;
            boost::system::error_code error;
            Bytes                     localBytes(
                bytes.begin() + sent,
                bytes.begin() + std::min(sent + chunkSize, bytes.size()));
            boost::asio::write(socket, boost::asio::buffer(localBytes), error);
            std::this_thread::sleep_for(5ms);
        }
        shutdown(socket);
    }};
    return t;
}
} // namespace

TEST(Client, OtherStatus) {
    boost::asio::io_context ioContext{1};

    for (size_t const chunkSize : {1024, 5, 3, 1}) {
        for (auto const status : {GF_FILE_NOT_FOUND, GF_ERROR, GF_INVALID}) {
            std::string const pathSent{"/a/b/c/d/d"};
            auto              t = launch_mock_server_with_bad_status(
                ioContext, default_port, status, chunkSize);

            auto  req = setup_request(pathSent);
            Bytes bytesReceived;
            setup_write_fcn(req, bytesReceived);

            EXPECT_EQ(perform(req), 0) << to_string(status) << ":" << chunkSize;
            EXPECT_EQ(bytesReceived.size(), 0u)
                << to_string(status) << ":" << chunkSize;
            EXPECT_EQ(get_filelen(req), 0u)
                << to_string(status) << ":" << chunkSize;
            EXPECT_EQ(get_bytesreceived(req), 0u)
                << to_string(status) << ":" << chunkSize;
            EXPECT_EQ(get_status(req), status)
                << to_string(status) << ":" << chunkSize;
            t.join();
        }
    }
}

namespace {
std::thread launch_mock_server_invalid_header(
    boost::asio::io_context& ioContext,
    unsigned short const     port,
    std::string const        header) {
    std::thread t{
        [&ioContext, acceptor = setup_acceptor(ioContext, port), header] {
            boost::system::error_code error;
            tcp::socket               socket{ioContext};
            acceptor->accept(socket);

            Bytes bytes(header.size());
            memcpy(bytes.data(), header.data(), header.size());
            boost::asio::write(socket, boost::asio::buffer(bytes), error);
            shutdown(socket);
        }};
    return t;
}
} // namespace

TEST(Client, InvalidHeader) {
    boost::asio::io_context ioContext{1};
    std::string const       badHeaders[] = {"GET",
                                            "   ",
                                            "",
                                            "GETFILE OK" + term,
                                            "GETFILE GET" + term,
                                            "GETFILE OK /a/b/a/b" + term,
                                            "GETFILE OK 123456" + term.substr(0, 3)};
    for (auto const& header : badHeaders) {
        std::string const pathSent{"/a/b/c/d/d"};
        auto              t =
            launch_mock_server_invalid_header(ioContext, default_port, header);

        auto  req = setup_request(pathSent);
        Bytes bytesReceived;
        setup_write_fcn(req, bytesReceived);

        EXPECT_EQ(perform(req), -1) << header;
        EXPECT_EQ(get_filelen(req), 0u) << header;
        EXPECT_EQ(bytesReceived.size(), 0u) << header;
        EXPECT_EQ(get_bytesreceived(req), 0u) << header;
        EXPECT_EQ(get_status(req), GF_INVALID);
        t.join();
    }
}

TEST(Client, EarlyShutdown) {
    struct Point {
        Bytes  toSend;
        size_t numToSend;
    };

    std::mt19937 gen{random_seed()};
    Point const  points[] = {
        {random_bytes(gen, 2), 1},
    };

    boost::asio::io_context ioContext{1};

    for (size_t const chunkSize : {1024, 5, 3, 1}) {
        for (auto const& point : points) {
            std::string const pathSent{"/a/b/c/d/d"};
            std::string       pathRequested;
            auto              t = launch_mock_server(ioContext,
                                        default_port,
                                        point.toSend,
                                        chunkSize,
                                        pathRequested,
                                        point.numToSend);

            auto  req = setup_request(pathSent);
            Bytes bytesReceived;
            setup_write_fcn(req, bytesReceived);

            EXPECT_EQ(perform(req), -1);
            EXPECT_EQ(get_filelen(req), point.toSend.size());
            EXPECT_EQ(get_bytesreceived(req), point.numToSend);
            EXPECT_EQ(bytesReceived,
                      Bytes(point.toSend.begin(),
                            point.toSend.begin() + point.numToSend));
            EXPECT_EQ(get_status(req), GF_OK);
            t.join();
        }
    }
}
