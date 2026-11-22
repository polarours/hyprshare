#include "cmd_clipboard_sync.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/sync/clipboard_sync.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cli {

namespace {

// The watcher runs in a detached child process so it survives the CLI call
// that started it. A pid file under the state dir lets status/stop find it.
std::filesystem::path daemon_pid_file(const CommandContext& ctx) {
  return ctx.paths.state_dir / "clipboard-sync" / "daemon.pid";
}

std::string self_executable() {
  char buffer[4096] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    return std::string(buffer, static_cast<std::size_t>(length));
  }
  return "hyprshare";
}

int read_daemon_pid(const CommandContext& ctx) {
  std::ifstream file(daemon_pid_file(ctx));
  int pid = -1;
  file >> pid;
  return pid;
}

bool process_alive(int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

void clear_daemon_pid(const CommandContext& ctx) {
  std::error_code error;
  std::filesystem::remove(daemon_pid_file(ctx), error);
}

}  // namespace

std::expected<int, std::string> handle_clipboard_sync(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare clipboard-sync <start|stop|status|push|pull|history|send|receive>"));
  }

  static hyprshare::sync::ClipboardSyncService service;

  // Internal entry point: the detached process that actually runs the watcher.
  // Kept out of --help; `start` spawns this with spawn_detached_shell.
  if (args[1] == "_daemon") {
    auto result = service.start();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    while (service.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  }

  if (args[1] == "start") {
    const int existing = read_daemon_pid(ctx);
    if (process_alive(existing)) {
      if (ctx.json) std::cout << "{\"status\":\"already_running\",\"pid\":" << existing << "}\n";
      else std::cout << "Clipboard sync is already running (pid " << existing << ")\n";
      return 0;
    }
    clear_daemon_pid(ctx);

    const std::string command =
      util::shell_quote(self_executable()) + " clipboard-sync _daemon";
    auto pid = ctx.runner.spawn_detached_shell(command);
    if (!pid.has_value()) {
      std::cerr << "error: " << pid.error() << "\n";
      return 1;
    }
    const auto path = daemon_pid_file(ctx);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file << *pid << "\n";
    if (ctx.json) std::cout << "{\"status\":\"started\",\"pid\":" << *pid << "}\n";
    else std::cout << "Clipboard sync service started (pid " << *pid << ")\n";
    return 0;
  }

  if (args[1] == "stop") {
    const int pid = read_daemon_pid(ctx);
    if (process_alive(pid)) {
      kill(pid, SIGTERM);
    }
    clear_daemon_pid(ctx);
    std::cout << "Clipboard sync service stopped\n";
    return 0;
  }

  if (args[1] == "status") {
    const int pid = read_daemon_pid(ctx);
    const bool running = process_alive(pid);
    if (!running) clear_daemon_pid(ctx);
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (running ? "running" : "stopped") << "\"";
      if (running) std::cout << ",\"pid\":" << pid;
      std::cout << "}\n";
    } else {
      std::cout << "Clipboard sync: " << (running ? "running" : "stopped") << "\n";
    }
    return 0;
  }

  if (args[1] == "push") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare clipboard-sync push <content>"));
    }

    auto result = service.push(args[2]);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Clipboard pushed\n";
    return 0;
  }

  if (args[1] == "pull") {
    auto result = service.pull();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      std::cout << "{\"content\":\"" << result->content << "\",\"mime_type\":\"" << result->mime_type << "\"}\n";
    } else {
      std::cout << result->content << "\n";
    }
    return 0;
  }

  if (args[1] == "send") {
    if (args.size() < 3) {
      return std::unexpected(std::string(
        "usage: hyprshare clipboard-sync send <host:port> [--token <token>] [--device <id>]"));
    }

    const auto token = take_option(args, "--token");
    const auto device = take_option(args, "--device");

    const auto separator = args[2].rfind(':');
    if (separator == std::string::npos) {
      return std::unexpected("the peer address needs a port: " + args[2]);
    }
    const std::string host = args[2].substr(0, separator);
    std::uint16_t port = 0;
    try {
      port = static_cast<std::uint16_t>(std::stoul(args[2].substr(separator + 1)));
    } catch (const std::exception&) {
      return std::unexpected("the peer address has an invalid port: " + args[2]);
    }

    auto result = service.push_to_peer(host, port, device.value_or("device"), token.value_or(""));
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Clipboard sent to " << args[2] << "\n";
    return 0;
  }

  if (args[1] == "receive") {
    if (args.size() < 3) {
      return std::unexpected(std::string(
        "usage: hyprshare clipboard-sync receive <host:port> [--token <token>]"));
    }

    const auto token = take_option(args, "--token");

    const auto separator = args[2].rfind(':');
    if (separator == std::string::npos) {
      return std::unexpected("the peer address needs a port: " + args[2]);
    }
    const std::string host = args[2].substr(0, separator);
    std::uint16_t port = 0;
    try {
      port = static_cast<std::uint16_t>(std::stoul(args[2].substr(separator + 1)));
    } catch (const std::exception&) {
      return std::unexpected("the peer address has an invalid port: " + args[2]);
    }

    auto result = service.pull_from_peer(host, port, token.value_or(""));
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      std::cout << "{\"content\":\"" << result->content
                << "\",\"source_device_id\":\"" << result->source_device_id << "\"}\n";
    } else {
      std::cout << result->content << "\n";
    }
    return 0;
  }

  if (args[1] == "history") {
    const auto history = service.history();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < history.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"content\":\"" << history[i].content << "\",\"mime_type\":\"" << history[i].mime_type << "\"}";
      }
      std::cout << "]\n";
    } else {
      for (const auto& entry : history) {
        std::cout << entry.content << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown clipboard-sync subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
