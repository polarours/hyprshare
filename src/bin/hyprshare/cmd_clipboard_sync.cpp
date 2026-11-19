#include "cmd_clipboard_sync.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/sync/clipboard_sync.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_clipboard_sync(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare clipboard-sync <start|stop|status|push|pull|history|send|receive>"));
  }

  static hyprshare::sync::ClipboardSyncService service;

  if (args[1] == "start") {
    auto result = service.start();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Clipboard sync service started" << std::endl;
    return 0;
  }

  if (args[1] == "stop") {
    service.stop();
    std::cout << "Clipboard sync service stopped\n";
    return 0;
  }

  if (args[1] == "status") {
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (service.is_running() ? "running" : "stopped") << "\"}\n";
    } else {
      std::cout << "Clipboard sync: " << (service.is_running() ? "running" : "stopped") << "\n";
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
