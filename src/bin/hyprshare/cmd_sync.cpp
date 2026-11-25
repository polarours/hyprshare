#include "cmd_sync.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/sync/file_sync.hpp"

namespace hyprshare::cli {

namespace {

// Rules are persisted as one JSON object per line under the state dir, so a
// `sync add` survives into the next CLI invocation. The service itself keeps
// rules in memory; these helpers bridge the per-process CLI to stable storage.
std::filesystem::path rules_file(const CommandContext& ctx) {
  return ctx.paths.state_dir / "sync" / "rules.jsonl";
}

std::string json_field(const std::string& line, const std::string& key) {
  const std::string needle = "\"" + key + "\":\"";
  const auto start = line.find(needle);
  if (start == std::string::npos) return {};
  const auto value_start = start + needle.size();
  const auto end = line.find('"', value_start);
  return end == std::string::npos ? std::string{} : line.substr(value_start, end - value_start);
}

void load_rules(const CommandContext& ctx, hyprshare::sync::FileSyncService& service) {
  std::ifstream file(rules_file(ctx));
  if (!file) return;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    hyprshare::sync::SyncRule rule;
    rule.id = json_field(line, "id");
    rule.name = json_field(line, "name");
    rule.local_path = json_field(line, "local_path");
    rule.remote_device_id = json_field(line, "remote_device_id");
    rule.remote_address = json_field(line, "remote_address");
    rule.remote_token = json_field(line, "remote_token");
    rule.remote_path = json_field(line, "remote_path");
    const auto direction = json_field(line, "direction");
    if (direction == "remote_to_local") {
      rule.direction = hyprshare::sync::SyncDirection::RemoteToLocal;
    } else if (direction == "bidirectional") {
      rule.direction = hyprshare::sync::SyncDirection::Bidirectional;
    } else {
      rule.direction = hyprshare::sync::SyncDirection::LocalToRemote;
    }
    rule.enabled = line.find("\"enabled\":false") == std::string::npos;
    if (!rule.id.empty()) static_cast<void>(service.add_rule(rule));
  }
}

void save_rules(const CommandContext& ctx, hyprshare::sync::FileSyncService& service) {
  const auto path = rules_file(ctx);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return;
  std::ofstream file(path, std::ios::trunc);
  if (!file) return;
  for (const auto& rule : service.list_rules()) {
    const char* direction = "local_to_remote";
    if (rule.direction == hyprshare::sync::SyncDirection::RemoteToLocal) direction = "remote_to_local";
    if (rule.direction == hyprshare::sync::SyncDirection::Bidirectional) direction = "bidirectional";
    file << "{\"id\":\"" << json_escape(rule.id)
         << "\",\"name\":\"" << json_escape(rule.name)
         << "\",\"local_path\":\"" << json_escape(rule.local_path.string())
         << "\",\"remote_device_id\":\"" << json_escape(rule.remote_device_id)
         << "\",\"remote_address\":\"" << json_escape(rule.remote_address)
         << "\",\"remote_token\":\"" << json_escape(rule.remote_token)
         << "\",\"remote_path\":\"" << json_escape(rule.remote_path.string())
         << "\",\"direction\":\"" << direction
         << "\",\"enabled\":" << (rule.enabled ? "true" : "false") << "}\n";
  }
}

}  // namespace

std::expected<int, std::string> handle_sync(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare sync <start|stop|status|add|remove|list|now>"));
  }

  static hyprshare::sync::FileSyncService service;
  // Every CLI call is a fresh process; rehydrate rules from disk first.
  load_rules(ctx, service);

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
    save_rules(ctx, service);
    if (ctx.json) {
      std::cout << "{\"status\":\"added\",\"id\":\"" << json_escape(rule.id)
                << "\",\"local_path\":\"" << json_escape(rule.local_path.string())
                << "\",\"remote_device_id\":\"" << json_escape(rule.remote_device_id)
                << "\",\"remote_address\":\"" << json_escape(rule.remote_address) << "\"}\n";
      return 0;
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
    save_rules(ctx, service);
    std::cout << "Sync rule removed\n";
    return 0;
  }

  if (args[1] == "list") {
    const auto rules = service.list_rules();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < rules.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"id\":\"" << json_escape(rules[i].id)
                  << "\",\"name\":\"" << json_escape(rules[i].name)
                  << "\",\"remote_device_id\":\"" << json_escape(rules[i].remote_device_id)
                  << "\",\"remote_address\":\"" << json_escape(rules[i].remote_address) << "\"}";
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
