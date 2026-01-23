
#include "transferclient.h"

#include <memory>

namespace transfer::test {
namespace detail {
struct DestroyClient {
    void operator()(TransferClient* ec) const;
};
} // namespace detail

// Unique pointer wrapper around TransferClient
using ClientPtr = std::unique_ptr<TransferClient, detail::DestroyClient>;

// create a client with the provided information and return it.  If the creation
// failed for any reason, the ClientPtr is unset and the returned status should
// indicate the nature of the failure.
std::tuple<ClientPtr, TransferClientStatus> create_client(
    char const*    serverName,
    unsigned short port);
} // namespace transfer::test
