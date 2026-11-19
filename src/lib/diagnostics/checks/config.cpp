#include "hyprshare/diagnostics/check_registry.hpp"

#include <filesystem>
#include <string>

#include "hyprshare/core/config.hpp"
#include "hyprshare/platform/net.hpp"

namespace hyprshare::diagnostics {

namespace {

model::CheckResult check_config_schema(const platform::Paths& paths, const platform::ProcessRunner&) {
  const auto config = core::load_config(paths);
  if (!config.has_value()) {
    return {"config.schema", model::CheckStatus::Fail, model::Severity::Error,
            "Config load failed: " + config.error(),
            {},
            {"Fix the config file syntax or permissions."}};
  }

  return {"config.schema", model::CheckStatus::Pass, model::Severity::Info,
          "Config schema is valid.", {}, {}};
}

model::CheckResult check_config_file_exists(const platform::Paths& paths, const platform::ProcessRunner&) {
  if (std::filesystem::exists(paths.config_file)) {
    return {"config.file_exists", model::CheckStatus::Pass, model::Severity::Info,
            "Config file exists: " + paths.config_file.string(), {}, {}};
  }
  return {"config.file_exists", model::CheckStatus::Fail, model::Severity::Warning,
          "Config file not found: " + paths.config_file.string(),
          {},
          {"Create a config file or run hyprshare with defaults."}};
}

}  // namespace

void register_config_checks(CheckRegistry& registry) {
  registry.register_check("config.schema", check_config_schema);
  registry.register_check("config.file_exists", check_config_file_exists);
}

}  // namespace hyprshare::diagnostics
