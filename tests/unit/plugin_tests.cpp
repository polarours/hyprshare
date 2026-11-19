#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "hyprshare/plugin/plugin_manager.hpp"
#include "hyprshare/plugin/plugin_registry.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: plugin_tests <path-to-example-plugin.so> [path-to-plugin-without-factory]\n";
    return 2;
  }
  const std::filesystem::path plugin_path = argv[1];
  const std::filesystem::path no_symbol_path = argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path();

  const std::filesystem::path temp_root =
    std::filesystem::temp_directory_path() / "hyprshare-plugin-tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  hyprshare::plugin::PluginManager manager;

  // A manager that has not loaded anything lists nothing.
  {
    const auto plugins = manager.list_plugins();
    expect(plugins.empty(), "a fresh plugin manager should list no plugins");
  }

  // Loading a file that does not exist is rejected.
  {
    const auto missing = manager.load_plugin(temp_root / "absent.so");
    expect(!missing.has_value(), "loading a missing plugin should fail");
  }

  // A file that is not a shared object is rejected by the dynamic loader.
  {
    const auto broken = temp_root / "broken.so";
    std::ofstream out(broken);
    out << "not an ELF object\n";
    out.close();

    const auto loaded = manager.load_plugin(broken);
    expect(!loaded.has_value(), "loading a non-shared-object file should fail");
  }

  // A shared object without the factory symbol is rejected.
  if (!no_symbol_path.empty()) {
    const auto loaded = manager.load_plugin(no_symbol_path);
    expect(!loaded.has_value(), "loading a file without the factory symbol should fail");
  }

  // The real example plugin loads and registers itself.
  {
    const auto loaded = manager.load_plugin(plugin_path);
    expect(loaded.has_value(),
      "the example plugin should load: " + (loaded.has_value() ? "" : loaded.error()));

    const auto plugins = manager.list_plugins();
    expect(plugins.size() == 1, "one plugin should be listed after loading");
    if (!plugins.empty()) {
      expect(plugins.front().name == "hello", "the loaded plugin should be named 'hello'");
      expect(plugins.front().version == "1.0.0", "the loaded plugin should report version 1.0.0");
      expect(plugins.front().enabled, "a freshly loaded plugin should be enabled");
      expect(plugins.front().path == plugin_path, "the listed plugin should carry its load path");
    }

    expect(manager.get_plugin("hello") != nullptr, "the loaded plugin should be retrievable by name");
    expect(manager.get_plugin("absent") == nullptr, "an unknown plugin should not be retrievable");

    const auto commands = manager.all_commands();
    expect(commands.size() == 1, "the enabled plugin should contribute one command");
    if (!commands.empty()) {
      expect(commands.front().name == "hello", "the contributed command should be named 'hello'");
    }
  }

  // Enabling and disabling actually change the reported state.
  {
    const auto disabled = manager.disable_plugin("hello");
    expect(disabled.has_value(), "disabling a loaded plugin should succeed");

    const auto plugins = manager.list_plugins();
    expect(!plugins.empty() && !plugins.front().enabled,
      "a disabled plugin should be reported as disabled");
    expect(manager.all_commands().empty(),
      "a disabled plugin should not contribute commands");

    const auto enabled = manager.enable_plugin("hello");
    expect(enabled.has_value(), "enabling a loaded plugin should succeed");

    const auto reenabled = manager.list_plugins();
    expect(!reenabled.empty() && reenabled.front().enabled,
      "a re-enabled plugin should be reported as enabled");
    expect(manager.all_commands().size() == 1,
      "a re-enabled plugin should contribute commands again");
  }

  // Unknown plugins are reported as such.
  {
    expect(!manager.enable_plugin("absent").has_value(),
      "enabling an unknown plugin should fail");
    expect(!manager.disable_plugin("absent").has_value(),
      "disabling an unknown plugin should fail");
    expect(!manager.unload_plugin("absent").has_value(),
      "unloading an unknown plugin should fail");
  }

  // The same plugin cannot be loaded twice.
  {
    const auto duplicate = manager.load_plugin(plugin_path);
    expect(!duplicate.has_value(), "loading the same plugin twice should fail");
  }

  // Unloading removes it.
  {
    const auto unloaded = manager.unload_plugin("hello");
    expect(unloaded.has_value(), "unloading a loaded plugin should succeed");
    expect(manager.list_plugins().empty(), "an unloaded plugin should no longer be listed");
    expect(manager.get_plugin("hello") == nullptr,
      "an unloaded plugin should not be retrievable");
    expect(manager.all_commands().empty(),
      "an unloaded plugin should not contribute commands");
  }

  // Bulk loading from a directory reports per-file failures without aborting.
  {
    const auto plugin_dir = temp_root / "plugins";
    std::filesystem::create_directories(plugin_dir);
    std::error_code copy_error;
    std::filesystem::copy_file(plugin_path, plugin_dir / "hello_plugin.so",
      std::filesystem::copy_options::overwrite_existing, copy_error);
    expect(!copy_error, "the example plugin should be copyable into the plugin directory");
    {
      std::ofstream broken(plugin_dir / "broken.so");
      broken << "not an ELF object\n";
    }

    const auto report = manager.load_plugins_from_dir(plugin_dir);
    expect(report.has_value(), "loading a plugin directory should succeed");
    if (report.has_value()) {
      expect(report->loaded == 1, "one valid plugin should be loaded from the directory");
      expect(report->failures.size() == 1, "the broken plugin should be reported as a failure");
    }

    const auto missing_dir = manager.load_plugins_from_dir(temp_root / "absent");
    expect(!missing_dir.has_value(), "loading a missing plugin directory should fail");
  }

  // The registry persists plugin registrations across processes.
  {
    // The registry stores absolute paths, so compare against the resolved one.
    const std::filesystem::path resolved_plugin =
      std::filesystem::weakly_canonical(plugin_path);
    const auto registry_file = temp_root / "config" / "plugins.list";
    hyprshare::plugin::PluginRegistry registry(registry_file);

    expect(registry.registrations().empty(), "a fresh registry should be empty");
    expect(!registry.contains(plugin_path), "a fresh registry should not contain the plugin");

    const auto added = registry.add(plugin_path);
    expect(added.has_value(), "registering a plugin should succeed");
    expect(registry.contains(plugin_path), "the registered plugin should be listed");

    auto entries = registry.registrations();
    expect(entries.size() == 1, "the registry should hold one plugin");
    if (!entries.empty()) {
      expect(entries.front().enabled, "a registered plugin should be enabled by default");
      expect(entries.front().path == resolved_plugin,
        "the registry should keep the resolved plugin path");
    }

    // The registration survives a new registry instance reading the same file.
    hyprshare::plugin::PluginRegistry reopened(registry_file);
    expect(reopened.contains(plugin_path), "the registration should persist to disk");
    expect(reopened.registrations().size() == 1, "the reloaded registry should hold one plugin");

    const auto disabled = registry.set_enabled(plugin_path, false);
    expect(disabled.has_value(), "disabling a registered plugin should succeed");
    entries = registry.registrations();
    expect(!entries.empty() && !entries.front().enabled,
      "a disabled plugin should be reported as disabled");

    // Registering again re-enables the plugin.
    expect(registry.add(plugin_path).has_value(), "re-registering a plugin should succeed");
    entries = registry.registrations();
    expect(!entries.empty() && entries.front().enabled,
      "re-registering a plugin should re-enable it");
    expect(entries.size() == 1, "re-registering should not duplicate the entry");

    // Paths containing spaces survive the round trip.
    const auto spaced = temp_root / "my plugins" / "hello plugin.so";
    std::filesystem::create_directories(spaced.parent_path());
    expect(registry.add(spaced).has_value(), "registering a path with spaces should succeed");
    hyprshare::plugin::PluginRegistry spaced_reader(registry_file);
    expect(spaced_reader.contains(spaced), "a path with spaces should survive the round trip");

    const auto removed = registry.remove(plugin_path);
    expect(removed.has_value(), "unregistering a plugin should succeed");
    expect(!registry.contains(plugin_path), "an unregistered plugin should be gone");

    expect(!registry.remove(plugin_path).has_value(),
      "unregistering an unknown plugin should fail");
    expect(!registry.set_enabled(plugin_path, true).has_value(),
      "enabling an unregistered plugin should fail");
  }

  std::filesystem::remove_all(temp_root);

  return test_support::report("plugin");
}
