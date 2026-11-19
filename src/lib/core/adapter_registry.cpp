#include "hyprshare/core/adapter_registry.hpp"

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

std::expected<model::AppProfile, std::string> parse_profile_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::unexpected("failed to open app profile: " + path.string());
  }

  model::AppProfile profile;
  std::optional<model::Strategy> current_strategy;
  std::string section;
  std::string subsection;
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
      if (current_strategy.has_value()) {
        profile.screen_strategies.push_back(*current_strategy);
        current_strategy.reset();
      }

      if (content.ends_with(':')) {
        section = content.substr(0, content.size() - 1);
        subsection.clear();
        continue;
      }

      const std::size_t colon = content.find(':');
      if (colon == std::string::npos) {
        return std::unexpected("invalid profile line " + std::to_string(line_number) + " in " + path.string());
      }

      const std::string key = util::trim(content.substr(0, colon));
      const std::string value = util::strip_quotes(util::trim(content.substr(colon + 1)));
      if (key == "id") {
        profile.id = value;
      } else if (key == "display_name") {
        profile.display_name = value;
      }
      continue;
    }

    if ((section == "matches" || section == "notes" || section == "launch_commands" || section == "launch_env") &&
        util::starts_with(content, "- ")) {
      auto value = util::strip_quotes(util::trim(content.substr(2)));
      if (section == "matches") {
        profile.matches.push_back(value);
      } else if (section == "notes") {
        profile.notes.push_back(value);
      } else if (section == "launch_commands") {
        profile.launch_commands.push_back(value);
      } else {
        profile.launch_env.push_back(value);
      }
      continue;
    }

    if (section == "screen_strategies") {
      if (indent == 2 && util::starts_with(content, "- backend:")) {
        if (current_strategy.has_value()) {
          profile.screen_strategies.push_back(*current_strategy);
        }
        current_strategy = model::Strategy{};
        current_strategy->backend_id = util::strip_quotes(util::trim(content.substr(std::string("- backend:").size())));
        subsection.clear();
        continue;
      }

      if (!current_strategy.has_value()) {
        return std::unexpected("strategy content before backend declaration in " + path.string());
      }

      if (indent == 4 && content == "when:") {
        subsection = "when";
        continue;
      }

      if (indent == 4) {
        const std::size_t colon = content.find(':');
        if (colon == std::string::npos) {
          return std::unexpected("invalid strategy line " + std::to_string(line_number) + " in " + path.string());
        }
        const std::string key = util::trim(content.substr(0, colon));
        const std::string value = util::strip_quotes(util::trim(content.substr(colon + 1)));
        if (key == "expose_as") {
          current_strategy->expose_as = value;
        }
        continue;
      }

      if (indent == 6 && subsection == "when" && util::starts_with(content, "- ")) {
        current_strategy->conditions.push_back(util::strip_quotes(util::trim(content.substr(2))));
      }
    }
  }

  if (current_strategy.has_value()) {
    profile.screen_strategies.push_back(*current_strategy);
  }

  if (profile.id.empty()) {
    return std::unexpected("profile missing id: " + path.string());
  }

  return profile;
}

bool profile_matches(const model::AppProfile& profile, const std::string_view hint) {
  const std::string lower_hint = util::to_lower(hint);
  if (util::to_lower(profile.id) == lower_hint || util::contains_case_insensitive(profile.display_name, hint)) {
    return true;
  }

  for (const auto& alias : profile.matches) {
    if (util::to_lower(alias) == lower_hint || util::contains_case_insensitive(alias, hint)) {
      return true;
    }
  }

  return false;
}

}  // namespace

AdapterRegistry::AdapterRegistry(std::vector<model::AppProfile> profiles) : profiles_(std::move(profiles)) {}

std::expected<AdapterRegistry, std::string> AdapterRegistry::load(const std::filesystem::path& directory) {
  if (!std::filesystem::exists(directory)) {
    return std::unexpected("compat directory not found: " + directory.string());
  }

  std::vector<model::AppProfile> profiles;
  for (const auto& path : list_yaml_files(directory)) {
    auto profile = parse_profile_file(path);
    if (!profile.has_value()) {
      return std::unexpected(profile.error());
    }
    profiles.push_back(*profile);
  }

  return AdapterRegistry(std::move(profiles));
}

const std::vector<model::AppProfile>& AdapterRegistry::all() const { return profiles_; }

std::optional<model::AppProfile> AdapterRegistry::find_by_hint(const std::string_view hint) const {
  if (hint.empty()) {
    return std::nullopt;
  }

  for (const auto& profile : profiles_) {
    if (profile_matches(profile, hint)) {
      return profile;
    }
  }

  return std::nullopt;
}

}  // namespace hyprshare::core
