#ifndef transfer_test_FileSourcePtr_hpp
#define transfer_test_FileSourcePtr_hpp

#include "transferserver.h"

#include <filesystem>
#include <memory>

namespace transfer::test {
namespace detail {
struct FileSourceDeleter {
    void operator()(FileTransferSource*) const;
};
} // namespace detail

using FileSourcePtr =
    std::unique_ptr<FileTransferSource, detail::FileSourceDeleter>;

FileSourcePtr create_file_source(std::filesystem::path const&);
} // namespace transfer::test

#endif // include guard
