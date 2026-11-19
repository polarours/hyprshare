// Build entry point for the example plugin.
//
// A plugin shared object only has to export the factory symbol declared in
// plugin_api.hpp. The manager resolves it, calls it, and takes ownership of
// the returned instance.

#include "hello_plugin.hpp"

#include "hyprshare/plugin/plugin_api.hpp"

extern "C" HYPRSHARE_PLUGIN_EXPORT hyprshare::plugin::Plugin* hyprshare_plugin_create() {
  return new HelloPlugin();
}
