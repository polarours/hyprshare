# hyprshare Plugin Examples

This directory contains example plugins for hyprshare.

## Plugin Structure

A hyprshare plugin is a shared object (`.so`) that:

1. Implements the `Plugin` interface
2. Exports the `hyprshare_plugin_create` factory symbol

The manager loads the object with `dlopen()`, resolves the factory with
`dlsym()`, calls it, and takes ownership of the returned instance.

## Example Plugins

### hello_plugin

A simple example plugin that adds a `hello` command. The plugin class lives in
`hello_plugin.hpp`; `hello_plugin.cpp` is the build entry point that exports
the factory.

```cpp
// hello_plugin.hpp
#include <iostream>

#include "hyprshare/plugin/plugin_manager.hpp"

class HelloPlugin : public hyprshare::plugin::Plugin {
 public:
  std::expected<void, std::string> initialize() override {
    return {};
  }

  void shutdown() override {}

  hyprshare::plugin::PluginInfo info() const override {
    return {
      "hello",
      "1.0.0",
      "A simple hello world plugin",
      "hyprshare",
      "",
      true
    };
  }

  std::vector<hyprshare::plugin::PluginCommand> commands() const override {
    return {
      {"hello", "Say hello", [](const std::vector<std::string>&) -> std::expected<int, std::string> {
        std::cout << "Hello from hyprshare plugin!\n";
        return 0;
      }}
    };
  }
};
```

```cpp
// hello_plugin.cpp
#include "hello_plugin.hpp"

#include "hyprshare/plugin/plugin_api.hpp"

extern "C" HYPRSHARE_PLUGIN_EXPORT hyprshare::plugin::Plugin* hyprshare_plugin_create() {
  return new HelloPlugin();
}
```

The factory must return a heap-allocated instance; the manager deletes it when
the plugin is unloaded.

## Building Plugins

Build the example with the hyprshare build system:

```bash
cmake -S . -B build -G Ninja
cmake --build build
# produces build/hello_plugin.so
```

Or compile a plugin by hand:

```bash
g++ -std=c++23 -shared -fPIC -o hello_plugin.so hello_plugin.cpp \
  -I/path/to/hyprshare/include
```

## Using Plugins

```bash
# Load and register a plugin
hyprshare plugin load ./hello_plugin.so

# List the registered plugins
hyprshare plugin list

# Show the commands contributed by enabled plugins
hyprshare plugin commands

# Run a plugin command
hyprshare hello

# Disable a plugin without unregistering it
hyprshare plugin disable hello

# Remove the registration
hyprshare plugin unload hello
```

`plugin load` registers the plugin in `~/.config/hyprshare/plugins.list`, so it
stays registered across CLI invocations. A disabled plugin stays registered but
contributes no commands.

## Plugin API

### Plugin Interface

```cpp
class Plugin {
 public:
  virtual ~Plugin() = default;

  virtual std::expected<void, std::string> initialize() = 0;
  virtual void shutdown() = 0;

  virtual PluginInfo info() const = 0;
  virtual std::vector<PluginCommand> commands() const = 0;
};
```

### PluginInfo

```cpp
struct PluginInfo {
  std::string name;
  std::string version;
  std::string description;
  std::string author;
  std::filesystem::path path;
  bool enabled = true;
};
```

### PluginCommand

```cpp
struct PluginCommand {
  std::string name;
  std::string description;
  std::function<std::expected<int, std::string>(const std::vector<std::string>&)> handler;
};
```

## Best Practices

1. Keep plugins simple and focused
2. Use the plugin API for all CLI integration
3. Handle errors gracefully
4. Provide helpful error messages
5. Document your plugin's commands
6. Keep the plugin compatible with the `Plugin` ABI of the hyprshare version it
   targets: the interface is header-only, so the plugin has to be built with
   the same compiler and standard as hyprshare itself
