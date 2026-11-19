#pragma once

#include <expected>
#include <string>
#include <vector>

#include "hyprshare/platform/paths.hpp"

namespace hyprshare::core {

struct GeneralConfig {
  std::string default_scope = "local-app";
  std::string discoverability = "paired-only";
  std::string log_level = "info";
};

struct ScreenConfig {
  std::vector<std::string> preferred_backends{"portal-screencast", "obs-virtual-camera"};
  bool allow_fallback_to_obs = true;
};

struct FilesConfig {
  std::string receive_dir = "~/Downloads/hyprshare";
  bool folder_shares_read_only_by_default = true;
};

struct SecurityConfig {
  bool require_explicit_clipboard_approval = true;
  int default_session_ttl_minutes = 30;
};

struct Config {
  GeneralConfig general;
  ScreenConfig screen;
  FilesConfig files;
  SecurityConfig security;
};

std::expected<Config, std::string> load_config(const platform::Paths& paths);
std::expected<void, std::string> save_config(const platform::Paths& paths, const Config& config);

// Dotted key form: <section>.<name>, e.g. "general.log_level".
std::expected<std::string, std::string> get_config_value(const Config& config, const std::string& key);
std::expected<void, std::string> set_config_value(Config& config, const std::string& key, const std::string& value);

}  // namespace hyprshare::core

