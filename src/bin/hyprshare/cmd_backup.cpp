#include "cmd_backup.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/backup/backup_manager.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_backup(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare backup <create|restore|delete|list|inspect>"));
  }

  hyprshare::backup::BackupManager manager;

  if (args[1] == "create") {
    const auto description = args.size() > 2 ? args[2] : "Manual backup";
    auto result = manager.create_backup(description);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"backup_id\":\"" << *result << "\"}\n";
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
      std::cout << "{\"status\":\"ok\",\"backup_id\":\"" << args[2] << "\"}\n";
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
      std::cout << "{\"status\":\"ok\",\"backup_id\":\"" << args[2] << "\"}\n";
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
        std::cout << "{\"backup_id\":\"" << backups[i].backup_id
                  << "\",\"description\":\"" << backups[i].description << "\"}";
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
      std::cout << "{\"backup_id\":\"" << result->backup_id
                << "\",\"entries\":" << result->entries.size() << "}\n";
    } else {
      std::cout << "Backup: " << result->backup_id << "\n";
      std::cout << "Entries: " << result->entries.size() << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown backup subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
