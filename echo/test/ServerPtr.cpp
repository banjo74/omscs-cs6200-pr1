
#include "ServerPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace echo::test {
namespace detail {
void DestroyServer::operator()(EchoServer* es) const {
    es_destroy(es);
}
} // namespace detail

std::tuple<ServerPtr, EchoServerStatus> create_server(
    size_t const         maxMessageLength,
    unsigned short const port,
    size_t const         maximumConnections) {
    // set status to garbage so that we can test that it's doing the right
    // thing.
    EchoServerStatus status = static_cast<EchoServerStatus>(
        std::numeric_limits<std::underlying_type_t<EchoServerStatus>>::max());
    ServerPtr ptr{
        es_create(&status, maxMessageLength, port, maximumConnections)};
    assert(ptr || status != EchoServerSuccess);
    return {std::move(ptr), status};
}

ServerRunner::ServerRunner(EchoServer* es)
    : stop_{std::make_unique<std::atomic<bool>>(false)}
    , future_{std::async(
          std::launch::async,
          [es, stopPtr = stop_.get()]() -> Result {
              Log  log;
              auto finalStatus = es_run(
                  es,
                  [](void* const ptr) {
                      return !*static_cast<std::atomic<bool>*>(ptr);
                  },
                  stopPtr,
                  [](void* const ptr, EchoServerStatus const status) {
                      static_cast<Log*>(ptr)->emplace_back(status);
                  },
                  &log);
              return Result{.finalStatus = finalStatus, .log = std::move(log)};
          })} {}

ServerRunner::~ServerRunner() {
    if (future_.valid()) {
        *stop_ = true;
        future_.wait();
    }
}

ServerRunner::Result ServerRunner::finish() {
    *stop_         = true;
    auto const out = future_.get();
    return out;
}
} // namespace echo::test
