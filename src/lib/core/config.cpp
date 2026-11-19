#include "hyprshare/core/config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

std::string strip_comments(const std::string& line) {
  bool in_quotes = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    if (line[index] == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (line[index] == '#' && !in_quotes) {
      return util::trim(line.substr(0, index));
    }
  }
  return util::trim(line);
}

bool parse_bool(const std::string_view value) {
  return util::to_lower(value) == "true";
}

int parse_int(const std::string_view value) {
  return std::stoi(std::string(value));
}

std::vector<std::string> parse_string_array(std::string_view value) {
  value = util::trim(value);
  if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
    return {};
  }

  value.remove_prefix(1);
  value.remove_suffix(1);
  const std::vector<std::string> raw_items = util::split(value, ',');

  std::vector<std::string> items;
  for (const std::string& item : raw_items) {
    if (!item.empty()) {
      items.push_back(util::strip_quotes(util::trim(item)));
    }
  }
  return items;
}

}  // namespace

std::expected<Config, std::string> load_config(const platform::Paths& paths) {
  Config config;
  if (!std::filesystem::exists(paths.config_file)) {
    return config;
  }

  std::ifstream input(paths.config_file);
  if (!input.is_open()) {
    return std::unexpected("failed to open config file: " + paths.config_file.string());
  }

  std::string section;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string cleaned = strip_comments(line);
    if (cleaned.empty()) {
      continue;
    }

    if (cleaned.front() == '[' && cleaned.back() == ']') {
      section = util::trim(cleaned.substr(1, cleaned.size() - 2));
      continue;
    }

    const std::size_t equals = cleaned.find('=');
    if (equals == std::string::npos) {
      return std::unexpected("invalid config line " + std::to_string(line_number) + ": " + cleaned);
    }

    const std::string key = util::trim(cleaned.substr(0, equals));
    const std::string value = util::trim(cleaned.substr(equals + 1));

    if (section == "general") {
      if (key == "default_scope") {
        config.general.default_scope = util::strip_quotes(value);
      } else if (key == "discoverability") {
        config.general.discoverability = util::strip_quotes(value);
      } else if (key == "log_level") {
        config.general.log_level = util::strip_quotes(value);
      }
    } else if (section == "screen") {
      if (key == "preferred_backends") {
        config.screen.preferred_backends = parse_string_array(value);
      } else if (key == "allow_fallback_to_obs") {
        config.screen.allow_fallback_to_obs = parse_bool(value);
      }
    } else if (section == "files") {
      if (key == "receive_dir") {
        config.files.receive_dir = util::strip_quotes(value);
      } else if (key == "folder_shares_read_only_by_default") {
        config.files.folder_shares_read_only_by_default = parse_bool(value);
      }
    } else if (section == "security") {
      if (key == "require_explicit_clipboard_approval") {
        config.security.require_explicit_clipboard_approval = parse_bool(value);
      } else if (key == "default_session_ttl_minutes") {
        config.security.default_session_ttl_minutes = parse_int(value);
      }
    }
  }

  return config;
}

std::expected<void, std::string> save_config(const platform::Paths& paths, const Config& config) {
  std::error_code ec;
  if (paths.config_file.has_parent_path()) {
    std::filesystem::create_directories(paths.config_file.parent_path(), ec);
  }

  std::ofstream output(paths.config_file, std::ios::trunc);
  if (!output.is_open()) {
    return std::unexpected("failed to open config file for writing: " + paths.config_file.string());
  }

  const auto join_array = [](const std::vector<std::string>& items) {
    std::string joined = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (i > 0) {
        joined += ", ";
      }
      joined += "\"" + items[i] + "\"";
    }
    joined += "]";
    return joined;
  };
  const auto to_bool = [](bool value) -> std::string { return value ? "true" : "false"; };

  output << "# hyprshare configuration\n\n";

  output << "[general]\n";
  output << "default_scope = \"" << config.general.default_scope << "\"\n";
  output << "discoverability = \"" << config.general.discoverability << "\"\n";
  output << "log_level = \"" << config.general.log_level << "\"\n\n";

  output << "[screen]\n";
  output << "preferred_backends = " << join_array(config.screen.preferred_backends) << "\n";
  output << "allow_fallback_to_obs = " << to_bool(config.screen.allow_fallback_to_obs) << "\n\n";

  output << "[files]\n";
  output << "receive_dir = \"" << config.files.receive_dir << "\"\n";
  output << "folder_shares_read_only_by_default = "
         << to_bool(config.files.folder_shares_read_only_by_default) << "\n\n";

  output << "[security]\n";
  output << "require_explicit_clipboard_approval = "
         << to_bool(config.security.require_explicit_clipboard_approval) << "\n";
  output << "default_session_ttl_minutes = " << config.security.default_session_ttl_minutes << "\n";

  if (output.fail()) {
    return std::unexpected("failed to write config file: " + paths.config_file.string());
  }
  return {};
}

std::expected<std::string, std::string> get_config_value(const Config& config, const std::string& key) {
  const auto dot = key.find('.');
  if (dot == std::string::npos) {
    return std::unexpected("config key must be in form <section>.<name>: " + key);
  }
  const auto section = key.substr(0, dot);
  const auto name = key.substr(dot + 1);

  if (section == "general") {
    if (name == "default_scope") {
      return config.general.default_scope;
    }
    if (name == "discoverability") {
      return config.general.discoverability;
    }
    if (name == "log_level") {
      return config.general.log_level;
    }
  } else if (section == "screen") {
    if (name == "preferred_backends") {
      std::string joined = "[";
      for (std::size_t i = 0; i < config.screen.preferred_backends.size(); ++i) {
        if (i > 0) {
          joined += ", ";
        }
        joined += "\"" + config.screen.preferred_backends[i] + "\"";
      }
      joined += "]";
      return joined;
    }
    if (name == "allow_fallback_to_obs") {
      return config.screen.allow_fallback_to_obs ? "true" : "false";
    }
  } else if (section == "files") {
    if (name == "receive_dir") {
      return config.files.receive_dir;
    }
    if (name == "folder_shares_read_only_by_default") {
      return config.files.folder_shares_read_only_by_default ? "true" : "false";
    }
  } else if (section == "security") {
    if (name == "require_explicit_clipboard_approval") {
      return config.security.require_explicit_clipboard_approval ? "true" : "false";
    }
    if (name == "default_session_ttl_minutes") {
      return std::to_string(config.security.default_session_ttl_minutes);
    }
  }

  return std::unexpected("unknown config key: " + key);
}

std::expected<void, std::string> set_config_value(Config& config, const std::string& key, const std::string& value) {
  const auto dot = key.find('.');
  if (dot == std::string::npos) {
    return std::unexpected("config key must be in form <section>.<name>: " + key);
  }
  const auto section = key.substr(0, dot);
  const auto name = key.substr(dot + 1);
  const auto stripped = util::strip_quotes(util::trim(value));

  if (section == "general") {
    if (name == "default_scope") {
      config.general.default_scope = stripped;
      return {};
    }
    if (name == "discoverability") {
      config.general.discoverability = stripped;
      return {};
    }
    if (name == "log_level") {
      config.general.log_level = stripped;
      return {};
    }
  } else if (section == "screen") {
    if (name == "preferred_backends") {
      auto array_value = util::trim(value);
      if (!array_value.empty() && array_value.front() != '[') {
        array_value = "[" + array_value + "]";
      }
      config.screen.preferred_backends = parse_string_array(array_value);
      return {};
    }
    if (name == "allow_fallback_to_obs") {
      config.screen.allow_fallback_to_obs = parse_bool(stripped);
      return {};
    }
  } else if (section == "files") {
    if (name == "receive_dir") {
      config.files.receive_dir = stripped;
      return {};
    }
    if (name == "folder_shares_read_only_by_default") {
      config.files.folder_shares_read_only_by_default = parse_bool(stripped);
      return {};
    }
  } else if (section == "security") {
    if (name == "require_explicit_clipboard_approval") {
      config.security.require_explicit_clipboard_approval = parse_bool(stripped);
      return {};
    }
    if (name == "default_session_ttl_minutes") {
      try {
        config.security.default_session_ttl_minutes = std::stoi(stripped);
      } catch (const std::exception&) {
        return std::unexpected("invalid integer value for " + key + ": " + value);
      }
      return {};
    }
  }

  return std::unexpected("unknown config key: " + key);
}

}  // namespace hyprshare::core

