#include "hyprshare/plugin/plugin_manager.hpp"

#include <dlfcn.h>

#include <cstring>
#include <filesystem>

#include "hyprshare/plugin/plugin_api.hpp"

namespace hyprshare::plugin {

namespace {

using PluginFactory = Plugin* (*)();

}  // namespace

PluginManager::PluginManager() {}

PluginManager::~PluginManager() {
  std::vector<void*> handles;
  handles.reserve(plugins_.size());
  for (auto& entry : plugins_) {
    entry.plugin->shutdown();
    if (entry.handle != nullptr) {
      handles.push_back(entry.handle);
    }
  }
  // Destroy the plugin instances before unloading their shared objects: the
  // virtual destructor lives in the plugin code.
  plugins_.clear();
  for (void* handle : handles) {
    dlclose(handle);
  }
}

std::expected<std::string, std::string> PluginManager::load_plugin(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return std::unexpected("plugin not found: " + path.string());
  }

  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* error = dlerror();
    return std::unexpected("failed to load plugin " + path.string() + ": " +
      (error != nullptr ? error : "unknown dynamic loader error"));
  }

  dlerror();
  void* symbol = dlsym(handle, kPluginFactorySymbol);
  const char* symbol_error = dlerror();
  if (symbol == nullptr || symbol_error != nullptr) {
    dlclose(handle);
    return std::unexpected("plugin " + path.string() + " does not export " +
      std::string(kPluginFactorySymbol));
  }

  // POSIX guarantees dlsym() can return a function pointer, but the direct
  // cast is not standard C++, so copy the representation instead.
  static_assert(sizeof(PluginFactory) == sizeof(void*), "unexpected dlsym pointer size");
  PluginFactory factory = nullptr;
  std::memcpy(&factory, &symbol, sizeof(factory));

  Plugin* instance = factory();
  if (instance == nullptr) {
    dlclose(handle);
    return std::unexpected("plugin factory returned no instance: " + path.string());
  }

  auto plugin = std::shared_ptr<Plugin>(instance);
  auto initialized = plugin->initialize();
  if (!initialized.has_value()) {
    // Destroy the instance while the shared object is still mapped.
    plugin.reset();
    dlclose(handle);
    return std::unexpected("failed to initialize plugin " + path.string() + ": " + initialized.error());
  }

  const auto name = plugin->info().name;
  for (const auto& entry : plugins_) {
    if (entry.plugin->info().name == name) {
      plugin.reset();
      dlclose(handle);
      return std::unexpected("plugin already loaded: " + name);
    }
  }

  plugins_.push_back({plugin, handle, path, true});
  return name;
}

std::expected<void, std::string> PluginManager::unload_plugin(const std::string& name) {
  for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
    if (it->plugin->info().name == name) {
      it->plugin->shutdown();
      void* handle = it->handle;
      // Destroy the instance before unloading its shared object: the virtual
      // destructor lives in the plugin code.
      plugins_.erase(it);
      if (handle != nullptr) {
        dlclose(handle);
      }
      return {};
    }
  }
  return std::unexpected("plugin not found: " + name);
}

std::expected<void, std::string> PluginManager::enable_plugin(const std::string& name) {
  for (auto& entry : plugins_) {
    if (entry.plugin->info().name == name) {
      entry.enabled = true;
      return {};
    }
  }
  return std::unexpected("plugin not found: " + name);
}

std::expected<void, std::string> PluginManager::disable_plugin(const std::string& name) {
  for (auto& entry : plugins_) {
    if (entry.plugin->info().name == name) {
      entry.enabled = false;
      return {};
    }
  }
  return std::unexpected("plugin not found: " + name);
}

std::vector<PluginInfo> PluginManager::list_plugins() const {
  std::vector<PluginInfo> infos;
  for (const auto& entry : plugins_) {
    PluginInfo info = entry.plugin->info();
    info.path = entry.path;
    info.enabled = entry.enabled;
    infos.push_back(info);
  }
  return infos;
}

std::shared_ptr<Plugin> PluginManager::get_plugin(const std::string& name) const {
  for (const auto& entry : plugins_) {
    if (entry.plugin->info().name == name) {
      return entry.plugin;
    }
  }
  return nullptr;
}

std::expected<PluginLoadReport, std::string> PluginManager::load_plugins_from_dir(
  const std::filesystem::path& dir) {
  if (!std::filesystem::exists(dir)) {
    return std::unexpected("plugin directory not found: " + dir.string());
  }

  PluginLoadReport report;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".so") {
      continue;
    }
    auto result = load_plugin(entry.path());
    if (result.has_value()) {
      ++report.loaded;
    } else {
      report.failures.push_back(entry.path().string() + ": " + result.error());
    }
  }

  return report;
}

std::vector<PluginCommand> PluginManager::all_commands() const {
  std::vector<PluginCommand> commands;
  for (const auto& entry : plugins_) {
    if (!entry.enabled) {
      continue;
    }
    auto plugin_commands = entry.plugin->commands();
    commands.insert(commands.end(), plugin_commands.begin(), plugin_commands.end());
  }
  return commands;
}

}  // namespace hyprshare::plugin
