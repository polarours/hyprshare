#include "cmd_backup.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/backup/backup_manager.hpp"

namespace hyprshare::cli {

namespace {

std::string json_string(const std::string& value) {
  std::string out;
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

std::expected<int, std::string> handle_backup(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare backup <create|restore|delete|list|inspect>"));
  }

  hyprshare::backup::BackupManager manager;

  if (args[1] == "create") {
    std::string description = "Manual backup";
    for (std::size_t i = 2; i < args.size(); ++i) {
      if (args[i] == "--description" && i + 1 < args.size()) {
        description = args[++i];
      } else if (!args[i].starts_with("--") && description == "Manual backup") {
        // Positional form documented in the user guide:
        // `hyprshare backup create "Before upgrade"`.
        description = args[i];
      }
    }
    auto result = manager.create_backup(description);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"backup_id\":\"" << json_string(*result) << "\"}\n";
    } else {
      std::cout << "Backup created: " << *result << "\n";
    }
    return 0;
  }

  if (args[1] == "restore") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare backup restore <backup-id>"));
    }

    auto result = manager.restore_backup(args[2]);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"ok\",\"backup_id\":\"" << json_string(args[2]) << "\"}\n";
    } else {
      std::cout << "Backup restored: " << args[2] << "\n";
    }
    return 0;
  }

  if (args[1] == "delete") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare backup delete <backup-id>"));
    }

    auto result = manager.delete_backup(args[2]);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"ok\",\"backup_id\":\"" << json_string(args[2]) << "\"}\n";
    } else {
      std::cout << "Backup deleted: " << args[2] << "\n";
    }
    return 0;
  }

  if (args[1] == "list") {
    const auto backups = manager.list_backups();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < backups.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"backup_id\":\"" << json_string(backups[i].backup_id)
                  << "\",\"description\":\"" << json_string(backups[i].description)
                  << "\",\"created_at\":" << backups[i].created_at << "}";
      }
      std::cout << "]\n";
    } else {
      for (const auto& backup : backups) {
        std::cout << backup.backup_id << " - " << backup.description << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "inspect") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare backup inspect <backup-id>"));
    }

    auto result = manager.inspect_backup(args[2]);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"backup_id\":\"" << json_string(result->backup_id)
                << "\",\"description\":\"" << json_string(result->description)
                << "\",\"created_at\":" << result->created_at << ",\"entries\":[";
      for (std::size_t i = 0; i < result->entries.size(); ++i) {
        if (i > 0) std::cout << ",";
        const auto& entry = result->entries[i];
        std::cout << "{\"name\":\"" << json_string(entry.name)
                  << "\",\"size\":" << entry.size
                  << ",\"timestamp\":" << entry.timestamp << "}";
      }
      std::cout << "]}\n";
    } else {
      std::cout << "Backup: " << result->backup_id << "\n";
      std::cout << "Description: " << result->description << "\n";
      std::cout << "Entries: " << result->entries.size() << "\n";
      for (const auto& entry : result->entries) {
        std::cout << "  " << entry.name << " (" << entry.size << " bytes)\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown backup subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
