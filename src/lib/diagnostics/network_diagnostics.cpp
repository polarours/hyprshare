#include "hyprshare/diagnostics/network_diagnostics.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <cstdint>
#include <map>
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

  // Default gateway from the kernel route table (destination 00000000).
  std::string default_gateway;
  std::ifstream route("/proc/net/route");
  if (route) {
    std::string line;
    std::getline(route, line);  // header
    while (std::getline(route, line)) {
      std::istringstream fields(line);
      std::string iface_name, destination, gateway;
      fields >> iface_name >> destination >> gateway;
      if (destination == "00000000" && gateway != "00000000") {
        // The route file stores the address as little-endian hex.
        std::uint32_t raw = 0;
        try {
          raw = static_cast<std::uint32_t>(std::stoul(gateway, nullptr, 16));
        } catch (...) {
          continue;
        }
        struct in_addr addr{};
        addr.s_addr = raw;
        char buffer[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer))) {
          default_gateway = buffer;
        }
        break;
      }
    }
  }

  struct ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0) {
    return std::unexpected("failed to enumerate network interfaces");
  }

  std::map<std::string, NetworkInterface> by_name;
  for (auto* current = addresses; current != nullptr; current = current->ifa_next) {
    if (current->ifa_addr == nullptr) continue;
    const std::string name = current->ifa_name;
    auto& iface = by_name[name];
    if (iface.name.empty()) {
      iface.name = name;
      iface.is_loopback = (current->ifa_flags & IFF_LOOPBACK) != 0;
      iface.is_up = (current->ifa_flags & IFF_UP) != 0;
      iface.gateway = iface.is_loopback ? "" : default_gateway;
    }
    if (current->ifa_addr->sa_family == AF_INET) {
      const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
      char buffer[INET_ADDRSTRLEN] = {};
      if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer))) {
        iface.address = buffer;
      }
      if (current->ifa_netmask != nullptr) {
        const auto* mask = reinterpret_cast<const sockaddr_in*>(current->ifa_netmask);
        char mask_buffer[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &mask->sin_addr, mask_buffer, sizeof(mask_buffer))) {
          iface.netmask = mask_buffer;
        }
      }
    }
  }
  freeifaddrs(addresses);

  for (auto& [name, iface] : by_name) {
    interfaces.push_back(std::move(iface));
  }
  return interfaces;
}

std::expected<bool, std::string> NetworkDiagnosticsRunner::test_connectivity(
  const std::string& host, const uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected("failed to create socket: " + std::string(std::strerror(errno)));
  }

  // The CLI advertises `latency <host>`, so resolve names as well as literal
  // IPv4 addresses instead of rejecting anything that is not an IP string.
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  const auto lookup =
    getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &resolved);
  if (lookup != 0 || resolved == nullptr) {
    if (resolved != nullptr) {
      freeaddrinfo(resolved);
    }
    close(fd);
    return std::unexpected("could not resolve " + host + ": " + gai_strerror(lookup));
  }

  timeval timeout{};
  timeout.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  bool connected = false;
  for (auto* candidate = resolved; candidate != nullptr; candidate = candidate->ai_next) {
    if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) {
      connected = true;
      break;
    }
  }
  freeaddrinfo(resolved);
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
