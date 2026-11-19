#include "cmd_compat.hpp"

#include <iostream>

#include "output_json.hpp"
#include "output_text.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_compat(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 3 || args[1] != "show") {
    return std::unexpected(std::string("usage: hyprshare compat show <app>"));
  }

  const auto profile = ctx.adapters.find_by_hint(args[2]);
  if (!profile.has_value()) {
    std::cerr << "error: no compatibility profile matched `" << args[2] << "`\n";
    return 1;
  }
  if (ctx.json) {
    print_profile_json(*profile);
  } else {
    print_profile_text(*profile);
  }
  return 0;
}

}  // namespace hyprshare::cli
