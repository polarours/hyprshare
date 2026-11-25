#include "cmd_monitor.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "command_utils.hpp"
#include "hyprshare/monitor/system_monitor.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cli {

namespace {

std::filesystem::path monitor_state_dir(const CommandContext& ctx) {
  return ctx.paths.state_dir / "monitor";
}

std::filesystem::path monitor_pid_file(const CommandContext& ctx) {
  return monitor_state_dir(ctx) / "daemon.pid";
}

// The sampler runs in its own process, so the latest reading is persisted for
// `status` to read. Without this, `monitor start` and `monitor status` were
// separate processes with separate in-memory state and status was always
// "stopped".
std::filesystem::path monitor_snapshot_file(const CommandContext& ctx) {
  return monitor_state_dir(ctx) / "latest.json";
}

std::string self_exe() {
  std::array<char, 4096> buffer{};
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) {
    return "hyprshare";
  }
  return std::string(buffer.data(), static_cast<std::size_t>(length));
}

int read_pid(const std::filesystem::path& path) {
  std::ifstream file(path);
  int pid = -1;
  file >> pid;
  return pid;
}

bool alive(const int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

void write_snapshot(
  const std::filesystem::path& path,
  const hyprshare::monitor::SystemMetrics& metrics) {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return;
  }
  out << "{\"cpu\":" << metrics.cpu_usage << ",\"memory\":" << metrics.memory_usage
      << ",\"disk\":" << metrics.disk_usage
      << ",\"network_sent\":" << metrics.network_bytes_sent
      << ",\"network_recv\":" << metrics.network_bytes_recv
      << ",\"connections\":" << metrics.active_connections << "}\n";
}

std::string read_snapshot(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  std::string line;
  std::getline(in, line);
  return line;
}

std::string metrics_json(const hyprshare::monitor::SystemMetrics& metrics) {
  return "{\"cpu\":" + std::to_string(metrics.cpu_usage) +
         ",\"memory\":" + std::to_string(metrics.memory_usage) +
         ",\"disk\":" + std::to_string(metrics.disk_usage) +
         ",\"network_sent\":" + std::to_string(metrics.network_bytes_sent) +
         ",\"network_recv\":" + std::to_string(metrics.network_bytes_recv) +
         ",\"connections\":" + std::to_string(metrics.active_connections) + "}";
}

}  // namespace

std::expected<int, std::string> handle_monitor(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare monitor <start|stop|status|snapshot>"));
  }

  static hyprshare::monitor::SystemMonitor monitor;

  // Internal entry point: the detached sampler process spawned by `start`.
  if (args[1] == "_daemon") {
    std::error_code error;
    std::filesystem::create_directories(monitor_state_dir(ctx), error);
    auto started = monitor.start();
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
    }
    while (monitor.is_running()) {
      const auto metrics = monitor.collect_metrics();
      if (metrics.has_value()) {
        write_snapshot(monitor_snapshot_file(ctx), *metrics);
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  }

  if (args[1] == "start") {
    const int existing = read_pid(monitor_pid_file(ctx));
    if (alive(existing)) {
      if (ctx.json) {
        std::cout << "{\"status\":\"already_running\",\"pid\":" << existing << "}\n";
      } else {
        std::cout << "System monitor is already running (pid " << existing << ")\n";
      }
      return 0;
    }

    std::error_code error;
    std::filesystem::create_directories(monitor_state_dir(ctx), error);
    std::filesystem::remove(monitor_pid_file(ctx), error);

    const std::string command = util::shell_quote(self_exe()) + " monitor _daemon";
    auto pid = ctx.runner.spawn_detached_shell(command);
    if (!pid.has_value()) {
      std::cerr << "error: " << pid.error() << "\n";
      return 1;
    }
    {
      std::ofstream pid_file(monitor_pid_file(ctx));
      pid_file << *pid << "\n";
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"started\",\"pid\":" << *pid << "}\n";
    } else {
      std::cout << "System monitor started (pid " << *pid << ")\n";
      std::cout << "Readings are written to " << monitor_snapshot_file(ctx).string() << "\n";
    }
    return 0;
  }

  if (args[1] == "stop") {
    const int pid = read_pid(monitor_pid_file(ctx));
    const bool was_running = alive(pid);
    if (was_running) {
      kill(pid, SIGTERM);
    }
    std::error_code error;
    std::filesystem::remove(monitor_pid_file(ctx), error);
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (was_running ? "stopped" : "not_running")
                << "\",\"pid\":" << (was_running ? pid : 0) << "}\n";
    } else {
      std::cout << "System monitor " << (was_running ? "stopped" : "was not running") << "\n";
    }
    return 0;
  }

  if (args[1] == "status") {
    const int pid = read_pid(monitor_pid_file(ctx));
    const bool running = alive(pid);
    const auto snapshot = read_snapshot(monitor_snapshot_file(ctx));

    if (ctx.json) {
      std::cout << "{\"running\":" << (running ? "true" : "false");
      if (running) {
        std::cout << ",\"pid\":" << pid;
      }
      if (!snapshot.empty()) {
        std::cout << ",\"latest\":" << snapshot;
      }
      std::cout << "}\n";
    } else {
      std::cout << "Monitor: " << (running ? "running" : "stopped") << "\n";
      if (running) {
        std::cout << "  pid: " << pid << "\n";
      }
      std::cout << "  latest reading: " << (snapshot.empty() ? "none yet" : snapshot) << "\n";
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
      std::cout << metrics_json(*metrics) << "\n";
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
