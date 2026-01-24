#ifndef transfer_test_TemporaryDirectory_hpp
#define transfer_test_TemporaryDirectory_hpp

#include <filesystem>

namespace transfer::test {
class TemporaryDirectory {
  public:
    TemporaryDirectory();
    TemporaryDirectory(TemporaryDirectory const&)            = delete;
    TemporaryDirectory(TemporaryDirectory&&)                 = default;
    TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&)      = default;
    ~TemporaryDirectory();

    std::filesystem::path dir() const {
        return dir_;
    }

  private:
    std::filesystem::path dir_;
};
} // namespace transfer::test

#endif // include guard
