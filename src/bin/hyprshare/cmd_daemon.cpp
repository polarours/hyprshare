#include "cmd_daemon.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "command_registry.hpp"
#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/daemon/ipc.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cli {

namespace {

// Sends a command to the daemon and reports what it answered.
std::expected<hyprshare::daemon::IpcResponse, std::string> ask(
  const CommandContext& ctx,
  const std::string& command,
  std::unordered_map<std::string, std::string> args = {}) {
  hyprshare::daemon::IpcRequest request;
  request.command = command;
  request.args = std::move(args);

  return hyprshare::daemon::send_request(
    hyprshare::daemon::daemon_socket_path(ctx.paths), request);
}

std::string result_value(
  const hyprshare::daemon::IpcResponse& response,
  const std::string& key) {
  const auto found = response.result.find(key);
  return found == response.result.end() ? "" : found->second;
}

std::filesystem::path daemon_pid_file(const CommandContext& ctx) {
  return ctx.paths.state_dir / "daemon" / "daemon.pid";
}

int read_daemon_pid(const CommandContext& ctx) {
  std::ifstream file(daemon_pid_file(ctx));
  int pid = -1;
  file >> pid;
  return pid;
}

bool daemon_alive(const int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

bool daemon_answers(const CommandContext& ctx) {
  return ask(ctx, "daemon.ping").has_value();
}

// `hyprshared` ships with the package; find it beside this binary (dev build)
// or in the usual install locations.
std::filesystem::path find_daemon_binary() {
  std::vector<std::filesystem::path> candidates = {
    "/usr/bin/hyprshared",
    "/usr/local/bin/hyprshared",
  };
  // Also look next to this CLI and up its ancestors (the dev build keeps the
  // daemon in the same `build/` directory as the CLI).
  std::array<char, 4096> exe_buffer{};
  const ssize_t exe_length = readlink("/proc/self/exe", exe_buffer.data(), exe_buffer.size() - 1);
  std::filesystem::path dir =
    exe_length > 0 ? std::filesystem::path(std::string(exe_buffer.data(),
                                                       static_cast<std::size_t>(exe_length)))
                         .parent_path()
                   : std::filesystem::path();
  for (int level = 0; level < 4 && !dir.empty(); ++level) {
    candidates.push_back(dir / "hyprshared");
    candidates.push_back(dir / "build" / "hyprshared");
    dir = dir.parent_path();
  }
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

}  // namespace

std::expected<int, std::string> handle_daemon(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare daemon <start|status|stop|inbox list|inbox start|inbox stop>"));
  }

  if (args[1] == "start") {
    if (daemon_answers(ctx)) {
      const int existing = read_daemon_pid(ctx);
      if (ctx.json) {
        std::cout << "{\"status\":\"already_running\",\"pid\":" << existing << "}\n";
      } else {
        std::cout << "Daemon is already running (pid " << existing << ")\n";
      }
      return 0;
    }

    const auto binary = find_daemon_binary();
    if (binary.empty()) {
      return std::unexpected("hyprshared not found next to this CLI or in /usr/bin");
    }

    std::error_code error;
    std::filesystem::create_directories(daemon_pid_file(ctx).parent_path(), error);
    auto pid = ctx.runner.spawn_detached_shell(util::shell_quote(binary.string()));
    if (!pid.has_value()) {
      return std::unexpected("failed to start hyprshared: " + pid.error());
    }
    {
      std::ofstream pid_file(daemon_pid_file(ctx));
      pid_file << *pid << "\n";
    }

    // Wait briefly for the IPC socket so `start` only reports success when the
    // daemon is genuinely answering.
    bool answering = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
      if (daemon_answers(ctx)) {
        answering = true;
        break;
      }
      if (!daemon_alive(*pid)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!answering) {
      std::filesystem::remove(daemon_pid_file(ctx), error);
      return std::unexpected("hyprshared started (pid " + std::to_string(*pid) +
                             ") but did not answer on the IPC socket");
    }

    const auto socket = hyprshare::daemon::daemon_socket_path(ctx.paths);
    if (ctx.json) {
      std::cout << "{\"status\":\"started\",\"pid\":" << *pid
                << ",\"socket\":\"" << socket.string() << "\"}\n";
    } else {
      std::cout << "Daemon started (pid " << *pid << ")\n";
      std::cout << "  Socket: " << socket << "\n";
    }
    return 0;
  }

  if (args[1] == "status") {
    const bool running = daemon_answers(ctx);
    const int pid = read_daemon_pid(ctx);
    const auto socket = hyprshare::daemon::daemon_socket_path(ctx.paths);
    if (!running) {
      std::error_code error;
      std::filesystem::remove(daemon_pid_file(ctx), error);
    }

    if (ctx.json) {
      std::cout << "{\"running\":" << (running ? "true" : "false")
                << ",\"socket\":\"" << socket.string() << "\"";
      if (running && pid > 0) {
        std::cout << ",\"pid\":" << pid;
      }
      std::cout << "}\n";
    } else {
      std::cout << "Daemon: " << (running ? "running" : "not running") << "\n";
      std::cout << "  Socket: " << socket << "\n";
      if (running && pid > 0) {
        std::cout << "  pid: " << pid << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "stop") {
    const auto response = ask(ctx, "daemon.stop");
    if (!response.has_value()) {
      std::cerr << "error: " << response.error() << "\n";
      return 1;
    }
    std::error_code cleanup;
    // daemon.stop is asynchronous: wait until the socket actually stops
    // answering so a following `daemon status` is consistent immediately.
    bool stopped = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
      if (!daemon_answers(ctx)) {
        stopped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (stopped) {
      std::filesystem::remove(daemon_pid_file(ctx), cleanup);
    }
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (stopped ? "stopped" : "stop_requested") << "\"}\n";
    } else {
      std::cout << (stopped ? "Daemon stopped\n" : "Daemon asked to stop\n");
    }
    return 0;
  }

  if (args[1] == "inbox" && args.size() >= 3) {
    if (args[2] == "list") {
      const auto response = ask(ctx, "inbox.list_listeners");
      if (!response.has_value()) {
        std::cerr << "error: " << response.error() << "\n";
        return 1;
      }

      const auto count_text = result_value(*response, "count");
      const long count = count_text.empty() ? 0 : std::stol(count_text);
      if (ctx.json) {
        std::cout << "{\"count\":" << count << "}\n";
      } else if (count == 0) {
        std::cout << "The daemon holds no inbox listeners.\n";
      } else {
        std::cout << "The daemon holds " << count << " inbox listener(s).\n";
        const auto details = result_value(*response, "listeners");
        if (!details.empty()) {
          std::cout << details << "\n";
        }
      }
      return 0;
    }

    if (args[2] == "start") {
      const auto host = take_option(args, "--host");
      const auto port = take_option(args, "--port");
      const auto destination = take_option(args, "--to");
      const auto count = take_option(args, "--count");
      const auto timeout = take_option(args, "--timeout");
      const auto max_bytes = take_option(args, "--max-bytes");
      const auto token = take_option(args, "--token");
      const bool announce = take_flag(args, "--announce");

      std::unordered_map<std::string, std::string> request_args;
      request_args["host"] = host.value_or("0.0.0.0");
      request_args["port"] = port.value_or("28556");
      request_args["destination"] = destination.value_or(ctx.config.files.receive_dir);
      request_args["count"] = count.value_or("1");
      request_args["timeout"] = timeout.value_or("0");
      request_args["max_bytes"] = max_bytes.value_or("0");
      request_args["token"] = token.value_or("");
      request_args["announce"] = announce ? "true" : "false";

      const auto response = ask(ctx, "inbox.start_listener", request_args);
      if (!response.has_value()) {
        std::cerr << "error: " << response.error() << "\n";
        return 1;
      }
      if (!response->ok) {
        std::cerr << "error: " << response->error << "\n";
        return 1;
      }

      std::cout << "The daemon listens on " << request_args["host"] << ":" << request_args["port"]
                << " and receives into " << request_args["destination"] << "\n";
      return 0;
    }

    if (args[2] == "stop") {
      if (args.size() < 4) {
        return std::unexpected(std::string("usage: hyprshare daemon inbox stop <listener-id>"));
      }

      std::unordered_map<std::string, std::string> request_args;
      request_args["fd"] = args[3];

      const auto response = ask(ctx, "inbox.stop_listener", request_args);
      if (!response.has_value()) {
        std::cerr << "error: " << response.error() << "\n";
        return 1;
      }
      if (!response->ok) {
        std::cerr << "error: " << response->error << "\n";
        return 1;
      }
      std::cout << "Listener " << args[3] << " stopped\n";
      return 0;
    }

    return std::unexpected("unknown daemon inbox subcommand: " + args[2]);
  }

  return std::unexpected("unknown daemon subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
