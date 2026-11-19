#include "cmd_daemon.hpp"

#include <iostream>
#include <string>

#include "command_registry.hpp"
#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/daemon/ipc.hpp"

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

}  // namespace

std::expected<int, std::string> handle_daemon(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare daemon <status|stop|inbox list|inbox start|inbox stop>"));
  }

  if (args[1] == "status") {
    const auto response = ask(ctx, "daemon.ping");
    if (!response.has_value()) {
      std::cerr << "error: " << response.error() << "\n";
      return 1;
    }

    const auto socket = hyprshare::daemon::daemon_socket_path(ctx.paths);
    if (ctx.json) {
      std::cout << "{\"running\":true,\"socket\":\"" << socket.string() << "\"}\n";
    } else {
      std::cout << "Daemon: running\n";
      std::cout << "  Socket: " << socket << "\n";
    }
    return 0;
  }

  if (args[1] == "stop") {
    const auto response = ask(ctx, "daemon.stop");
    if (!response.has_value()) {
      std::cerr << "error: " << response.error() << "\n";
      return 1;
    }
    std::cout << "Daemon asked to stop\n";
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
