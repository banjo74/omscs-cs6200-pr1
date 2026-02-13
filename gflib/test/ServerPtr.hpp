
#include "../gfserver-student.h"
#include "../gfserver.h"
#include "Bytes.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace gf::test {
namespace detail {
struct DestroyServer {
    void operator()(gfserver_t* ec) const;
};
} // namespace detail

// Unique pointer wrapper around gfserver_t
using ServerPtr = std::unique_ptr<gfserver_t, detail::DestroyServer>;

// Create a server
ServerPtr create_server();

// some hackery to get a more c++ish API to the server, this template class is a
// wrapper around a pointer to gfserver function.  The first argument to each
// function is always a pointer to a pointer to a gfserver_t.
// the remaining arguments the return value depend on the function.  CTAD below
// avoids having to spell out these template parameters.
template <typename Return, typename... Arguments>
struct GfsFunctionWrapper {
    GfsFunctionWrapper(Return (*fcn)(gfserver_t**, Arguments...))
        : fcn_{fcn} {}

    Return operator()(gfserver_t* gfs, Arguments... args) const {
        return fcn_(&gfs, args...);
    }

    Return operator()(ServerPtr& ptr, Arguments... args) const {
        return operator()(ptr.get(), args...);
    }

    Return operator()(std::shared_ptr<gfserver_t>& ptr,
                      Arguments... args) const {
        return operator()(ptr.get(), args...);
    }

  private:
    Return (*fcn_)(gfserver_t**, Arguments...);
};

template <typename Return, typename... Arguments>
GfsFunctionWrapper(Return (*)(gfserver_t**, Arguments...))
    -> GfsFunctionWrapper<Return, Arguments...>;

// now, this creates an inline variable that is FunctionStem and points an
// instance of GfsFunctionWrapper.  so something like set_port(ServerPtr,
// port) looks like a function call and it's just calling gfc_set_port under the
// hood.
#define GFS_WRAPPER(FunctionStem)                         \
    inline auto const FunctionStem = GfsFunctionWrapper { \
        gfserver_##FunctionStem                           \
    }

GFS_WRAPPER(set_port);
GFS_WRAPPER(port);
GFS_WRAPPER(set_handler);
GFS_WRAPPER(set_handlerarg);
GFS_WRAPPER(set_maxpending);
GFS_WRAPPER(listen);
GFS_WRAPPER(set_continue_fcn);
GFS_WRAPPER(serve);

/*!
 Constructing a ServerRunner starts a server running on an seperate thread.
 When the runner goes out of scope, it attemts to shut down the server and join
 its thread.
 */
class ServerRunner {
  public:
    ServerRunner(ServerPtr server, std::unordered_map<std::string, Bytes> data);

    ~ServerRunner();

  private:
    std::unique_ptr<std::atomic<bool>> stop_;
    std::future<void>                  future_;
};

} // namespace gf::test
