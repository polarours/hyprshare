#include "cmd_profile.hpp"

#include <iostream>
#include <filesystem>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/profile/profile_manager.hpp"

namespace hyprshare::cli {

namespace {

// A profile is addressed by its identifier or by its name, so that the name
// that was given when it was created is enough to find it again.
std::string resolve_profile_id(
  const hyprshare::profile::ProfileManager& pm,
  const std::string& identifier) {
  const auto direct = pm.get_profile(identifier);
  if (direct.has_value()) {
    return identifier;
  }

  for (const auto& profile : pm.list_profiles()) {
    if (profile.name == identifier) {
      return profile.id;
    }
  }

  return "";
}

}  // namespace

std::expected<int, std::string> handle_profile(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("profile"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare profile <list|get|create|delete|switch|export|import>"));
  }

  static hyprshare::profile::ProfileManager pm;

  if (args[1] == "list") {
    const auto profiles = pm.list_profiles();
    if (ctx.json) {
      std::cout << "{\"profiles\":[";
      for (std::size_t i = 0; i < profiles.size(); ++i) {
        if (i > 0) std::cout << ",";
        const auto& p = profiles[i];
        std::cout << "{\"id\":\"" << p.id
                  << "\",\"name\":\"" << p.name
                  << "\",\"email\":\"" << p.email
                  << "\",\"active\":" << (p.active ? "true" : "false")
                  << "}";
      }
      std::cout << "],\"count\":" << profiles.size() << "}\n";
    } else {
      std::cout << "Profiles (" << profiles.size() << "):\n";
      for (const auto& p : profiles) {
        std::cout << "  " << p.id << " (" << p.name << ")"
                  << (p.active ? " *" : "") << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "get") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profile get <profile-id>"));
    }
    const auto id = resolve_profile_id(pm, args[2]);
    if (id.empty()) {
      return std::unexpected("profile not found: " + args[2]);
    }
    const auto result = pm.get_profile(id);
    if (!result.has_value()) {
      return std::unexpected("profile not found: " + result.error());
    }
    if (ctx.json) {
      std::cout << "{\"id\":\"" << result->id
                << "\",\"name\":\"" << result->name
                << "\",\"email\":\"" << result->email
                << "\",\"active\":" << (result->active ? "true" : "false")
                << ",\"config_dir\":\"" << result->config_dir.string()
                << "\",\"state_dir\":\"" << result->state_dir.string()
                << "\"}\n";
    } else {
      std::cout << "Profile: " << result->id << "\n";
      std::cout << "  Name: " << result->name << "\n";
      if (!result->email.empty()) {
        std::cout << "  Email: " << result->email << "\n";
      }
      std::cout << "  Config: " << result->config_dir.string() << "\n";
      std::cout << "  State:  " << result->state_dir.string() << "\n";
    }
    return 0;
  }

  if (args[1] == "create") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profile create <name> [--email <email>]"));
    }
    std::string email;
    for (std::size_t i = 3; i < args.size(); ++i) {
      if (args[i] == "--email" && i + 1 < args.size()) email = args[++i];
    }
    const auto result = pm.create_profile(args[2], email);
    if (!result.has_value()) {
      return std::unexpected("create failed: " + result.error());
    }

    // The identifier is not the name, so it is reported separately.
    const auto created = pm.list_profiles();
    std::string created_id;
    for (const auto& profile : created) {
      if (profile.name == args[2]) {
        created_id = profile.id;
      }
    }

    std::cout << "Profile created: " << args[2] << "\n";
    if (!created_id.empty()) {
      std::cout << "  Id: " << created_id << "\n";
    }
    return 0;
  }

  if (args[1] == "delete") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profile delete <profile-id>"));
    }
    const auto id = resolve_profile_id(pm, args[2]);
    if (id.empty()) {
      return std::unexpected("profile not found: " + args[2]);
    }
    const auto result = pm.delete_profile(id);
    if (!result.has_value()) {
      return std::unexpected("delete failed: " + result.error());
    }
    std::cout << "Profile deleted: " << id << "\n";
    return 0;
  }

  if (args[1] == "switch") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profile switch <profile-id>"));
    }
    const auto id = resolve_profile_id(pm, args[2]);
    if (id.empty()) {
      return std::unexpected("profile not found: " + args[2]);
    }
    const auto result = pm.switch_profile(id);
    if (!result.has_value()) {
      return std::unexpected("switch failed: " + result.error());
    }
    std::cout << "Switched to profile: " << id << "\n";
    return 0;
  }

  if (args[1] == "export") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare profile export <profile-id> <output-path>"));
    }
    const auto id = resolve_profile_id(pm, args[2]);
    if (id.empty()) {
      return std::unexpected("profile not found: " + args[2]);
    }
    const auto result = pm.export_profile(id, std::filesystem::path(args[3]));
    if (!result.has_value()) {
      return std::unexpected("export failed: " + result.error());
    }
    std::cout << "Exported profile to: " << args[3] << "\n";
    return 0;
  }

  if (args[1] == "import") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profile import <path>"));
    }
    const auto result = pm.import_profile(std::filesystem::path(args[2]));
    if (!result.has_value()) {
      return std::unexpected("import failed: " + result.error());
    }
    std::cout << "Imported profile: " << args[2] << "\n";
    return 0;
  }

  return std::unexpected("unknown profile subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
