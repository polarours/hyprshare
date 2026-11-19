#pragma once

#include "hyprshare/plugin/plugin_manager.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define HYPRSHARE_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define HYPRSHARE_PLUGIN_EXPORT
#endif

// Plugin ABI.
//
// A plugin is a shared object that exports a single factory symbol. The
// manager resolves it with dlsym() after dlopen()ing the object, calls it, and
// takes ownership of the returned instance.
//
// A minimal plugin looks like this:
//
//   #include "hyprshare/plugin/plugin_api.hpp"
//
//   class MyPlugin : public hyprshare::plugin::Plugin { /* ... */ };
//
//   extern "C" HYPRSHARE_PLUGIN_EXPORT hyprshare::plugin::Plugin* hyprshare_plugin_create() {
//     return new MyPlugin();
//   }
//
// The factory must return a heap-allocated instance; the manager deletes it
// when the plugin is unloaded.
extern "C" HYPRSHARE_PLUGIN_EXPORT hyprshare::plugin::Plugin* hyprshare_plugin_create();

// Name of the symbol the manager looks up in a plugin shared object.
inline constexpr const char* kPluginFactorySymbol = "hyprshare_plugin_create";
