#pragma once

#include <expected>
#include <string>
#include <vector>

namespace hyprshare::cli {

struct CommandContext;

std::expected<int, std::string> handle_daemon(CommandContext& ctx, std::vector<std::string>& args);

}  // namespace hyprshare::cli
