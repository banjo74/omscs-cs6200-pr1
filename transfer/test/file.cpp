#include "file.hpp"

#include <boost/functional/hash.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdio.h>

namespace transfer::test {
void spew(std::filesystem::path const& file, Bytes const& bytes) {
    std::ofstream stream{file, std::ios::out | std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"cannot create " + file.string()};
    }
    stream.write(
        reinterpret_cast<std::ofstream::char_type const*>(bytes.data()),
        bytes.size());
    stream.close();
}

Bytes slurp(std::filesystem::path const& file) {
    std::ifstream stream{file, std::ios::in | std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"cannot read " + file.string()};
    }
    stream.seekg(0, std::ios::end);
    Bytes out(stream.tellg());
    stream.seekg(0);
    stream.read(reinterpret_cast<std::ofstream::char_type*>(out.data()),
                out.size());
    return out;
}

std::filesystem::path temp_file(std::filesystem::path const& dir) {
    static thread_local std::mt19937 gen{std::random_device{}()};
    if (!exists(dir)) {
        throw std::runtime_error{
            "directory passed to temp_file does not exist " + dir.string()};
    }
    std::uniform_int_distribution<unsigned long> dist;
    while (true) {
        char buffer[sizeof(unsigned long) * 2 + 1];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%0*lx",
                      static_cast<int>(sizeof(unsigned long) * 2),
                      dist(gen));
        auto const toTry = dir / std::filesystem::path{buffer};
        if (!exists(toTry)) {
            return toTry;
        }
    }
}
} // namespace transfer::test
