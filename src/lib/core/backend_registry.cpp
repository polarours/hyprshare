#include "hyprshare/core/backend_registry.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

std::vector<std::filesystem::path> list_yaml_files(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string strip_comments(const std::string& line) {
  return util::trim(line.substr(0, line.find('#')));
}

std::expected<model::BackendDescriptor, std::string> parse_backend_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::unexpected("failed to open backend file: " + path.string());
  }

  model::BackendDescriptor descriptor;
  std::string section;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string cleaned = strip_comments(line);
    if (cleaned.empty()) {
      continue;
    }

    const std::size_t indent = line.find_first_not_of(' ');
    const std::string content = indent == std::string::npos ? std::string() : util::trim(line.substr(indent));

    if (indent == 0 || indent == std::string::npos) {
      if (content.ends_with(':')) {
        section = content.substr(0, content.size() - 1);
        continue;
      }

      const std::size_t colon = content.find(':');
      if (colon == std::string::npos) {
        return std::unexpected("invalid backend line " + std::to_string(line_number) + " in " + path.string());
      }

      const std::string key = util::trim(content.substr(0, colon));
      const std::string value = util::strip_quotes(util::trim(content.substr(colon + 1)));
      if (key == "id") {
        descriptor.id = value;
      } else if (key == "capability") {
        const auto capability = model::parse_capability(value);
        if (!capability.has_value()) {
          return std::unexpected("unknown capability '" + value + "' in " + path.string());
        }
        descriptor.capability = *capability;
      } else if (key == "support_tier") {
        const auto tier = model::parse_support_tier(value);
        if (!tier.has_value()) {
          return std::unexpected("unknown support tier '" + value + "' in " + path.string());
        }
        descriptor.support_tier = *tier;
      } else if (key == "summary") {
        descriptor.summary = value;
      } else if (key == "optional") {
        descriptor.optional = util::to_lower(value) == "true";
      } else if (key == "launch_supported") {
        descriptor.launch_supported = util::to_lower(value) == "true";
      }
      continue;
    }

    if (section == "prerequisites" && util::starts_with(content, "- ")) {
      descriptor.prerequisites.push_back(util::strip_quotes(util::trim(content.substr(2))));
    }
  }

  if (descriptor.id.empty()) {
    return std::unexpected("backend file missing id: " + path.string());
  }
  return descriptor;
}

}  // namespace

BackendRegistry::BackendRegistry(std::vector<model::BackendDescriptor> backends) : backends_(std::move(backends)) {}

std::expected<BackendRegistry, std::string> BackendRegistry::load(const std::filesystem::path& directory) {
  if (!std::filesystem::exists(directory)) {
    return std::unexpected("backend directory not found: " + directory.string());
  }

  std::vector<model::BackendDescriptor> backends;
  for (const auto& path : list_yaml_files(directory)) {
    auto descriptor = parse_backend_file(path);
    if (!descriptor.has_value()) {
      return std::unexpected(descriptor.error());
    }
    backends.push_back(*descriptor);
  }

  return BackendRegistry(std::move(backends));
}

const std::vector<model::BackendDescriptor>& BackendRegistry::all() const { return backends_; }

std::optional<model::BackendDescriptor> BackendRegistry::find(const std::string_view id) const {
  const std::string wanted = util::to_lower(id);
  for (const auto& descriptor : backends_) {
    if (util::to_lower(descriptor.id) == wanted) {
      return descriptor;
    }
  }
  return std::nullopt;
}

}  // namespace hyprshare::core
