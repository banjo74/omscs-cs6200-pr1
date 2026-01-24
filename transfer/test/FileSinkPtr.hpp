#ifndef transfer_test_FileSinkPtr_hpp
#define transfer_test_FileSinkPtr_hpp

#include "transferclient.h"

#include <filesystem>
#include <memory>

namespace transfer::test {
namespace detail {
struct FileSinkDeleter {
    void operator()(FileTransferSink*) const;
};
} // namespace detail

using FileSinkPtr = std::unique_ptr<FileTransferSink, detail::FileSinkDeleter>;

FileSinkPtr create_file_sink(std::filesystem::path const&);
} // namespace transfer::test

#endif // include guard
