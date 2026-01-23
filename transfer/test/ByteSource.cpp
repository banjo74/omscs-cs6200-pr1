
#include "ByteSource.hpp"

#include <string.h>

namespace transfer::test {
namespace {
void* start_(void*) {
    return new size_t{0};
}

ssize_t read_(void* const  sourceData,
              void* const  sessionData,
              void* const  bytes_,
              size_t const n) {
    auto const&  source = *static_cast<ByteSource const*>(sourceData);
    auto&        index  = *static_cast<size_t*>(sessionData);
    auto* const  bytes  = static_cast<std::byte*>(bytes_);
    size_t const toRead = std::min(n, source.data().size() - index);
    memcpy(bytes, source.data().data(), toRead);
    index += toRead;
    return toRead;
}

int finish_(void*, void* const sessionData) {
    delete static_cast<size_t*>(sessionData);
    return 1;
}
} // namespace

ByteSource::ByteSource(Data d)
    : data_{std::move(d)} {
    source_initialize(&base_, start_, read_, finish_, this);
}
} // namespace transfer::test
