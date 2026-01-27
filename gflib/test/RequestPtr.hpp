
#include "../gfclient.h"

#include <memory>

namespace gf::test {
namespace detail {
struct DestroyRequest {
    void operator()(gfcrequest_t* ec) const;
};
} // namespace detail

// Unique pointer wrapper around gfcrequest_t
using RequestPtr = std::unique_ptr<gfcrequest_t, detail::DestroyRequest>;

// Create a client
RequestPtr create_request();

template <typename Return, typename... Arguments>
struct GfcFunctionWrapper {
    GfcFunctionWrapper(Return (*fcn)(gfcrequest_t**, Arguments...))
        : fcn_{fcn} {}

    Return operator()(RequestPtr& ptr, Arguments... args) const {
        auto* p = ptr.get();
        return fcn_(&p, args...);
    }

  private:
    Return (*fcn_)(gfcrequest_t**, Arguments...);
};

template <typename Return, typename... Arguments>
GfcFunctionWrapper(Return (*)(gfcrequest_t**, Arguments...))
    -> GfcFunctionWrapper<Return, Arguments...>;

#define GFC_WRAPPER(FunctionStem)                  \
    auto const FunctionStem = GfcFunctionWrapper { \
        gfc_##FunctionStem                         \
    }

GFC_WRAPPER(set_port);
GFC_WRAPPER(set_server);
GFC_WRAPPER(set_path);
GFC_WRAPPER(set_headerfunc);
GFC_WRAPPER(set_headerarg);
GFC_WRAPPER(set_writearg);
GFC_WRAPPER(set_writefunc);
GFC_WRAPPER(perform);
GFC_WRAPPER(get_status);
GFC_WRAPPER(get_bytesreceived);
GFC_WRAPPER(get_filelen);
} // namespace gf::test
