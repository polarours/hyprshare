#include <chrono>
#include <thread>

#include "hyprshare/monitor/system_monitor.hpp"
#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  hyprshare::monitor::SystemMonitor monitor;

  expect(!monitor.is_running(), "a fresh monitor should be stopped");
  expect(monitor.active_transfers().empty(), "a fresh monitor should have no transfers");

  // The first call establishes the CPU baseline; it must still succeed.
  const auto first = monitor.collect_metrics();
  expect(first.has_value(), "collecting metrics should succeed");
  if (!first.has_value()) {
    return 1;
  }

  // Percentages must stay within their valid range.
  expect(first->cpu_usage >= 0.0 && first->cpu_usage <= 100.0,
    "CPU usage should be a percentage");
  expect(first->memory_usage >= 0.0 && first->memory_usage <= 100.0,
    "memory usage should be a percentage");
  expect(first->disk_usage >= 0.0 && first->disk_usage <= 100.0,
    "disk usage should be a percentage");
  expect(first->disk_usage > 0.0,
    "the root filesystem should report non-zero usage on this machine");

  // Network counters are cumulative byte totals; they cannot be negative.
  expect(first->network_bytes_sent > 0 || first->network_bytes_recv > 0,
    "at least one network interface should have seen traffic");

  // A later sample uses the delta baseline and stays in range.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto second = monitor.collect_metrics();
  expect(second.has_value(), "a second sample should succeed");
  if (second.has_value()) {
    expect(second->cpu_usage >= 0.0 && second->cpu_usage <= 100.0,
      "the delta-based CPU usage should stay a percentage");
  }

  return test_support::report("monitor");
}
