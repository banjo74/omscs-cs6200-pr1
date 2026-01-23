#ifndef transfer_test_ByteReceiveClient_hpp
#define transfer_test_ByteReceiveClient_hpp

#include "transferclient.h"

#include <cstddef>
#include <vector>

namespace transfer::test {
class ByteReceiveClient {
  public:
    using Data = std::vector<std::byte>;

    ByteReceiveClient(Data s = {});

    ByteReceiveClient(ByteReceiveClient const& o)
        : ByteReceiveClient{o.data_} {}

    ByteReceiveClient(ByteReceiveClient&& o)
        : ByteReceiveClient{std::move(o.data_)} {}

    ReceiveClient* client() {
        return &client_;
    }

    Data data() const {
        return data_;
    }

    void data(Data);

  private:
    ReceiveClient client_;
    Data          data_;
};
} // namespace transfer::test

#endif // include guard
