#ifndef transfer_test_ByteSink_hpp
#define transfer_test_ByteSink_hpp

#include "transferclient.h"

#include <cstddef>
#include <vector>

namespace transfer::test {
class ByteSink {
  public:
    using Data = std::vector<std::byte>;

    ByteSink(Data s = {});

    ByteSink(ByteSink const& o)
        : ByteSink{o.data_} {}

    ByteSink(ByteSink&& o)
        : ByteSink{std::move(o.data_)} {}

    TransferSink* base() {
        return &base_;
    }

    Data data() const {
        return data_;
    }

    void data(Data);

  private:
    TransferSink base_;
    Data         data_;
};
} // namespace transfer::test

#endif // include guard
