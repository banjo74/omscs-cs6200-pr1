
#include "FileSourcePtr.hpp"

namespace transfer::test {
namespace detail {
void FileSourceDeleter::operator()(FileTransferSource* source) const {
    fsource_destroy(source);
}
} // namespace detail

FileSourcePtr create_file_source(std::filesystem::path const& path) {
    return FileSourcePtr{fsource_create(path.c_str())};
}
} // namespace transfer::test
