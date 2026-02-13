
#include "ServerPtr.hpp"

#include "../gfserver-student.h"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace gf::test {
namespace detail {
void DestroyServer::operator()(gfserver_t* gfc) const {
    gfserver_destroy(&gfc);
}
} // namespace detail

ServerPtr create_server() {
    return ServerPtr{gfserver_create()};
}
} // namespace gf::test
