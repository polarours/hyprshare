#include "cmd_mobile.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/mobile/mobile_transfer.hpp"
#include "hyprshare/remote/remote_control.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_mobile(CommandContext&, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare mobile <serve|send>"));
  }

  if (args[1] == "serve") {
    const auto port_str = take_option(args, "--port");
    const uint16_t port = port_str ? static_cast<uint16_t>(std::stoul(*port_str)) : 28556;

    hyprshare::mobile::MobileTransferServer server(port);
    auto start_result = server.start();
    if (!start_result.has_value()) {
      std::cerr << "error: " << start_result.error() << "\n";
      return 1;
    }

    std::cout << "Mobile transfer server started on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (server.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
  }

  if (args[1] == "send") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare mobile send <device_id> <file_path>"));
    }

    const auto device_id = args[2];
    const auto file_path = args[3];

    if (!std::filesystem::exists(file_path)) {
      return std::unexpected("file not found: " + file_path);
    }

    hyprshare::mobile::MobileTransferServer server;
    auto send_result = server.send_file(device_id, file_path);
    if (!send_result.has_value()) {
      std::cerr << "error: " << send_result.error() << "\n";
      return 1;
    }

    std::cout << "File sent to " << device_id << "\n";
    return 0;
  }

  return std::unexpected("unknown mobile subcommand: " + args[1]);
}

namespace {

std::filesystem::path remote_state_dir(const CommandContext& ctx) {
  return ctx.paths.state_dir / "remote";
}

std::filesystem::path remote_pid_file(const CommandContext& ctx) {
  return remote_state_dir(ctx) / "daemon.pid";
}

std::filesystem::path remote_port_file(const CommandContext& ctx) {
  return remote_state_dir(ctx) / "daemon.port";
}

std::filesystem::path remote_input_log(const CommandContext& ctx) {
  return remote_state_dir(ctx) / "input.log";
}

std::string self_executable_path() {
  std::array<char, 4096> buffer{};
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) {
    return "hyprshare";
  }
  return std::string(buffer.data(), static_cast<std::size_t>(length));
}

int read_pid_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  int pid = -1;
  file >> pid;
  return pid;
}

bool process_alive(const int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

// Reports which tool could actually inject input on this host. None of them is
// a dependency of the package, so an absent backend must be visible rather than
// silently turning injected events into log lines.
std::string detect_input_backend(const hyprshare::platform::ProcessRunner& runner) {
  for (const char* candidate : {"ydotool", "wtype", "dotool"}) {
    if (runner.command_exists(candidate)) {
      return candidate;
    }
  }
  return "none";
}

std::size_t count_input_events(const std::filesystem::path& log) {
  std::ifstream file(log);
  if (!file.is_open()) {
    return 0;
  }
  std::size_t lines = 0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty()) {
      ++lines;
    }
  }
  return lines;
}

}  // namespace

std::expected<int, std::string> handle_remote(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare remote <serve|status|stop>"));
  }

  // Internal entry point for the detached process started by `serve`. Hidden
  // from --help; the server only stays up while this process lives.
  if (args[1] == "_daemon") {
    const auto port_str = take_option(args, "--port");
    const uint16_t port = port_str ? static_cast<uint16_t>(std::stoul(*port_str)) : 28557;

    hyprshare::remote::RemoteControlServer server(port);
    auto started = server.start();
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
    }

    // Events are recorded to the state dir so `status` can report a real count.
    // Injection needs an external backend that this package does not ship.
    const auto log_path = remote_input_log(ctx);
    server.set_input_callback([log_path](const hyprshare::remote::InputEvent& event) {
      std::ofstream log(log_path, std::ios::app);
      if (!log.is_open()) {
        return;
      }
      log << "type=" << static_cast<int>(event.type) << " x=" << event.x
          << " y=" << event.y << " action=" << event.action << " key=" << event.key
          << "\n";
    });

    while (server.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  }

  if (args[1] == "serve") {
    const int existing = read_pid_file(remote_pid_file(ctx));
    if (process_alive(existing)) {
      if (ctx.json) {
        std::cout << "{\"status\":\"already_running\",\"pid\":" << existing << "}\n";
      } else {
        std::cout << "Remote control is already running (pid " << existing << ")\n";
      }
      return 0;
    }

    const auto port_str = take_option(args, "--port");
    const uint16_t port = port_str ? static_cast<uint16_t>(std::stoul(*port_str)) : 28557;

    std::error_code error;
    std::filesystem::create_directories(remote_state_dir(ctx), error);
    std::filesystem::remove(remote_pid_file(ctx), error);

    const std::string command = util::shell_quote(self_executable_path()) +
                                " remote _daemon --port " + std::to_string(port);
    auto pid = ctx.runner.spawn_detached_shell(command);
    if (!pid.has_value()) {
      std::cerr << "error: " << pid.error() << "\n";
      return 1;
    }

    {
      std::ofstream pid_file(remote_pid_file(ctx));
      pid_file << *pid << "\n";
      std::ofstream port_file(remote_port_file(ctx));
      port_file << port << "\n";
    }

    const auto backend = detect_input_backend(ctx.runner);
    if (ctx.json) {
      std::cout << "{\"status\":\"started\",\"pid\":" << *pid << ",\"port\":" << port
                << ",\"input_backend\":\"" << backend << "\"}\n";
    } else {
      std::cout << "Remote control server started on port " << port << " (pid " << *pid
                << ")\n";
      if (backend == "none") {
        std::cout << "Note: no input injection backend found (ydotool/wtype/dotool), so "
                     "incoming input events are recorded to "
                  << remote_input_log(ctx).string() << " but not injected.\n";
      } else {
        std::cout << "Input events are recorded to " << remote_input_log(ctx).string()
                  << "; injection backend detected: " << backend << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "stop") {
    const int pid = read_pid_file(remote_pid_file(ctx));
    const bool was_running = process_alive(pid);
    if (was_running) {
      kill(pid, SIGTERM);
    }
    std::error_code error;
    std::filesystem::remove(remote_pid_file(ctx), error);
    std::filesystem::remove(remote_port_file(ctx), error);
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (was_running ? "stopped" : "not_running")
                << "\",\"pid\":" << (was_running ? pid : 0) << "}\n";
    } else {
      std::cout << "Remote control: " << (was_running ? "stopped" : "was not running")
                << "\n";
    }
    return 0;
  }

  if (args[1] == "status") {
    const int pid = read_pid_file(remote_pid_file(ctx));
    const bool running = process_alive(pid);
    if (!running) {
      std::error_code error;
      std::filesystem::remove(remote_pid_file(ctx), error);
      std::filesystem::remove(remote_port_file(ctx), error);
    }

    const int port = read_pid_file(remote_port_file(ctx));
    const auto events = count_input_events(remote_input_log(ctx));
    const auto backend = detect_input_backend(ctx.runner);

    if (ctx.json) {
      std::cout << "{\"status\":\"" << (running ? "running" : "not_running") << "\"";
      if (running) {
        std::cout << ",\"pid\":" << pid;
      }
      if (port > 0) {
        std::cout << ",\"port\":" << port;
      }
      std::cout << ",\"input_events\":" << events;
      std::cout << ",\"input_backend\":\"" << backend << "\"";
      std::cout << "}\n";
    } else {
      std::cout << "Remote control: " << (running ? "running" : "not running") << "\n";
      if (running) {
        std::cout << "  pid: " << pid << "\n";
      }
      if (port > 0) {
        std::cout << "  port: " << port << "\n";
      }
      std::cout << "  input events recorded: " << events << "\n";
      std::cout << "  input injection backend: " << backend
                << (backend == "none" ? " (events are recorded only)" : "") << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown remote subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
