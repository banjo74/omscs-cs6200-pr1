
#include "ByteSink.hpp"

namespace transfer::test {
namespace {
void* start_(void*) {
    return new Bytes{};
}

ssize_t send_(void*,
              void* const       sessionData,
              void const* const bytes_,
              size_t const      n) {
    auto* const dataPtr = static_cast<Bytes*>(sessionData);
    auto* const bytes   = static_cast<std::byte const*>(bytes_);
    dataPtr->insert(dataPtr->end(), bytes, bytes + n);
    return static_cast<size_t>(n);
}

void cancel_(void*, void* const sessionData) {
    auto* const dataPtr = static_cast<Bytes*>(sessionData);
    delete dataPtr;
}

int finish_(void* clientData, void* const sessionData) {
    auto* const dataPtr = static_cast<Bytes*>(sessionData);
    static_cast<ByteSink*>(clientData)->data(std::move(*dataPtr));
    delete dataPtr;
    return 1;
}
} // namespace

ByteSink::ByteSink(Bytes d)
    : data_{std::move(d)} {
    sink_initialize(&base_, start_, send_, cancel_, finish_, this);
}

void ByteSink::data(Bytes d) {
    data_ = std::move(d);
}
} // namespace transfer::test
