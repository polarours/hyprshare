#pragma once

#include <iostream>

#include "hyprshare/plugin/plugin_manager.hpp"

// Example plugin implementation.
//
// The companion hello_plugin.cpp exports the plugin factory symbol; see
// examples/plugins/README.md for how to build and load it.
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
