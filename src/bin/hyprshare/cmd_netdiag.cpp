#include "cmd_netdiag.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/diagnostics/network_diagnostics.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_netdiag(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare netdiag <interfaces|test|latency|resolve>"));
  }

  hyprshare::diagnostics::NetworkDiagnosticsRunner runner;

  if (args[1] == "interfaces") {
    auto interfaces = runner.list_interfaces();
    if (!interfaces.has_value()) {
      std::cerr << "error: " << interfaces.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < interfaces->size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"name\":\"" << (*interfaces)[i].name
                  << "\",\"address\":\"" << (*interfaces)[i].address
                  << "\",\"up\":" << ((*interfaces)[i].is_up ? "true" : "false") << "}";
      }
      std::cout << "]\n";
    } else {
      for (const auto& iface : *interfaces) {
        std::cout << iface.name << " - " << (iface.is_up ? "UP" : "DOWN") << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "test") {
    auto diagnostics = runner.run_all();
    if (!diagnostics.has_value()) {
      std::cerr << "error: " << diagnostics.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{";
      std::cout << "\"internet_reachable\":" << (diagnostics->internet_reachable ? "true" : "false") << ",";
      std::cout << "\"mdns_available\":" << (diagnostics->mdns_available ? "true" : "false") << ",";
      std::cout << "\"avahi_running\":" << (diagnostics->avahi_running ? "true" : "false");
      std::cout << "}\n";
    } else {
      std::cout << "Internet: " << (diagnostics->internet_reachable ? "reachable" : "not reachable") << "\n";
      std::cout << "mDNS: " << (diagnostics->mdns_available ? "available" : "not available") << "\n";
      std::cout << "Avahi: " << (diagnostics->avahi_running ? "running" : "not running") << "\n";
    }
    return 0;
  }

  if (args[1] == "latency") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare netdiag latency <host> [port]"));
    }

    const auto host = args[2];
    const auto port = args.size() > 3 ? static_cast<uint16_t>(std::stoul(args[3])) : static_cast<uint16_t>(80);

    auto latency = runner.measure_latency(host, port);
    if (!latency.has_value()) {
      std::cerr << "error: " << latency.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"host\":\"" << host << "\",\"port\":" << port
                << ",\"latency_ms\":" << *latency << "}\n";
    } else {
      std::cout << "Latency to " << host << ":" << port << " = " << *latency << "ms\n";
    }
    return 0;
  }

  if (args[1] == "resolve") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare netdiag resolve <hostname>"));
    }

    auto ip = runner.resolve_hostname(args[2]);
    if (!ip.has_value()) {
      std::cerr << "error: " << ip.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"hostname\":\"" << args[2] << "\",\"ip\":\"" << *ip << "\"}\n";
    } else {
      std::cout << args[2] << " -> " << *ip << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown netdiag subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
