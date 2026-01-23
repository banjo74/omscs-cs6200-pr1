#ifndef transfer_test_ByteSource_hpp
#define transfer_test_ByteSource_hpp

#include "transferserver.h"

#include <cstddef>
#include <vector>

namespace transfer::test {
class ByteSource {
  public:
    using Data = std::vector<std::byte>;

    ByteSource(Data s = {});

    ByteSource(ByteSource const& o)
        : ByteSource{o.data_} {}

    ByteSource(ByteSource&& o)
        : ByteSource{std::move(o.data_)} {}

    TransferSource* base() {
        return &base_;
    }

    Data const& data() const {
        return data_;
    }

  private:
    TransferSource base_;
    Data           data_;
};
} // namespace transfer::test

#endif // include guard
