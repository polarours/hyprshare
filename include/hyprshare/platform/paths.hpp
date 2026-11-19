#pragma once

#include <filesystem>

namespace hyprshare::platform {

struct Paths {
  std::filesystem::path source_dir;
  std::filesystem::path data_dir;
  std::filesystem::path config_dir;
  std::filesystem::path config_file;
  std::filesystem::path state_dir;
  std::filesystem::path cache_dir;
  std::filesystem::path runtime_dir;
};

Paths resolve_paths();

}  // namespace hyprshare::platform

