#ifndef transfer_test_ByteSink_hpp
#define transfer_test_ByteSink_hpp

#include "Bytes.hpp"
#include "transferclient.h"

#include <cstddef>
#include <vector>

namespace transfer::test {
class ByteSink {
  public:
    ByteSink(Bytes s = {});

    ByteSink(ByteSink const& o)
        : ByteSink{o.data_} {}

    ByteSink(ByteSink&& o)
        : ByteSink{std::move(o.data_)} {}

    TransferSink* base() {
        return &base_;
    }

    Bytes data() const {
        return data_;
    }

    void data(Bytes);

  private:
    TransferSink base_;
    Bytes        data_;
};
} // namespace transfer::test

#endif // include guard
