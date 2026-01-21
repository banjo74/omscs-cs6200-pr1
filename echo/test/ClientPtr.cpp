
#include "ClientPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace echo {
namespace detail {
void DestroyClient::operator()(EchoClient* ec) const {
    ec_destroy(ec);
}
} // namespace detail

std::tuple<ClientPtr, EchoClientStatus> create_client(char const* hostName, unsigned short const port, size_t const bufferSize) {
    // set status to garbage so that we can test that it gets written
    EchoClientStatus status = static_cast<EchoClientStatus>(std::numeric_limits<std::underlying_type_t<EchoClientStatus>>::max());
    ClientPtr        ptr{ec_create(&status, hostName, port, bufferSize)};
    assert(ptr || status != EchoClientSuccess);
    return {std::move(ptr), status};
}
} // namespace echo
