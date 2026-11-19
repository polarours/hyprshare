#pragma once

#include <expected>
#include <vector>

#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::core {

class AppLauncher {
 public:
  explicit AppLauncher(const platform::ProcessRunner& runner);

  [[nodiscard]] std::vector<std::string> resolve_available_commands(const model::AppProfile& profile) const;
  std::expected<std::vector<int>, std::string> execute(const model::AppLaunchPlan& plan) const;

 private:
  const platform::ProcessRunner& runner_;
};

}  // namespace hyprshare::core
