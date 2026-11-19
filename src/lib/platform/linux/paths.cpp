#include "hyprshare/platform/paths.hpp"

#include <cstdlib>

namespace hyprshare::platform {

namespace {

std::filesystem::path env_path(const char* name, const std::filesystem::path& fallback) {
  if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
    return value;
  }
  return fallback;
}

}  // namespace

Paths resolve_paths() {
  const std::filesystem::path home = env_path("HOME", std::filesystem::current_path());
  const std::filesystem::path source_dir = HYPRSHARE_SOURCE_DIR;
  const std::filesystem::path default_data_dir = source_dir / "data";

  Paths paths;
  paths.source_dir = source_dir;
  paths.data_dir = env_path("HYPRSHARE_DATA_DIR", default_data_dir);
  paths.config_dir = env_path("XDG_CONFIG_HOME", home / ".config") / "hyprshare";
  paths.config_file = paths.config_dir / "config.toml";
  paths.state_dir = env_path("XDG_STATE_HOME", home / ".local" / "state") / "hyprshare";
  paths.cache_dir = env_path("XDG_CACHE_HOME", home / ".cache") / "hyprshare";
  paths.runtime_dir = env_path("XDG_RUNTIME_DIR", "/tmp") / "hyprshare";
  return paths;
}

}  // namespace hyprshare::platform
