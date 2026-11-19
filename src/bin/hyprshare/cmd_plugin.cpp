#include "cmd_plugin.hpp"

#include <filesystem>
#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/plugin/plugin_manager.hpp"
#include "hyprshare/plugin/plugin_registry.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_plugin(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string(
      "usage: hyprshare plugin <list|load|unload|enable|disable|commands>"));
  }

  static hyprshare::plugin::PluginManager manager;
  hyprshare::plugin::PluginRegistry registry(ctx.paths.config_dir / "plugins.list");

  if (args[1] == "list") {
    const auto plugins = hyprshare::plugin::load_registered(manager, registry);
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < plugins.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"name\":\"" << plugins[i].name
                  << "\",\"version\":\"" << plugins[i].version
                  << "\",\"path\":\"" << plugins[i].path.string()
                  << "\",\"enabled\":" << (plugins[i].enabled ? "true" : "false");
        if (!plugins[i].error.empty()) {
          std::cout << ",\"error\":\"" << plugins[i].error << "\"";
        }
        std::cout << "}";
      }
      std::cout << "]\n";
    } else if (plugins.empty()) {
      std::cout << "No plugins registered.\n";
    } else {
      for (const auto& plugin : plugins) {
        if (!plugin.error.empty()) {
          std::cout << plugin.path.string() << " (failed to load: " << plugin.error << ")\n";
          continue;
        }
        std::cout << plugin.name << " v" << plugin.version
                  << " (" << (plugin.enabled ? "enabled" : "disabled") << ")"
                  << " " << plugin.path.string() << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "load") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare plugin load <path>"));
    }

    const std::filesystem::path path = args[2];
    auto loaded = manager.load_plugin(path);
    if (!loaded.has_value()) {
      std::cerr << "error: " << loaded.error() << "\n";
      return 1;
    }

    auto registered = registry.add(path);
    if (!registered.has_value()) {
      std::cerr << "error: " << registered.error() << "\n";
      return 1;
    }

    const auto plugin = manager.get_plugin(*loaded);
    if (ctx.json) {
      std::cout << "{\"status\":\"ok\",\"name\":\"" << *loaded
                << "\",\"version\":\"" << (plugin ? plugin->info().version : "") << "\"}\n";
    } else {
      std::cout << "Plugin loaded: " << *loaded;
      if (plugin) {
        std::cout << " v" << plugin->info().version;
      }
      std::cout << "\n";
    }
    return 0;
  }

  if (args[1] == "unload") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare plugin unload <name>"));
    }

    const auto plugins = hyprshare::plugin::load_registered(manager, registry);
    for (const auto& plugin : plugins) {
      if (plugin.name != args[2]) {
        continue;
      }
      auto removed = registry.remove(plugin.path);
      if (!removed.has_value()) {
        std::cerr << "error: " << removed.error() << "\n";
        return 1;
      }
      std::cout << "Plugin unregistered: " << args[2] << "\n";
      return 0;
    }

    std::cerr << "error: plugin not found: " << args[2] << "\n";
    return 1;
  }

  if (args[1] == "enable" || args[1] == "disable") {
    if (args.size() < 3) {
      return std::unexpected(std::string(
        "usage: hyprshare plugin " + args[1] + " <name>"));
    }

    const bool enable = args[1] == "enable";
    const auto plugins = hyprshare::plugin::load_registered(manager, registry);
    for (const auto& plugin : plugins) {
      if (plugin.name != args[2]) {
        continue;
      }
      auto updated = registry.set_enabled(plugin.path, enable);
      if (!updated.has_value()) {
        std::cerr << "error: " << updated.error() << "\n";
        return 1;
      }
      std::cout << "Plugin " << (enable ? "enabled" : "disabled") << ": " << args[2] << "\n";
      return 0;
    }

    std::cerr << "error: plugin not found: " << args[2] << "\n";
    return 1;
  }

  if (args[1] == "commands") {
    static_cast<void>(hyprshare::plugin::load_registered(manager, registry));
    const auto commands = manager.all_commands();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < commands.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"name\":\"" << commands[i].name
                  << "\",\"description\":\"" << commands[i].description << "\"}";
      }
      std::cout << "]\n";
    } else if (commands.empty()) {
      std::cout << "No plugin commands available.\n";
    } else {
      for (const auto& command : commands) {
        std::cout << command.name << " - " << command.description << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown plugin subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
