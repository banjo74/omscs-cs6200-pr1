
#include "ClientPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace transfer::test {
namespace detail {
void DestroyClient::operator()(TransferClient* ec) const {
    tc_destroy(ec);
}
} // namespace detail

std::tuple<ClientPtr, TransferClientStatus> create_client(
    char const*          hostName,
    unsigned short const port) {
    // set status to garbage so that we can test that it gets written
    TransferClientStatus status = static_cast<TransferClientStatus>(
        std::numeric_limits<
            std::underlying_type_t<TransferClientStatus>>::max());
    ClientPtr ptr{tc_create(&status, hostName, port)};
    assert(ptr || status != TransferClientSuccess);
    return {std::move(ptr), status};
}
} // namespace transfer::test
