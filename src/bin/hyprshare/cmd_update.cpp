#include "cmd_update.hpp"

#include <iostream>
#include <filesystem>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/update/update_checker.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_update(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("update"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare update <check|download|apply|status>"));
  }

  static hyprshare::update::UpdateChecker checker;
  checker.set_current_version(HYPRSHARE_VERSION);

  if (const auto manifest = take_option(args, "--manifest")) {
    checker.set_release_manifest(*manifest);
  }
  if (const auto url = take_option(args, "--url")) {
    checker.set_release_url(*url);
  }

  if (args[1] == "check") {
    const auto result = checker.check_for_updates();
    if (!result.has_value()) {
      return std::unexpected("update check failed: " + result.error());
    }

    if (ctx.json) {
      std::cout << "{\"current_version\":\"" << result->current_version
                << "\",\"latest_version\":\"" << result->latest_version
                << "\",\"update_available\":" << (result->update_available ? "true" : "false")
                << ",\"release_info_available\":" << (result->release_info_available ? "true" : "false")
                << ",\"download_url\":\"" << result->download_url
                << "\",\"release_notes\":\"" << result->release_notes
                << "\",\"manifest\":\"" << checker.release_manifest().string()
                << "\"}\n";
    } else if (!result->release_info_available) {
      // Say plainly that nothing was checked, instead of reporting that the
      // installation is up to date.
      std::cout << "Current version: " << result->current_version << "\n";
      std::cout << "No release information available.\n";
      if (!checker.release_url().empty()) {
        std::cout << "  Looked at: " << checker.release_url() << "\n";
      } else {
        std::cout << "  Looked for: " << checker.release_manifest() << "\n";
      }
      std::cout << "  Provide a release manifest with --manifest <file> or "
                   "--url <url>, or set HYPRSHARE_RELEASE_MANIFEST.\n";
    } else {
      std::cout << "Current version: " << result->current_version << "\n";
      std::cout << "Latest version:  " << result->latest_version << "\n";
      if (result->update_available) {
        std::cout << "Update available! Run: hyprshare update download\n";
        if (!result->download_url.empty()) {
          std::cout << "  Download: " << result->download_url << "\n";
        }
      } else {
        std::cout << "Already up to date.\n";
      }
    }
    return 0;
  }

  if (args[1] == "download") {
    const auto checked = checker.check_for_updates();
    if (!checked.has_value()) {
      return std::unexpected("update check failed: " + checked.error());
    }
    if (!checked->release_info_available) {
      return std::unexpected(
        "no release information available; provide a release manifest with --manifest <file>");
    }

    const auto result = checker.download_update(checked->download_url);
    if (!result.has_value()) {
      return std::unexpected("download failed: " + result.error());
    }

    std::cout << "Downloaded to: " << checker.download_dir() << "\n";
    std::cout << "  Run the installer from that directory to apply the update.\n";
    return 0;
  }

  if (args[1] == "apply") {
    const bool confirmed = take_flag(args, "--yes");

    const auto plan = checker.plan_update();
    if (!plan.has_value()) {
      return std::unexpected("apply failed: " + plan.error());
    }

    if (!confirmed) {
      std::cout << "The release would be unpacked and installed:\n";
      std::cout << "  Archive:   " << plan->archive << "\n";
      std::cout << "  Unpacked:  " << plan->unpack_dir << "\n";
      std::cout << "  Installer: " << plan->installer << "\n";
      std::cout << "Run it with --yes to install it.\n";
      return 0;
    }

    const auto applied = checker.apply_update();
    if (!applied.has_value()) {
      return std::unexpected("apply failed: " + applied.error());
    }
    std::cout << "The release was installed.\n";
    return 0;
  }

  if (args[1] == "status") {
    if (ctx.json) {
      std::cout << "{\"current_version\":\"" << checker.current_version() << "\"}\n";
    } else {
      std::cout << "hyprshare " << checker.current_version() << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown update subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
