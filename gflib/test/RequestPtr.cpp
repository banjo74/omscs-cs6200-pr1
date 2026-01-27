
#include "RequestPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace gf::test {
namespace detail {
void DestroyRequest::operator()(gfcrequest_t* gfc) const {
    gfc_cleanup(&gfc);
}
} // namespace detail

RequestPtr create_request() {
    return RequestPtr{gfc_create()};
}
} // namespace gf::test
