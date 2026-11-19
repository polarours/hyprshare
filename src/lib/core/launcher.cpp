#include "hyprshare/core/launcher.hpp"

#include <string>

namespace hyprshare::core {

AppLauncher::AppLauncher(const platform::ProcessRunner& runner) : runner_(runner) {}

std::vector<std::string> AppLauncher::resolve_available_commands(const model::AppProfile& profile) const {
  std::vector<std::string> available;
  for (const auto& command : profile.launch_commands) {
    if (runner_.command_exists(command)) {
      available.push_back(command);
    }
  }
  return available;
}

std::expected<std::vector<int>, std::string> AppLauncher::execute(const model::AppLaunchPlan& plan) const {
  if (!plan.ready) {
    return std::unexpected("launch plan is not ready");
  }

  std::vector<int> pids;
  for (const auto& action : plan.actions) {
    auto pid = runner_.spawn_detached_shell(action.command);
    if (!pid.has_value()) {
      return std::unexpected("failed to execute `" + action.description + "`: " + pid.error());
    }
    pids.push_back(*pid);
  }

  return pids;
}

}  // namespace hyprshare::core

