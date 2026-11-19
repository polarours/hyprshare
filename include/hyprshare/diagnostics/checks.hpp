#pragma once

#include "hyprshare/diagnostics/check_registry.hpp"

namespace hyprshare::diagnostics {

void register_network_checks(CheckRegistry& registry);
void register_environment_checks(CheckRegistry& registry);
void register_config_checks(CheckRegistry& registry);

}  // namespace hyprshare::diagnostics
