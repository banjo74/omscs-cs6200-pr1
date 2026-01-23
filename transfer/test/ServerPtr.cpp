
#include "ServerPtr.hpp"

#include <assert.h>
#include <limits>
#include <type_traits>

namespace transfer::test {
namespace detail {
void DestroyServer::operator()(TransferServer* es) const {
    ts_destroy(es);
}
} // namespace detail

std::tuple<ServerPtr, TransferServerStatus> create_server(
    unsigned short const port,
    size_t const         maximumConnections) {
    // set status to garbage so that we can test that it's doing the right
    // thing.
    TransferServerStatus status = static_cast<TransferServerStatus>(
        std::numeric_limits<
            std::underlying_type_t<TransferServerStatus>>::max());
    ServerPtr ptr{ts_create(&status, port, maximumConnections)};
    assert(ptr || status != TransferServerSuccess);
    return {std::move(ptr), status};
}

ServerRunner::ServerRunner(TransferServer* es, TransferSource* source)
    : stop_{std::make_unique<std::atomic<bool>>(false)}
    , future_{std::async(
          std::launch::async,
          [es, source, stopPtr = stop_.get()]() -> Result {
              Log  log;
              auto finalStatus = ts_run(
                  es,
                  source,
                  [](void* const ptr) {
                      return !*static_cast<std::atomic<bool>*>(ptr);
                  },
                  stopPtr,
                  [](void* const ptr, TransferServerStatus const status) {
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
} // namespace transfer::test
