#include "cmd_sync.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/sync/file_sync.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_sync(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare sync <start|stop|status|add|remove|list|now>"));
  }

  static hyprshare::sync::FileSyncService service;

  if (args[1] == "start") {
    auto result = service.start();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "File sync service started" << std::endl;
    return 0;
  }

  if (args[1] == "stop") {
    service.stop();
    std::cout << "File sync service stopped\n";
    return 0;
  }

  if (args[1] == "status") {
    if (ctx.json) {
      std::cout << "{\"status\":\"" << (service.is_running() ? "running" : "stopped") << "\"}\n";
    } else {
      std::cout << "File sync: " << (service.is_running() ? "running" : "stopped") << "\n";
    }
    return 0;
  }

  if (args[1] == "add") {
    if (args.size() < 4) {
      return std::unexpected(std::string(
        "usage: hyprshare sync add <local-path> <remote-device-id> --address <host:port> [--token <token>]"));
    }

    const auto address = take_option(args, "--address");
    const auto token = take_option(args, "--token");

    hyprshare::sync::SyncRule rule;
    rule.id = "sync-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    rule.name = args[2];
    rule.local_path = args[2];
    rule.remote_device_id = args[3];
    rule.remote_address = address.value_or("");
    rule.remote_token = token.value_or("");
    rule.direction = hyprshare::sync::SyncDirection::LocalToRemote;

    auto result = service.add_rule(rule);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Sync rule added: " << rule.id << "\n";
    if (rule.remote_address.empty()) {
      std::cout << "  No peer address set; the rule needs --address <host:port> to transfer anything.\n";
    } else {
      std::cout << "  Peer: " << rule.remote_device_id << " at " << rule.remote_address << "\n";
    }
    return 0;
  }

  if (args[1] == "remove") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare sync remove <rule-id>"));
    }

    auto result = service.remove_rule(args[2]);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Sync rule removed\n";
    return 0;
  }

  if (args[1] == "list") {
    const auto rules = service.list_rules();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < rules.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"id\":\"" << rules[i].id
                  << "\",\"name\":\"" << rules[i].name
                  << "\",\"remote_device_id\":\"" << rules[i].remote_device_id
                  << "\",\"remote_address\":\"" << rules[i].remote_address << "\"}";
      }
      std::cout << "]\n";
    } else {
      for (const auto& rule : rules) {
        std::cout << rule.id << ": " << rule.name << " -> " << rule.remote_device_id;
        if (!rule.remote_address.empty()) {
          std::cout << " at " << rule.remote_address;
        }
        std::cout << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "now") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare sync now <rule-id>"));
    }

    const auto report = service.sync_now(args[2]);
    if (!report.has_value()) {
      std::cerr << "error: " << report.error() << "\n";
      return 1;
    }
    if (!report->failures.empty()) {
      for (const auto& failure : report->failures) {
        std::cerr << "error: " << failure << "\n";
      }
      return 1;
    }
    std::cout << "Synced " << report->files_sent << " file(s) sent, "
              << report->files_received << " file(s) received\n";
    std::cout << "  Bytes: " << report->bytes_sent << " sent, "
              << report->bytes_received << " received\n";
    return 0;
  }

  return std::unexpected("unknown sync subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
