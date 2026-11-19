#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace hyprshare::plugin {

class PluginManager;

// A plugin the user has registered. The path points at the shared object; the
// enabled flag is what the user toggles with `hyprshare plugin enable`.
//
// The name, version, and error fields are filled in once the plugin has been
// loaded; an empty error means the plugin loaded successfully.
struct PluginRegistration {
  std::filesystem::path path;
  bool enabled = true;
  std::string name;
  std::string version;
  std::string error;
};

// Persistent list of registered plugins.
//
// The registry is a small TOML-ish file:
//
//   [plugins]
//   enabled = ["/usr/lib/hyprshare/hello.so"]
//   disabled = ["/opt/hyprshare/legacy.so"]
//
// It lives next to the main configuration so that a registered plugin stays
// registered across CLI invocations.
class PluginRegistry {
 public:
  explicit PluginRegistry(std::filesystem::path file);

  // Registers a plugin. Registering an already known path re-enables it.
  std::expected<void, std::string> add(const std::filesystem::path& path);
  std::expected<void, std::string> remove(const std::filesystem::path& path);
  std::expected<void, std::string> set_enabled(const std::filesystem::path& path, bool enabled);

  std::vector<PluginRegistration> registrations() const;
  bool contains(const std::filesystem::path& path) const;

  const std::filesystem::path& file() const;

 private:
  std::expected<void, std::string> write(const std::vector<PluginRegistration>& entries) const;

  std::filesystem::path file_;
};

// Loads every registered plugin into the manager and applies the stored
// enabled state. A plugin that fails to load is reported in the result
// instead of aborting the whole set.
std::vector<PluginRegistration> load_registered(
  PluginManager& manager, const PluginRegistry& registry);

}  // namespace hyprshare::plugin
