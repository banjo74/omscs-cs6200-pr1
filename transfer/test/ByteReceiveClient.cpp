
#include "ByteReceiveClient.hpp"

namespace transfer::test {
namespace {
void* start_(void*) {
    return new ByteReceiveClient::Data{};
}

ssize_t send_(void*,
              void* const       sessionData,
              void const* const bytes_,
              size_t const      n) {
    auto* const dataPtr = static_cast<ByteReceiveClient::Data*>(sessionData);
    auto* const bytes   = static_cast<std::byte const*>(bytes_);
    dataPtr->insert(dataPtr->end(), bytes, bytes + n);
    return static_cast<size_t>(n);
}

void cancel_(void*, void* const sessionData) {
    auto* const dataPtr = static_cast<ByteReceiveClient::Data*>(sessionData);
    delete dataPtr;
}

int finish_(void* clientData, void* const sessionData) {
    auto* const dataPtr = static_cast<ByteReceiveClient::Data*>(sessionData);
    static_cast<ByteReceiveClient*>(clientData)->data(std::move(*dataPtr));
    delete dataPtr;
    return 1;
}
} // namespace

ByteReceiveClient::ByteReceiveClient(Data d)
    : data_{std::move(d)} {
    rc_initialize(&client_, start_, send_, cancel_, finish_, this);
}

void ByteReceiveClient::data(Data d) {
    data_ = std::move(d);
}
} // namespace transfer::test
