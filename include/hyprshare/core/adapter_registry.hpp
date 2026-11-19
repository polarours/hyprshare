#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "hyprshare/model/types.hpp"

namespace hyprshare::core {

class AdapterRegistry {
 public:
  static std::expected<AdapterRegistry, std::string> load(const std::filesystem::path& directory);

  [[nodiscard]] const std::vector<model::AppProfile>& all() const;
  [[nodiscard]] std::optional<model::AppProfile> find_by_hint(std::string_view hint) const;

 private:
  explicit AdapterRegistry(std::vector<model::AppProfile> profiles);

  std::vector<model::AppProfile> profiles_;
};

}  // namespace hyprshare::core

