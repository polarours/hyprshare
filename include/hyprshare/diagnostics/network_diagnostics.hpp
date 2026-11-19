#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace hyprshare::diagnostics {

struct NetworkInterface {
  std::string name;
  std::string address;
  std::string netmask;
  std::string gateway;
  bool is_up = false;
  bool is_loopback = false;
};

struct NetworkTest {
  std::string name;
  bool passed = false;
  std::string message;
  double latency_ms = 0;
};

struct NetworkDiagnostics {
  std::vector<NetworkInterface> interfaces;
  std::vector<NetworkTest> tests;
  std::string dns_server;
  bool internet_reachable = false;
  bool mdns_available = false;
  bool avahi_running = false;
};

class NetworkDiagnosticsRunner {
 public:
  NetworkDiagnosticsRunner();
  ~NetworkDiagnosticsRunner();

  std::expected<NetworkDiagnostics, std::string> run_all();
  std::expected<std::vector<NetworkInterface>, std::string> list_interfaces();
  std::expected<bool, std::string> test_connectivity(const std::string& host, uint16_t port);
  std::expected<double, std::string> measure_latency(const std::string& host, uint16_t port);
  std::expected<bool, std::string> test_mdns();
  std::expected<bool, std::string> test_avahi();
  std::expected<std::string, std::string> resolve_hostname(const std::string& hostname);

 private:
  std::expected<NetworkInterface, std::string> parse_interface(const std::string& line);
};

}  // namespace hyprshare::diagnostics
