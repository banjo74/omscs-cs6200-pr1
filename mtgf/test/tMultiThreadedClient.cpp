
#include "../gfclient-student.h"
#include "../gfclient.h"
#include "Bytes.hpp"
#include "TokenizerPtr.hpp"
#include "random_bytes.hpp"
#include "random_seed.hpp"
#include "terminator.hpp"

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/version.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <limits.h>
#include <list>
#include <random>
#include <thread>

using boost::asio::ip::tcp;
using namespace gf::test;

namespace {
unsigned short const default_port = 14757;
} // namespace

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

class Shutdown {
  public:
    Shutdown() = default;

    Shutdown(tcp::socket& socket)
        : socket_{&socket} {}

    Shutdown(Shutdown&& o)
        : socket_{std::exchange(o.socket_, nullptr)} {}

    ~Shutdown() {
        if (socket_) {
            shutdown(*socket_);
        }
    }

  private:
    tcp::socket* socket_ = nullptr;
};

struct File {
    Bytes  bytes;
    size_t toSend = std::numeric_limits<size_t>::max();
};

using Files = std::unordered_map<std::string, File>;

void send(tcp::socket& socket, Bytes const& bytes, size_t const numBytes) {
    boost::system::error_code error;
    boost::asio::write(
        socket,
        boost::asio::buffer(bytes.data(), std::min(bytes.size(), numBytes)),
        error);
}

void send(tcp::socket& socket, Bytes const& bytes) {
    send(socket, bytes, bytes.size());
}

Bytes to_bytes(std::string const& str) {
    Bytes bytes(str.size());
    memcpy(bytes.data(), str.data(), str.size());
    return bytes;
}

void handle_request(tcp::socket& socket, Files const& files) {
    Shutdown const sd{socket};

    // handle the header
    auto tok = create_tokenizer();
    while (!tok_done(tok.get()) && !tok_invalid(tok.get())) {
        char         buffer[1024];
        size_t const read =
            socket.read_some(boost::asio::buffer(buffer, std::size(buffer)));
        tok_process(tok.get(), buffer, read);
    }

    // unpack teh request
    RequestGet request;
    unpack_request_get(tok.get(), &request);
    auto const iter = files.find(request.path);

    if (iter == files.end()) {
        // file not found, respond so
        send(socket, to_bytes("GETFILE FILE_NOT_FOUND" + terminator));
        return;
    }
    send(socket,
         to_bytes("GETFILE OK " + std::to_string(iter->second.bytes.size()) +
                  terminator));
    send(socket, iter->second.bytes, iter->second.toSend);
}

std::thread launch_mock_server(boost::asio::io_context& ioContext,
                               unsigned short const     port,
                               Files                    files,
                               size_t const             numRequests,
                               size_t const             poolSize) {
    std::thread t{[&ioContext,
                   acceptor = setup_acceptor(ioContext, port),
                   files    = std::move(files),
                   numRequests,
                   poolSize] {
        boost::asio::thread_pool pool{poolSize};
        for (size_t handledRequests = 0; handledRequests < numRequests;
             ++handledRequests) {
            tcp::socket socket{ioContext};
            acceptor->accept(socket);
            boost::asio::post(pool,
                              [socket = std::move(socket), &files]() mutable {
                                  handle_request(socket, files);
                              });
        }
        pool.join();
    }};
    return t;
}

struct ReceivedFile {
    std::string          path;
    std::optional<Bytes> bytes;

    bool operator==(ReceivedFile const&) const = default;
};

std::ostream& operator<<(std::ostream& stream, ReceivedFile const& rf) {
    stream << "{" << rf.path << ", ";
    if (rf.bytes) {
        stream << "some bytes:" << rf.bytes->size();
    } else {
        stream << "unset";
    }
    stream << "}";
    return stream;
}

class ReceivedFiles {
  public:
    using Files     = std::list<ReceivedFile>;
    ReceivedFiles() = default;

    ReceivedFile& add() {
        std::unique_lock<std::mutex> lk{mtx_};
        files_.emplace_back();
        return files_.back();
    }

    Files const& files() const {
        return files_;
    }

  private:
    Files      files_;
    std::mutex mtx_;
};

void init(Sink& sink, ReceivedFiles& files) {
    sink_initialize(
        &sink,
        [](void* rfs, char const* const path) -> void* {
            auto& rf = reinterpret_cast<ReceivedFiles*>(rfs)->add();
            rf.path  = path;
            rf.bytes = Bytes{};
            return &rf;
        },
        [](void*, void* const rf_, void const* const buffer_, size_t const n) {
            auto&       rf     = *reinterpret_cast<ReceivedFile*>(rf_);
            auto* const buffer = static_cast<std::byte const*>(buffer_);
            rf.bytes->insert(rf.bytes->end(), buffer, buffer + n);
            return static_cast<ssize_t>(n);
        },
        [](void*, void* const rf_) {
            auto& rf = *reinterpret_cast<ReceivedFile*>(rf_);
            rf.bytes.reset();
        },
        [](void*, void*) { return 0; },
        &files);
}
} // namespace

TEST(MutliThreadedClient, Basic) {
    std::vector<std::pair<std::string, std::string>> requests;
    Files                                            files;
    std::vector<ReceivedFile>                        expectedReceived;
    std::mt19937                                     gen{random_seed()};

    // should succeed
    for (size_t i = 0; i < 1024; ++i) {
        auto const reqPath   = "/req" + std::to_string(i);
        auto const localPath = "/local" + std::to_string(i);
        requests.emplace_back(reqPath, localPath);
        auto const [iter, _] =
            files.emplace(reqPath, File{.bytes = random_bytes(gen, 2 * i)});
        expectedReceived.push_back(
            ReceivedFile{.path = localPath, .bytes = iter->second.bytes});
    }

    // should succeed (very large)
    for (size_t i = 0; i < 16; ++i) {
        auto const reqPath   = "/lreq" + std::to_string(i);
        auto const localPath = "/llocal" + std::to_string(i);
        requests.emplace_back(reqPath, localPath);
        auto const [iter, _] = files.emplace(
            reqPath, File{.bytes = random_bytes(gen, 1024 * 1024 * 16)});
        expectedReceived.push_back(
            ReceivedFile{.path = localPath, .bytes = iter->second.bytes});
    }

    // fail too short
    for (size_t i = 0; i < 128; ++i) {
        auto const reqPath   = "/sreq" + std::to_string(i);
        auto const localPath = "/slocal" + std::to_string(i);
        requests.emplace_back(reqPath, localPath);
        files.emplace(reqPath,
                      File{.bytes = random_bytes(gen, 2 * i + 4), .toSend = i});
        expectedReceived.push_back(
            ReceivedFile{.path = localPath, .bytes = std::nullopt});
    }

    // fail not found
    for (size_t i = 0; i < 128; ++i) {
        auto const reqPath   = "/freq" + std::to_string(i);
        auto const localPath = "/flocal" + std::to_string(i);
        requests.emplace_back(reqPath, localPath);
        expectedReceived.push_back(
            ReceivedFile{.path = localPath, .bytes = std::nullopt});
    }

    size_t const nThreads = 16;

    std::ranges::shuffle(requests, gen);
    boost::asio::io_context ioContext{1};
    auto                    serverThread = launch_mock_server(
        ioContext, default_port, files, requests.size(), nThreads);

    Sink          sink;
    ReceivedFiles receivedFiles;
    init(sink, receivedFiles);
    auto* mtc =
        mtc_start("localhost", default_port, nThreads, &sink, nullptr, nullptr);
    for (auto const& [reqPath, localPath] : requests) {
        std::string const rp{reqPath};
        std::string const lp{localPath};
        mtc_process(mtc, rp.c_str(), lp.c_str());
    }
    mtc_finish(mtc);
    serverThread.join();

    EXPECT_THAT(receivedFiles.files(),
                testing::UnorderedElementsAreArray(expectedReceived));
}
