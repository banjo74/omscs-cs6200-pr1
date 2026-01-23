
#include "transferserver.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace transfer::test {
namespace detail {
struct DestroyServer {
    void operator()(TransferServer* ec) const;
};
} // namespace detail

// Unique pointer wrapper around TransferServer
using ServerPtr = std::unique_ptr<TransferServer, detail::DestroyServer>;

// create a server with the provided information and return it.  If the creation
// failed for any reason, the ServerPtr is unset and the returned status should
// indicate the nature of the failure.
std::tuple<ServerPtr, TransferServerStatus> create_server(
    unsigned short port,
    size_t         maximumConnections);

/*!
 Constructing a ServerRunner starts a server running on an seperate thread.
 Calling finish on that object closes down the running server and returns the
 resulting status. If the sever is still running when ServerRunner goes out of
 scope, it will attempt to stop the server and join that server's thread.
 */
class ServerRunner {
  public:
    ServerRunner(TransferServer* server, TransferSource* source);

    ServerRunner(ServerPtr& ptr, TransferSource* source)
        : ServerRunner{ptr.get(), source} {}

    ~ServerRunner();

    using Log = std::vector<TransferServerStatus>;

    struct Result {
        TransferServerStatus finalStatus;
        Log                  log;
    };

    Result finish();

  private:
    std::unique_ptr<std::atomic<bool>> stop_;
    std::future<Result>                future_;
};

// EXPECT_THAT(runner.finish(), clean_server_exit())
// requires that the final status be Success and there be no logged errors.
inline auto clean_server_exit() {
    return testing::AllOf(
        testing::Field("finalStatus",
                       &ServerRunner::Result::finalStatus,
                       TransferServerSuccess),
        testing::Field("log", &ServerRunner::Result::log, testing::IsEmpty()));
}
} // namespace transfer::test
