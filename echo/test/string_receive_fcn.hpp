#ifndef string_receive_fcn_hpp
#define string_receive_fcn_hpp

#include "echoclient.h"

#include <string>

namespace echo::test {
// An implementation of a ReceiveFcn that will append to a string.
// the "data" for the ReceiveFcn should be the address of a std::string
inline void string_receive_fcn(void* strPtr, char const* data, size_t n) {
    static_cast<std::string*>(strPtr)->append(data, n);
}
} // namespace echo::test

#endif // include guard
