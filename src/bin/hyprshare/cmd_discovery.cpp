#include "cmd_discovery.hpp"

#include <iostream>

#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/discovery.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_discovery(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown discovery subcommand"));
  }

  if (args[1] == "status") {
    const auto discovery_status = hyprshare::core::inspect_discovery_status(ctx.runner);
    if (ctx.json) {
      print_discovery_status_json(discovery_status);
    } else {
      print_discovery_status_text(discovery_status);
    }
    return 0;
  }

  if (args[1] == "browse") {
    const auto inboxes = hyprshare::core::browse_inbox_announcements(ctx.runner);
    if (!inboxes.has_value()) {
      std::cerr << "error: " << inboxes.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_discovered_inboxes_json(*inboxes);
    } else {
      print_discovered_inboxes_text(*inboxes);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown discovery subcommand: " + args[1]));
}

}  // namespace hyprshare::cli
