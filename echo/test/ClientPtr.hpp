
#include "echoclient.h"

#include <memory>

namespace echo {
namespace detail {
struct DestroyClient {
    void operator()(EchoClient* ec) const;
};
} // namespace detail

// Unique pointer wrapper around EchoClient
using ClientPtr = std::unique_ptr<EchoClient, detail::DestroyClient>;

// create a client with the provided information and return it.  If the creation failed for any reason,
// the ClientPtr is unset and the returned status should indicate the nature of the failure.
std::tuple<ClientPtr, EchoClientStatus> create_client(char const* serverName, unsigned short port, size_t bufferSize);
} // namespace echo
