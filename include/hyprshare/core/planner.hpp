#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/model/types.hpp"

namespace hyprshare::core {

class SharePlanner {
 public:
  model::SharePlan plan_screen(
    std::string_view app_hint,
    const model::FactMap& facts,
    const Config& config,
    const BackendRegistry& backends,
    const AdapterRegistry& adapters) const;

  model::AppLaunchPlan plan_app_launch(
    std::string_view app_hint,
    const model::FactMap& facts,
    const Config& config,
    const BackendRegistry& backends,
    const AdapterRegistry& adapters,
    std::span<const std::string> available_launch_commands) const;
};

}  // namespace hyprshare::core
