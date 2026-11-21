#include "hyprshare/profiling/profiler.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace hyprshare::profiling {

Profiler::Profiler() {}

Profiler::~Profiler() {}

void Profiler::start(const std::string& name) {
  ProfileEntry entry;
  entry.name = name;
  entry.start_time = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  entries_.push_back(entry);
}

void Profiler::stop(const std::string& name) {
  for (auto& entry : entries_) {
    if (entry.name == name && entry.end_time == 0) {
      entry.end_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
      entry.duration_us = entry.end_time - entry.start_time;
      break;
    }
  }
}

void Profiler::add_bytes(const std::string& name, const std::uint64_t bytes) {
  for (auto& entry : entries_) {
    if (entry.name == name && entry.end_time == 0) {
      entry.bytes_processed += bytes;
      // A running entry has no duration_us yet; measure elapsed time so the
      // throughput is live instead of staying at zero until stop().
      const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
      const auto elapsed_us = entry.duration_us > 0
        ? entry.duration_us
        : std::max<std::int64_t>(1, now - entry.start_time);
      entry.throughput_mbps = static_cast<double>(entry.bytes_processed) /
        (static_cast<double>(elapsed_us) * 1024.0 * 1024.0);
      break;
    }
  }
}

ProfileReport Profiler::report() const {
  ProfileReport report;
  report.entries = entries_;

  for (const auto& entry : entries_) {
    report.total_duration_us += entry.duration_us;
    report.total_bytes += entry.bytes_processed;
  }

  if (report.total_duration_us > 0) {
    report.avg_throughput_mbps = static_cast<double>(report.total_bytes) /
      (static_cast<double>(report.total_duration_us) * 1024.0 * 1024.0);
  }

  return report;
}

void Profiler::reset() {
  entries_.clear();
}

void Profiler::print_report() const {
  const auto report = this->report();

  std::cout << "Performance Profile Report\n";
  std::cout << "==========================\n";
  std::cout << "Total Duration: " << report.total_duration_us / 1000 << " ms\n";
  std::cout << "Total Bytes: " << report.total_bytes << " bytes\n";
  std::cout << "Avg Throughput: " << report.avg_throughput_mbps << " MB/s\n\n";

  for (const auto& entry : report.entries) {
    std::cout << entry.name << ": " << entry.duration_us / 1000 << " ms, "
              << entry.bytes_processed << " bytes, "
              << entry.throughput_mbps << " MB/s\n";
  }
}

}  // namespace hyprshare::profiling
