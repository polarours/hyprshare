#pragma once

#include <optional>
#include <string_view>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/diagnostics/check_registry.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::diagnostics {

enum class FailThreshold {
  None,
  Warning,
  Error,
};

class Doctor {
 public:
  explicit Doctor(const platform::ProcessRunner& runner);

  model::DoctorReport run(
    const platform::Paths& paths,
    const core::AdapterRegistry& adapters,
    std::optional<std::string_view> app_hint = std::nullopt) const;

  void set_fail_threshold(FailThreshold threshold);
  FailThreshold fail_threshold() const;
  int exit_code_for_report(const model::DoctorReport& report) const;

 private:
  const platform::ProcessRunner& runner_;
  FailThreshold fail_threshold_ = FailThreshold::None;
  CheckRegistry registry_;
};

}  // namespace hyprshare::diagnostics

