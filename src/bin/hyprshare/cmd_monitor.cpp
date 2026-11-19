#include "cmd_monitor.hpp"

#include <iostream>
#include <thread>

#include "command_utils.hpp"
#include "hyprshare/monitor/system_monitor.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_monitor(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare monitor <start|stop|status|snapshot>"));
  }

  static hyprshare::monitor::SystemMonitor monitor;

  if (args[1] == "start") {
    auto result = monitor.start();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "System monitor started" << std::endl;

    if (!ctx.json) {
      std::cout << "Press Ctrl+C to stop monitoring." << std::endl;
      while (monitor.is_running()) {
        auto metrics = monitor.collect_metrics();
        if (metrics.has_value()) {
          std::cout << "\rCPU: " << metrics->cpu_usage << "% | "
                    << "Memory: " << metrics->memory_usage << "% | "
                    << "Connections: " << metrics->active_connections << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    return 0;
  }

  if (args[1] == "stop") {
    monitor.stop();
    std::cout << "System monitor stopped\n";
    return 0;
  }

  if (args[1] == "status") {
    if (ctx.json) {
      std::cout << "{\"running\":" << (monitor.is_running() ? "true" : "false") << "}\n";
    } else {
      std::cout << "Monitor: " << (monitor.is_running() ? "running" : "stopped") << "\n";
    }
    return 0;
  }

  if (args[1] == "snapshot") {
    auto metrics = monitor.collect_metrics();
    if (!metrics.has_value()) {
      std::cerr << "error: " << metrics.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"cpu\":" << metrics->cpu_usage
                << ",\"memory\":" << metrics->memory_usage
                << ",\"disk\":" << metrics->disk_usage
                << ",\"network_sent\":" << metrics->network_bytes_sent
                << ",\"network_recv\":" << metrics->network_bytes_recv
                << ",\"connections\":" << metrics->active_connections << "}\n";
    } else {
      std::cout << "CPU Usage: " << metrics->cpu_usage << "%\n";
      std::cout << "Memory Usage: " << metrics->memory_usage << "%\n";
      std::cout << "Disk Usage: " << metrics->disk_usage << "%\n";
      std::cout << "Network Sent: " << metrics->network_bytes_sent << " bytes\n";
      std::cout << "Network Recv: " << metrics->network_bytes_recv << " bytes\n";
      std::cout << "Active Connections: " << metrics->active_connections << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown monitor subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
