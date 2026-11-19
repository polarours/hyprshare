#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hyprshare::plugin {

struct PluginInfo {
  std::string name;
  std::string version;
  std::string description;
  std::string author;
  std::filesystem::path path;
  bool enabled = true;
};

struct PluginCommand {
  std::string name;
  std::string description;
  std::function<std::expected<int, std::string>(const std::vector<std::string>&)> handler;
};

// Outcome of a bulk plugin directory load.
struct PluginLoadReport {
  std::size_t loaded = 0;
  std::vector<std::string> failures;
};

class Plugin {
 public:
  virtual ~Plugin() = default;

  virtual std::expected<void, std::string> initialize() = 0;
  virtual void shutdown() = 0;

  virtual PluginInfo info() const = 0;
  virtual std::vector<PluginCommand> commands() const = 0;
};

class PluginManager {
 public:
  PluginManager();
  ~PluginManager();

  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

  // Loads a plugin shared object and returns the plugin name. The object must
  // export the plugin factory symbol described in plugin_api.hpp.
  std::expected<std::string, std::string> load_plugin(const std::filesystem::path& path);
  std::expected<void, std::string> unload_plugin(const std::string& name);
  std::expected<void, std::string> enable_plugin(const std::string& name);
  std::expected<void, std::string> disable_plugin(const std::string& name);

  std::vector<PluginInfo> list_plugins() const;
  std::shared_ptr<Plugin> get_plugin(const std::string& name) const;

  // Loads every shared object in a directory. Individual failures are
  // reported in the result instead of aborting the whole directory.
  std::expected<PluginLoadReport, std::string> load_plugins_from_dir(const std::filesystem::path& dir);

  // Commands contributed by all enabled plugins.
  std::vector<PluginCommand> all_commands() const;

 private:
  struct LoadedPlugin {
    std::shared_ptr<Plugin> plugin;
    void* handle = nullptr;
    std::filesystem::path path;
    bool enabled = true;
  };

  std::vector<LoadedPlugin> plugins_;
};

}  // namespace hyprshare::plugin
