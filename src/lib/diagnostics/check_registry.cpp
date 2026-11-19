#include "hyprshare/diagnostics/check_registry.hpp"

namespace hyprshare::diagnostics {

void CheckRegistry::register_check(std::string id, CheckFunction check) {
  checks_.push_back({std::move(id), std::move(check)});
}

std::vector<model::CheckResult> CheckRegistry::run_all(
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) const {
  std::vector<model::CheckResult> results;
  results.reserve(checks_.size());
  for (const auto& entry : checks_) {
    results.push_back(entry.check(paths, runner));
  }
  return results;
}

}  // namespace hyprshare::diagnostics
