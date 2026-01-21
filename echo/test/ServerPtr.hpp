
#include "echoserver.h"

#include <memory>

namespace echo {
namespace detail {
struct DestroyServer {
    void operator()(EchoServer* ec) const;
};
} // namespace detail

// Unique pointer wrapper around EchoServer
using ServerPtr = std::unique_ptr<EchoServer, detail::DestroyServer>;

// create a server with the provided information and return it.  If the creation failed for any reason,
// the ServerPtr is unset and the returned status should indicate the nature of the failure.
std::tuple<ServerPtr, EchoServerStatus> create_server(unsigned short port, size_t maximumConnections);
} // namespace echo
