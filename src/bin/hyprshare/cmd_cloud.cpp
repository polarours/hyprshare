#include "cmd_cloud.hpp"

#include <iostream>
#include <filesystem>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/cloud/cloud_sync.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_cloud(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("cloud"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare cloud <configure|upload|download|sync|list|status>"));
  }

  static hyprshare::cloud::CloudSyncClient client;
  const auto selected_provider = take_option(args, "--provider");

  if (args[1] == "configure") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare cloud configure <provider> [--type local|http] [--endpoint <url>] [--bucket <name>] [--token <token>]"));
    }
    hyprshare::cloud::CloudConfig config;
    config.provider = args[2];
    config.type = take_option(args, "--type").value_or(hyprshare::cloud::kLocalProvider);
    config.endpoint = take_option(args, "--endpoint").value_or("");
    config.bucket = take_option(args, "--bucket").value_or("");
    config.access_key = take_option(args, "--token").value_or("");
    config.secret_key = take_option(args, "--secret").value_or("");
    config.enabled = true;

    if (config.type != hyprshare::cloud::kLocalProvider &&
        config.type != hyprshare::cloud::kHttpProvider) {
      return std::unexpected("unknown cloud provider type: " + config.type);
    }
    if (config.type == hyprshare::cloud::kHttpProvider && config.endpoint.empty()) {
      return std::unexpected("the http provider needs --endpoint <url>");
    }

    const auto result = client.configure(config);
    if (!result.has_value()) {
      return std::unexpected("configure failed: " + result.error());
    }
    if (ctx.json) {
      std::cout << "{\"status\":\"configured\",\"provider\":\"" << json_escape(config.provider)
                << "\",\"type\":\"" << json_escape(config.type) << "\"";
      if (config.type == hyprshare::cloud::kHttpProvider) {
        std::cout << ",\"endpoint\":\"" << json_escape(config.endpoint) << "\"";
        if (!config.bucket.empty()) {
          std::cout << ",\"bucket\":\"" << json_escape(config.bucket) << "\"";
        }
      } else {
        std::cout << ",\"local_store\":\"" << json_escape(client.local_store().string()) << "\"";
      }
      std::cout << "}\n";
      return 0;
    }

    std::cout << "Cloud configured: " << config.provider << " (" << config.type << ")\n";
    if (config.type == hyprshare::cloud::kHttpProvider) {
      std::cout << "  Endpoint: " << config.endpoint << "\n";
      if (!config.bucket.empty()) {
        std::cout << "  Bucket: " << config.bucket << "\n";
      }
    } else {
      std::cout << "  Local store: " << client.local_store() << "\n";
    }
    return 0;
  }

  // Every other subcommand works against the persisted configuration, so that
  // a provider configured in an earlier invocation is still in effect.
  const auto loaded = client.load(selected_provider.value_or(""));
  if (!loaded.has_value()) {
    if (args[1] == "status") {
      // Reporting that nothing is configured is the expected answer here.
      if (ctx.json) {
        std::cout << "{\"enabled\":false,\"provider\":\"\",\"status\":\"idle\"}\n";
      } else {
        std::cout << "Cloud: disabled\n";
        std::cout << "  Provider: \n";
        std::cout << "  Status: idle\n";
        std::cout << "  Local store: \n";
      }
      return 0;
    }
    return std::unexpected(loaded.error());
  }

  if (args[1] == "upload") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare cloud upload <local-path> <remote-path>"));
    }
    const auto local = std::filesystem::path(args[2]);
    const auto remote = args[3];
    if (!std::filesystem::exists(local)) {
      return std::unexpected("source not found: " + local.string());
    }
    const auto result = client.upload(local, remote);
    if (!result.has_value()) {
      return std::unexpected("upload failed: " + result.error());
    }
    std::cout << "Uploaded: " << local.string() << " -> " << remote << "\n";
    if (client.config().type == hyprshare::cloud::kHttpProvider) {
      std::cout << "  Stored at: " << client.object_url(remote) << "\n";
    } else {
      std::cout << "  Stored in: " << (client.local_store() / remote) << "\n";
    }
    return 0;
  }

  if (args[1] == "download") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare cloud download <remote-path> <local-path>"));
    }
    const auto remote = args[2];
    const auto local = std::filesystem::path(args[3]);
    const auto result = client.download(remote, local);
    if (!result.has_value()) {
      return std::unexpected("download failed: " + result.error());
    }
    std::cout << "Downloaded: " << remote << " -> " << local.string() << "\n";
    if (client.config().type == hyprshare::cloud::kHttpProvider) {
      std::cout << "  Read from: " << client.object_url(remote) << "\n";
    } else {
      std::cout << "  Read from: " << (client.local_store() / remote) << "\n";
    }
    return 0;
  }

  if (args[1] == "sync") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare cloud sync <local-dir> <remote-dir>"));
    }
    const auto local_dir = std::filesystem::path(args[2]);
    const auto remote_dir = args[3];
    if (!std::filesystem::exists(local_dir)) {
      return std::unexpected("local dir not found: " + local_dir.string());
    }
    const auto result = client.sync_directory(local_dir, remote_dir);
    if (!result.has_value()) {
      return std::unexpected("sync failed: " + result.error());
    }
    std::cout << "Synced: " << local_dir.string() << " -> " << remote_dir << "\n";
    if (client.config().type == hyprshare::cloud::kHttpProvider) {
      std::cout << "  Stored at: " << client.object_url(remote_dir) << "\n";
    } else {
      std::cout << "  Stored in: " << (client.local_store() / remote_dir) << "\n";
    }
    return 0;
  }

  if (args[1] == "list") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare cloud list <remote-dir>"));
    }
    const auto remote_dir = args[2];
    const auto result = client.list_files(remote_dir);
    if (!result.has_value()) {
      return std::unexpected("list failed: " + result.error());
    }
    if (ctx.json) {
      std::cout << "{\"files\":[";
      for (std::size_t i = 0; i < result->size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "\"" << result->at(i) << "\"";
      }
      std::cout << "],\"count\":" << result->size() << "}\n";
    } else {
      std::cout << "Files in " << remote_dir << " (" << result->size() << "):\n";
      for (const auto& f : *result) {
        std::cout << "  " << f << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "status") {
    const auto cfg = client.config();
    if (ctx.json) {
      std::cout << "{\"enabled\":" << (cfg.enabled ? "true" : "false")
                << ",\"provider\":\"" << cfg.provider
                << "\",\"local_store\":\"" << client.local_store().string()
                << "\",\"status\":\"" << [&]() -> std::string {
                  switch (client.status()) {
                    case hyprshare::cloud::SyncStatus::Idle: return "idle";
                    case hyprshare::cloud::SyncStatus::Syncing: return "syncing";
                    case hyprshare::cloud::SyncStatus::Uploading: return "uploading";
                    case hyprshare::cloud::SyncStatus::Downloading: return "downloading";
                    case hyprshare::cloud::SyncStatus::Error: return "error";
                  }
                  return "unknown";
                }() << "\"}\n";
    } else {
      std::cout << "Cloud: " << (cfg.enabled ? "enabled" : "disabled") << "\n";
      std::cout << "  Provider: " << cfg.provider << " (" << cfg.type << ")\n";
      if (cfg.type == hyprshare::cloud::kHttpProvider) {
        std::cout << "  Endpoint: " << cfg.endpoint << "\n";
        if (!cfg.bucket.empty()) {
          std::cout << "  Bucket: " << cfg.bucket << "\n";
        }
      } else {
        std::cout << "  Local store: " << client.local_store() << "\n";
      }
      std::cout << "  Status: ";
      switch (client.status()) {
        case hyprshare::cloud::SyncStatus::Idle: std::cout << "idle"; break;
        case hyprshare::cloud::SyncStatus::Syncing: std::cout << "syncing"; break;
        case hyprshare::cloud::SyncStatus::Uploading: std::cout << "uploading"; break;
        case hyprshare::cloud::SyncStatus::Downloading: std::cout << "downloading"; break;
        case hyprshare::cloud::SyncStatus::Error: std::cout << "error"; break;
      }
      std::cout << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown cloud subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
