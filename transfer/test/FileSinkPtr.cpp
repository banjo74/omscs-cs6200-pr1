
#include "FileSinkPtr.hpp"

namespace transfer::test {
namespace detail {
void FileSinkDeleter::operator()(FileTransferSink* sink) const {
    fsink_destroy(sink);
}
} // namespace detail

FileSinkPtr create_file_sink(std::filesystem::path const& path) {
    return FileSinkPtr{fsink_create(path.c_str())};
}
} // namespace transfer::test
