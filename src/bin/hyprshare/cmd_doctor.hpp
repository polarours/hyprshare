#pragma once

#include <string>
#include <vector>

#include "command_registry.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_doctor(CommandContext& ctx, std::vector<std::string>& args);
std::expected<int, std::string> handle_check(CommandContext& ctx, std::vector<std::string>& args);
std::expected<int, std::string> handle_support(CommandContext& ctx, std::vector<std::string>& args);

}  // namespace hyprshare::cli
