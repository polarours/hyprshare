// Test fixture: a valid shared object that does not export the plugin factory
// symbol. Loading it must be rejected with a clear error.

extern "C" int hyprshare_not_a_plugin() {
  return 0;
}
