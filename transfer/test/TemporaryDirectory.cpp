
#include "TemporaryDirectory.hpp"

#include <stdlib.h>

namespace transfer::test {
namespace {
std::filesystem::path make_directory() {
    std::string template_{std::filesystem::temp_directory_path() /
                          std::filesystem::path{"testXXXXXX"}};
    if (char* const result = mkdtemp(template_.data())) {
        return {result};
    }
    throw std::runtime_error{"unable to create temporary directory"};
}
} // namespace

TemporaryDirectory::TemporaryDirectory()
    : dir_{make_directory()} {}

TemporaryDirectory::~TemporaryDirectory() {
    if (!dir_.empty()) {
        remove_all(dir_);
    }
}
} // namespace transfer::test
