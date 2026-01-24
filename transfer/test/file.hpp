#ifndef transfer_test_file_hpp
#define transfer_test_file_hpp

#include "Bytes.hpp"

#include <filesystem>

namespace transfer::test {
// write bytes to file.  create the file if necessary.  overwrite the file if it
// exists
void spew(std::filesystem::path const& file, Bytes const& bytes);

// read file and return as a sequence of Bytes
Bytes slurp(std::filesystem::path const& file);

// return the full path to a file name in dir that does not exist at the time
// that this function is called.
std::filesystem::path temp_file(std::filesystem::path const& dir);
} // namespace transfer::test

#endif // include guard
