#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::diagnostics {

struct SupportBundleOptions {
  std::filesystem::path output_dir;
  std::optional<std::string> app_hint;
};

struct SupportBundle {
  std::filesystem::path directory;
  std::vector<std::filesystem::path> files;
};

std::expected<SupportBundle, std::string> write_support_bundle(
  const platform::Paths& paths,
  const core::Config& config,
  const core::BackendRegistry& backends,
  const core::AdapterRegistry& adapters,
  const model::DoctorReport& report,
  const SupportBundleOptions& options);

}  // namespace hyprshare::diagnostics
