#include "hyprshare/plugin/plugin_registry.hpp"

#include <algorithm>
#include <fstream>

#include "hyprshare/plugin/plugin_manager.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::plugin {

namespace {

std::vector<std::string> parse_string_array(const std::string& value) {
  const std::string trimmed = util::trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
    return {};
  }

  std::vector<std::string> items;
  for (const std::string& raw : util::split(trimmed.substr(1, trimmed.size() - 2), ',')) {
    const std::string item = util::trim(raw);
    if (!item.empty()) {
      items.push_back(util::strip_quotes(item));
    }
  }
  return items;
}

std::string format_string_array(const std::vector<std::filesystem::path>& paths) {
  std::string formatted = "[";
  for (std::size_t index = 0; index < paths.size(); ++index) {
    if (index > 0) {
      formatted += ", ";
    }
    formatted += "\"" + paths[index].string() + "\"";
  }
  formatted += "]";
  return formatted;
}

// Registrations are stored as absolute paths: the registry is read from
// whatever directory the CLI happens to run in.
std::filesystem::path normalize(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return canonical;
  }
  return std::filesystem::absolute(path).lexically_normal();
}

}  // namespace

PluginRegistry::PluginRegistry(std::filesystem::path file) : file_(std::move(file)) {}

std::expected<void, std::string> PluginRegistry::add(const std::filesystem::path& path) {
  const auto target = normalize(path);
  auto entries = registrations();
  bool found = false;
  for (auto& entry : entries) {
    if (entry.path == target) {
      entry.enabled = true;
      found = true;
    }
  }
  if (!found) {
    PluginRegistration entry;
    entry.path = target;
    entry.enabled = true;
    entries.push_back(entry);
  }
  return write(entries);
}

std::expected<void, std::string> PluginRegistry::remove(const std::filesystem::path& path) {
  const auto target = normalize(path);
  auto entries = registrations();
  const auto before = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                    [&](const PluginRegistration& entry) { return entry.path == target; }),
    entries.end());
  if (entries.size() == before) {
    return std::unexpected("plugin is not registered: " + path.string());
  }
  return write(entries);
}

std::expected<void, std::string> PluginRegistry::set_enabled(
  const std::filesystem::path& path, const bool enabled) {
  const auto target = normalize(path);
  auto entries = registrations();
  bool found = false;
  for (auto& entry : entries) {
    if (entry.path == target) {
      entry.enabled = enabled;
      found = true;
    }
  }
  if (!found) {
    return std::unexpected("plugin is not registered: " + path.string());
  }
  return write(entries);
}

std::vector<PluginRegistration> PluginRegistry::registrations() const {
  std::vector<PluginRegistration> entries;
  if (!std::filesystem::exists(file_)) {
    return entries;
  }

  std::ifstream input(file_);
  if (!input.is_open()) {
    return entries;
  }

  std::string line;
  while (std::getline(input, line)) {
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string key = util::trim(line.substr(0, equals));
    const std::string value = util::trim(line.substr(equals + 1));
    const bool enabled = key == "enabled";
    if (key != "enabled" && key != "disabled") {
      continue;
    }
    for (const std::string& path : parse_string_array(value)) {
      PluginRegistration entry;
      entry.path = std::filesystem::path(path);
      entry.enabled = enabled;
      entries.push_back(entry);
    }
  }

  return entries;
}

bool PluginRegistry::contains(const std::filesystem::path& path) const {
  const auto target = normalize(path);
  for (const auto& entry : registrations()) {
    if (entry.path == target) {
      return true;
    }
  }
  return false;
}

const std::filesystem::path& PluginRegistry::file() const {
  return file_;
}

std::expected<void, std::string> PluginRegistry::write(
  const std::vector<PluginRegistration>& entries) const {  std::vector<std::filesystem::path> enabled;
  std::vector<std::filesystem::path> disabled;
  for (const auto& entry : entries) {
    if (entry.enabled) {
      enabled.push_back(entry.path);
    } else {
      disabled.push_back(entry.path);
    }
  }

  std::error_code error;
  if (file_.has_parent_path()) {
    std::filesystem::create_directories(file_.parent_path(), error);
  }

  std::ofstream output(file_, std::ios::trunc);
  if (!output.is_open()) {
    return std::unexpected("failed to open plugin registry for writing: " + file_.string());
  }

  output << "# hyprshare plugin registry\n\n";
  output << "[plugins]\n";
  output << "enabled = " << format_string_array(enabled) << "\n";
  output << "disabled = " << format_string_array(disabled) << "\n";

  if (output.fail()) {
    return std::unexpected("failed to write plugin registry: " + file_.string());
  }
  return {};
}

std::vector<PluginRegistration> load_registered(
  PluginManager& manager, const PluginRegistry& registry) {
  std::vector<PluginRegistration> collected;
  for (const auto& entry : registry.registrations()) {
    PluginRegistration item;
    item.path = entry.path;
    item.enabled = entry.enabled;

    auto loaded = manager.load_plugin(entry.path);
    if (loaded.has_value()) {
      item.name = *loaded;
      if (const auto plugin = manager.get_plugin(item.name)) {
        item.version = plugin->info().version;
      }
      if (entry.enabled) {
        static_cast<void>(manager.enable_plugin(item.name));
      } else {
        static_cast<void>(manager.disable_plugin(item.name));
      }
    } else {
      item.error = loaded.error();
    }

    collected.push_back(item);
  }
  return collected;
}

}  // namespace hyprshare::plugin
