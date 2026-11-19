#pragma once

#include <iostream>
#include <string>

// Shared helpers for the unit test binaries. Each test file is a standalone
// program that reports its own result.
namespace test_support {

inline int failures = 0;

inline void expect(const bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAILED: " << message << "\n";
  }
}

// Prints the summary and returns the exit code of the test binary.
inline int report(const std::string& suite) {
  if (failures == 0) {
    std::cout << suite << ": all tests passed.\n";
  } else {
    std::cerr << suite << ": " << failures << " check(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}

}  // namespace test_support
