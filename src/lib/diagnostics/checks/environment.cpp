#include "hyprshare/diagnostics/check_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "hyprshare/platform/net.hpp"

namespace hyprshare::diagnostics {

namespace {

model::CheckResult check_xdg_runtime_dir(const platform::Paths&, const platform::ProcessRunner&) {
  const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime == nullptr || *xdg_runtime == '\0') {
    return {"env.xdg_runtime_dir", model::CheckStatus::Fail, model::Severity::Warning,
            "XDG_RUNTIME_DIR is not set.",
            {},
            {"Set XDG_RUNTIME_DIR for proper session state management."}};
  }

  const std::filesystem::path runtime_dir(xdg_runtime);
  std::error_code error;
  if (!std::filesystem::exists(runtime_dir, error)) {
    return {"env.xdg_runtime_dir", model::CheckStatus::Fail, model::Severity::Warning,
            "XDG_RUNTIME_DIR does not exist: " + std::string(xdg_runtime),
            {},
            {"Create the runtime directory or fix XDG_RUNTIME_DIR."}};
  }

  return {"env.xdg_runtime_dir", model::CheckStatus::Pass, model::Severity::Info,
          "XDG_RUNTIME_DIR is set and exists.", {}, {}};
}

model::CheckResult check_home_env(const platform::Paths&, const platform::ProcessRunner&) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return {"env.home", model::CheckStatus::Fail, model::Severity::Warning,
            "HOME is not set.",
            {},
            {"Set HOME for proper path expansion."}};
  }
  return {"env.home", model::CheckStatus::Pass, model::Severity::Info,
          "HOME is set.", {}, {}};
}

model::CheckResult check_tmp_writable(const platform::Paths&, const platform::ProcessRunner&) {
  const auto tmp_dir = std::filesystem::temp_directory_path();
  const auto probe = tmp_dir / ".hyprshare-tmp-probe";
  std::error_code error;
  std::ofstream ofs(probe);
  if (!ofs.is_open()) {
    return {"env.tmp_writable", model::CheckStatus::Fail, model::Severity::Warning,
            "Temporary directory is not writable: " + tmp_dir.string(),
            {},
            {"Check permissions on the temporary directory."}};
  }
  ofs.close();
  std::filesystem::remove(probe, error);
  return {"env.tmp_writable", model::CheckStatus::Pass, model::Severity::Info,
          "Temporary directory is writable.", {}, {}};
}

}  // namespace

void register_environment_checks(CheckRegistry& registry) {
  registry.register_check("env.xdg_runtime_dir", check_xdg_runtime_dir);
  registry.register_check("env.home", check_home_env);
  registry.register_check("env.tmp_writable", check_tmp_writable);
}

}  // namespace hyprshare::diagnostics
