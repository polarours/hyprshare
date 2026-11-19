#pragma once

#include <string>
#include <vector>

#include "command_registry.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_batch(CommandContext& ctx, std::vector<std::string>& args);

}  // namespace hyprshare::cli
