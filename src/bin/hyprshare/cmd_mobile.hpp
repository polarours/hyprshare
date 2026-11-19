#pragma once

#include <string>
#include <vector>

#include "command_registry.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_mobile(CommandContext& ctx, std::vector<std::string>& args);
std::expected<int, std::string> handle_remote(CommandContext& ctx, std::vector<std::string>& args);

}  // namespace hyprshare::cli
