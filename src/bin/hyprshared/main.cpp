#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/daemon/server.hpp"
#include "hyprshare/platform/paths.hpp"

namespace {

hyprshare::daemon::DaemonServer* g_server = nullptr;

void signal_handler(int) {
  if (g_server) {
    g_server->stop();
  }
}

template <typename T>
T load_or_exit(std::expected<T, std::string> result) {
  if (!result.has_value()) {
    std::cerr << "error: " << result.error() << "\n";
    std::exit(1);
  }
  return std::move(*result);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);

  if (!args.empty() && (args[0] == "help" || args[0] == "--help")) {
    std::cout
      << "hyprshared - hyprshare daemon\n\n"
      << "Usage:\n"
      << "  hyprshared          Start the daemon\n"
      << "  hyprshared --help   Show this help\n";
    return 0;
  }

  const auto paths = hyprshare::platform::resolve_paths();
  const auto config = load_or_exit(hyprshare::core::load_config(paths));
  const auto backends = load_or_exit(hyprshare::core::BackendRegistry::load(paths.data_dir / "backends"));
  const auto adapters = load_or_exit(hyprshare::core::AdapterRegistry::load(paths.data_dir / "compat" / "apps"));

  hyprshare::daemon::DaemonServer server(paths, config, backends, adapters);
  g_server = &server;

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  if (!server.start()) {
    std::cerr << "error: failed to start daemon\n";
    return 1;
  }

  std::cout << "daemon: running (press Ctrl+C to stop)" << std::endl;

  while (server.is_running()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  server.stop();
  std::cout << "daemon: stopped\n";
  return 0;
}
