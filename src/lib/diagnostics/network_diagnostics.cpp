#include "hyprshare/diagnostics/network_diagnostics.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace hyprshare::diagnostics {

NetworkDiagnosticsRunner::NetworkDiagnosticsRunner() {}

NetworkDiagnosticsRunner::~NetworkDiagnosticsRunner() {}

std::expected<NetworkDiagnostics, std::string> NetworkDiagnosticsRunner::run_all() {
  NetworkDiagnostics diagnostics;

  auto interfaces = list_interfaces();
  if (interfaces.has_value()) {
    diagnostics.interfaces = *interfaces;
  }

  diagnostics.internet_reachable = test_connectivity("8.8.8.8", 53).value_or(false);
  diagnostics.mdns_available = test_mdns().value_or(false);
  diagnostics.avahi_running = test_avahi().value_or(false);

  NetworkTest connectivity_test;
  connectivity_test.name = "internet_connectivity";
  connectivity_test.passed = diagnostics.internet_reachable;
  connectivity_test.message = diagnostics.internet_reachable ? "Internet reachable" : "Internet not reachable";
  diagnostics.tests.push_back(connectivity_test);

  NetworkTest mdns_test;
  mdns_test.name = "mdns_available";
  mdns_test.passed = diagnostics.mdns_available;
  mdns_test.message = diagnostics.mdns_available ? "mDNS available" : "mDNS not available";
  diagnostics.tests.push_back(mdns_test);

  NetworkTest avahi_test;
  avahi_test.name = "avahi_running";
  avahi_test.passed = diagnostics.avahi_running;
  avahi_test.message = diagnostics.avahi_running ? "Avahi running" : "Avahi not running";
  diagnostics.tests.push_back(avahi_test);

  return diagnostics;
}

std::expected<std::vector<NetworkInterface>, std::string> NetworkDiagnosticsRunner::list_interfaces() {
  std::vector<NetworkInterface> interfaces;

  std::ifstream net_dev("/proc/net/dev");
  if (!net_dev.is_open()) {
    return std::unexpected("failed to open /proc/net/dev");
  }

  std::string line;
  std::getline(net_dev, line); // skip header
  std::getline(net_dev, line); // skip header

  while (std::getline(net_dev, line)) {
    auto iface = parse_interface(line);
    if (iface.has_value()) {
      interfaces.push_back(*iface);
    }
  }

  return interfaces;
}

std::expected<bool, std::string> NetworkDiagnosticsRunner::test_connectivity(
  const std::string& host, const uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected("failed to create socket: " + std::string(std::strerror(errno)));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    close(fd);
    return std::unexpected("invalid IP address: " + host);
  }

  timeval timeout{};
  timeout.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  const bool connected = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  close(fd);

  return connected;
}

std::expected<double, std::string> NetworkDiagnosticsRunner::measure_latency(
  const std::string& host, const uint16_t port) {
  const auto start = std::chrono::steady_clock::now();

  auto result = test_connectivity(host, port);
  if (!result.has_value()) {
    return std::unexpected(result.error());
  }

  const auto end = std::chrono::steady_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  return static_cast<double>(duration.count()) / 1000.0;
}

std::expected<bool, std::string> NetworkDiagnosticsRunner::test_mdns() {
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return false;
  }

  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(5353);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  const bool bound = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  close(fd);

  return bound;
}

std::expected<bool, std::string> NetworkDiagnosticsRunner::test_avahi() {
  FILE* pipe = popen("pidof avahi-daemon", "r");
  if (!pipe) {
    return false;
  }

  char buffer[128];
  const bool running = fgets(buffer, sizeof(buffer), pipe) != nullptr;
  pclose(pipe);

  return running;
}

std::expected<std::string, std::string> NetworkDiagnosticsRunner::resolve_hostname(
  const std::string& hostname) {
  struct addrinfo hints{}, *result;
  hints.ai_family = AF_INET;

  const int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  if (status != 0) {
    return std::unexpected("failed to resolve hostname: " + std::string(gai_strerror(status)));
  }

  char ip[INET_ADDRSTRLEN];
  auto* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
  inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));

  freeaddrinfo(result);
  return std::string(ip);
}

std::expected<NetworkInterface, std::string> NetworkDiagnosticsRunner::parse_interface(
  const std::string& line) {
  const auto colon_pos = line.find(':');
  if (colon_pos == std::string::npos) {
    return std::unexpected("invalid interface line");
  }

  NetworkInterface iface;
  iface.name = line.substr(0, colon_pos);
  iface.is_loopback = iface.name == "lo";

  return iface;
}

}  // namespace hyprshare::diagnostics
