#include "hyprshare/diagnostics/check_registry.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "hyprshare/platform/net.hpp"

namespace hyprshare::diagnostics {

namespace {

model::CheckResult check_tcp_loopback(const platform::Paths&, const platform::ProcessRunner&) {
  const auto fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return {"network.tcp_loopback", model::CheckStatus::Fail, model::Severity::Warning,
            "Failed to create TCP socket: " + std::string(std::strerror(errno)),
            {},
            {"Check socket permissions or kernel network configuration."}};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return {"network.tcp_loopback", model::CheckStatus::Fail, model::Severity::Warning,
            "Failed to bind TCP socket to loopback: " + std::string(std::strerror(errno)),
            {},
            {"Check network namespace or socket permissions."}};
  }

  close(fd);
  return {"network.tcp_loopback", model::CheckStatus::Pass, model::Severity::Info,
          "TCP loopback binding works.", {}, {}};
}

model::CheckResult check_dbus_session(const platform::Paths&, const platform::ProcessRunner&) {
  const char* dbus_addr = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  if (dbus_addr == nullptr || *dbus_addr == '\0') {
    return {"env.dbus_session", model::CheckStatus::Fail, model::Severity::Warning,
            "DBUS_SESSION_BUS_ADDRESS is not set.",
            {},
            {"Start a D-Bus session bus for portal and IPC features."}};
  }
  return {"env.dbus_session", model::CheckStatus::Pass, model::Severity::Info,
          "D-Bus session bus is available.", {}, {}};
}

model::CheckResult check_wayland_display(const platform::Paths&, const platform::ProcessRunner&) {
  const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
  if (wayland_display == nullptr || *wayland_display == '\0') {
    return {"env.wayland_display", model::CheckStatus::Fail, model::Severity::Warning,
            "WAYLAND_DISPLAY is not set.",
            {},
            {"Run hyprshare inside a Wayland session."}};
  }
  return {"env.wayland_display", model::CheckStatus::Pass, model::Severity::Info,
          "Wayland display is available.", {}, {}};
}

}  // namespace

void register_network_checks(CheckRegistry& registry) {
  registry.register_check("network.tcp_loopback", check_tcp_loopback);
  registry.register_check("env.dbus_session", check_dbus_session);
  registry.register_check("env.wayland_display", check_wayland_display);
}

}  // namespace hyprshare::diagnostics
