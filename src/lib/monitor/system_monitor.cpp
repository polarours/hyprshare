#include "hyprshare/monitor/system_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/statvfs.h>
#include <thread>

namespace hyprshare::monitor {

namespace {

struct CpuReading {
  std::uint64_t total = 0;
  std::uint64_t idle = 0;
};

// /proc/stat line 1: cpu user nice system idle iowait irq softirq steal ...
std::optional<CpuReading> read_cpu() {
  std::ifstream stat("/proc/stat");
  if (!stat.is_open()) {
    return std::nullopt;
  }
  std::string line;
  if (!std::getline(stat, line)) {
    return std::nullopt;
  }
  std::istringstream fields(line);
  std::string label;
  fields >> label;
  if (label != "cpu") {
    return std::nullopt;
  }

  CpuReading reading;
  std::uint64_t values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (auto& value : values) {
    if (!(fields >> value)) {
      break;
    }
    reading.total += value;
  }
  // idle is field 4; iowait is field 5. Both count as idle time.
  reading.idle = values[3] + values[4];
  return reading;
}

double clamp_percent(const double value) {
  return std::clamp(value, 0.0, 100.0);
}

// Root filesystem usage from statvfs, which works for any mount point.
double read_disk_usage(const char* path) {
  struct statvfs info {};
  if (statvfs(path, &info) != 0 || info.f_blocks == 0) {
    return 0.0;
  }
  // Prefer f_bavail: the blocks reserved for root are not usable by the user.
  const auto total = static_cast<double>(info.f_blocks);
  const auto available = static_cast<double>(info.f_bavail);
  return clamp_percent(100.0 * (total - available) / total);
}

// /proc/net/dev: each non-loopback interface reports cumulative receive and
// transmit byte counters in columns 2 and 10.
void read_network(std::uint64_t& sent, std::uint64_t& received) {
  for (const char* table : {"/proc/net/dev"}) {
    std::ifstream input(table);
    if (!input.is_open()) {
      continue;
    }
    std::string line;
    std::getline(input, line);  // header: Inter-| Receive | Transmit
    std::getline(input, line);  // header: face |bytes packets ...
    while (std::getline(input, line)) {
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      std::string interface = line.substr(0, colon);
      const auto first = interface.find_first_not_of(" \t");
      if (first != std::string::npos) {
        interface = interface.substr(first);
      }
      if (interface == "lo") {
        continue;
      }
      std::istringstream columns(line.substr(colon + 1));
      std::uint64_t receive_bytes = 0;
      std::uint64_t discard = 0;
      std::uint64_t transmit_bytes = 0;
      if (!(columns >> receive_bytes)) {
        continue;
      }
      // Skip the remaining 7 receive columns to reach the transmit bytes.
      for (int i = 0; i < 7; ++i) {
        columns >> discard;
      }
      if (columns >> transmit_bytes) {
        received += receive_bytes;
        sent += transmit_bytes;
      }
    }
  }
}

// /proc/net/tcp{,6}: state 01 is TCP_ESTABLISHED.
std::uint32_t read_established_connections() {
  std::uint32_t count = 0;
  for (const char* table : {"/proc/net/tcp", "/proc/net/tcp6"}) {
    std::ifstream input(table);
    if (!input.is_open()) {
      continue;
    }
    std::string line;
    std::getline(input, line);  // header
    while (std::getline(input, line)) {
      std::istringstream columns(line);
      std::uint32_t slot = 0;
      std::string local, remote, state;
      if ((columns >> slot >> local >> remote >> state) && state == "01") {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

SystemMonitor::SystemMonitor() {}

SystemMonitor::~SystemMonitor() {
  stop();
}

std::expected<void, std::string> SystemMonitor::start() {
  running_ = true;
  std::thread(&SystemMonitor::monitor_loop, this).detach();
  return {};
}

void SystemMonitor::stop() {
  running_ = false;
}

std::expected<SystemMetrics, std::string> SystemMonitor::collect_metrics() {
  SystemMetrics metrics;

  // Read memory before taking the lock; it is independent of the deltas.
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.is_open()) {
    std::string line;
    std::uint64_t total = 0;
    std::uint64_t available = 0;
    while (std::getline(meminfo, line)) {
      if (line.find("MemTotal:") == 0) {
        total = std::stoull(line.substr(9));
      } else if (line.find("MemAvailable:") == 0) {
        available = std::stoull(line.substr(13));
      }
    }
    if (total > 0) {
      metrics.memory_usage = clamp_percent(
        100.0 * (1.0 - static_cast<double>(available) / static_cast<double>(total)));
    }
  }

  metrics.disk_usage = read_disk_usage("/");

  read_network(metrics.network_bytes_sent, metrics.network_bytes_recv);
  metrics.active_connections = read_established_connections();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics.active_transfers = static_cast<std::uint32_t>(transfers_.size());

    const auto cpu = read_cpu();
    if (cpu.has_value()) {
      if (has_cpu_baseline_ && cpu->total > last_cpu_total_) {
        const auto total_delta = cpu->total - last_cpu_total_;
        const auto idle_delta = cpu->idle > last_cpu_idle_
          ? cpu->idle - last_cpu_idle_ : 0;
        const double busy = 1.0 - static_cast<double>(idle_delta) /
          static_cast<double>(total_delta);
        metrics.cpu_usage = clamp_percent(busy * 100.0);
        last_cpu_total_ = cpu->total;
        last_cpu_idle_ = cpu->idle;
      } else if (!has_cpu_baseline_) {
        // No previous sample exists. Take a short second reading so a single
        // `monitor snapshot` still reports a real usage instead of 0%. 50 ms
        // is enough for the kernel's CPU accounting (10 ms ticks) to move,
        // without making the CLI call feel sluggish.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto cpu2 = read_cpu();
        if (cpu2.has_value() && cpu2->total > cpu->total) {
          const auto total_delta = cpu2->total - cpu->total;
          const auto idle_delta = cpu2->idle > cpu->idle
            ? cpu2->idle - cpu->idle : 0;
          const double busy = 1.0 - static_cast<double>(idle_delta) /
            static_cast<double>(total_delta);
          metrics.cpu_usage = clamp_percent(busy * 100.0);
        }
        const auto& final_reading = cpu2.has_value() ? *cpu2 : *cpu;
        last_cpu_total_ = final_reading.total;
        last_cpu_idle_ = final_reading.idle;
        has_cpu_baseline_ = true;
      }
    }
    current_metrics_ = metrics;
  }

  return metrics;
}

std::vector<TransferMetrics> SystemMonitor::active_transfers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return transfers_;
}

void SystemMonitor::set_metrics_callback(MetricsCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
}

bool SystemMonitor::is_running() const {
  return running_;
}

void SystemMonitor::monitor_loop() {
  while (running_) {
    auto metrics = collect_metrics();
    MetricsCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = callback_;
    }
    if (metrics.has_value() && callback) {
      callback(*metrics);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace hyprshare::monitor
