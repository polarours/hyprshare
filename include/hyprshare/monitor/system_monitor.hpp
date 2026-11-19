#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace hyprshare::monitor {

struct SystemMetrics {
  double cpu_usage = 0;
  double memory_usage = 0;
  double disk_usage = 0;
  std::uint64_t network_bytes_sent = 0;
  std::uint64_t network_bytes_recv = 0;
  std::uint32_t active_connections = 0;
  std::uint32_t active_transfers = 0;
};

struct TransferMetrics {
  std::string session_id;
  std::string source;
  std::string destination;
  std::uint64_t bytes_transferred = 0;
  std::uint64_t total_bytes = 0;
  double speed_mbps = 0;
  std::int64_t elapsed_seconds = 0;
};

using MetricsCallback = std::function<void(const SystemMetrics&)>;

class SystemMonitor {
 public:
  SystemMonitor();
  ~SystemMonitor();

  std::expected<void, std::string> start();
  void stop();

  std::expected<SystemMetrics, std::string> collect_metrics();
  std::vector<TransferMetrics> active_transfers() const;

  void set_metrics_callback(MetricsCallback callback);

  bool is_running() const;

 private:
  void monitor_loop();

  // CPU usage is a delta between two /proc/stat snapshots.
  std::uint64_t last_cpu_total_ = 0;
  std::uint64_t last_cpu_idle_ = 0;
  bool has_cpu_baseline_ = false;

  // Network counters are cumulative; keep the previous reading to report
  // per-second rates is left to the caller, but expose absolute totals here.
  mutable std::mutex mutex_;
  std::atomic<bool> running_{false};
  SystemMetrics current_metrics_;
  std::vector<TransferMetrics> transfers_;
  MetricsCallback callback_;
};

}  // namespace hyprshare::monitor
