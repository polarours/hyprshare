#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hyprshare::profiling {

struct ProfileEntry {
  std::string name;
  std::int64_t start_time = 0;
  std::int64_t end_time = 0;
  std::int64_t duration_us = 0;
  std::uint64_t bytes_processed = 0;
  double throughput_mbps = 0;
};

struct ProfileReport {
  std::vector<ProfileEntry> entries;
  std::int64_t total_duration_us = 0;
  std::uint64_t total_bytes = 0;
  double avg_throughput_mbps = 0;
};

class Profiler {
 public:
  Profiler();
  ~Profiler();

  void start(const std::string& name);
  void stop(const std::string& name);
  void add_bytes(const std::string& name, std::uint64_t bytes);

  ProfileReport report() const;
  void reset();

  void print_report() const;

 private:
  std::vector<ProfileEntry> entries_;
};

}  // namespace hyprshare::profiling
