
#include "ServerPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace echo {
namespace detail {
void DestroyServer::operator()(EchoServer* es) const {
    es_destroy(es);
}
} // namespace detail

std::tuple<ServerPtr, EchoServerStatus> create_server(unsigned short const port, size_t const maximumConnections) {
    // set status to garbage so that we can test that it's doing the right thing.
    EchoServerStatus status = static_cast<EchoServerStatus>(std::numeric_limits<std::underlying_type_t<EchoServerStatus>>::max());
    ServerPtr        ptr{es_create(&status, port, maximumConnections)};
    assert(ptr || status != EchoServerSuccess);
    return {std::move(ptr), status};
}
} // namespace echo
