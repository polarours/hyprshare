#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "hyprshare/model/types.hpp"

namespace hyprshare::core {

class BackendRegistry {
 public:
  static std::expected<BackendRegistry, std::string> load(const std::filesystem::path& directory);

  [[nodiscard]] const std::vector<model::BackendDescriptor>& all() const;
  [[nodiscard]] std::optional<model::BackendDescriptor> find(std::string_view id) const;

 private:
  explicit BackendRegistry(std::vector<model::BackendDescriptor> backends);

  std::vector<model::BackendDescriptor> backends_;
};

}  // namespace hyprshare::core

