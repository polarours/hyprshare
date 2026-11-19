#include "hyprshare/diagnostics/doctor.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include "hyprshare/core/config.hpp"
#include "hyprshare/core/discovery.hpp"
#include "hyprshare/diagnostics/checks.hpp"
#include "hyprshare/platform/net.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::diagnostics {

using hyprshare::platform::expand_home_path;

namespace {

bool env_contains(std::string_view name, std::string_view needle) {
  if (const char* value = std::getenv(std::string(name).c_str()); value != nullptr) {
    return util::contains_case_insensitive(value, needle);
  }
  return false;
}

bool env_equals(std::string_view name, std::string_view expected) {
  if (const char* value = std::getenv(std::string(name).c_str()); value != nullptr) {
    return util::to_lower(value) == util::to_lower(expected);
  }
  return false;
}

bool user_service_active(const platform::ProcessRunner& runner, const std::string_view unit) {
  if (!runner.command_exists("systemctl")) {
    return false;
  }
  const auto result = runner.run_shell("systemctl --user is-active " + std::string(unit));
  return result.exit_code == 0 && util::to_lower(result.output) == "active";
}

model::CheckResult make_check(
  std::string id,
  const bool success,
  const model::Severity failure_severity,
  std::string summary,
  std::vector<std::string> remediation = {}) {
  model::CheckResult result;
  result.id = std::move(id);
  result.status = success ? model::CheckStatus::Pass : model::CheckStatus::Fail;
  result.severity = success ? model::Severity::Info : failure_severity;
  result.summary = std::move(summary);
  result.remediation = std::move(remediation);
  return result;
}

struct ReceiveDirAssessment {
  bool resolved = false;
  bool exists = false;
  bool ready = false;
  std::filesystem::path path;
  std::string error;
};

std::filesystem::path unique_probe_path(const std::filesystem::path& parent, const std::string_view prefix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 10000; ++attempt) {
    const auto candidate = parent / (std::string(prefix) + std::to_string(getpid()) + "-" + std::to_string(now) + "-" +
                                     std::to_string(attempt));
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return parent / (std::string(prefix) + std::to_string(getpid()) + "-fallback");
}

std::expected<std::filesystem::path, std::string> resolve_receive_dir_path(const core::Config& config) {
  if (config.files.receive_dir.empty()) {
    return std::unexpected("files.receive_dir is empty");
  }

  const auto expanded = expand_home_path(config.files.receive_dir);
  if (expanded.empty()) {
    return std::unexpected("failed to expand files.receive_dir `" + config.files.receive_dir + "`");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(expanded, error);
  if (error) {
    return std::unexpected("failed to resolve files.receive_dir `" + config.files.receive_dir + "`: " + error.message());
  }
  return absolute;
}

std::expected<void, std::string> probe_existing_receive_dir(const std::filesystem::path& directory) {
  if (!std::filesystem::is_directory(directory)) {
    return std::unexpected("receive directory path exists but is not a directory: " + directory.string());
  }

  const auto probe_path = unique_probe_path(directory, ".hyprshare-doctor-write-");
  std::ofstream probe(probe_path);
  if (!probe.is_open()) {
    return std::unexpected("receive directory is not writable: " + directory.string());
  }
  probe << "probe\n";
  probe.close();

  std::error_code error;
  std::filesystem::remove(probe_path, error);
  if (error) {
    return std::unexpected("failed to clean up receive directory probe `" + probe_path.string() + "`: " + error.message());
  }
  return {};
}

std::expected<void, std::string> probe_creatable_receive_dir(const std::filesystem::path& directory) {
  auto parent = directory.parent_path();
  if (parent.empty()) {
    parent = ".";
  }

  if (!std::filesystem::exists(parent)) {
    return std::unexpected("receive directory parent does not exist: " + parent.string());
  }
  if (!std::filesystem::is_directory(parent)) {
    return std::unexpected("receive directory parent is not a directory: " + parent.string());
  }

  const auto probe_path = unique_probe_path(parent, ".hyprshare-doctor-create-");
  std::error_code error;
  std::filesystem::create_directory(probe_path, error);
  if (error) {
    return std::unexpected("receive directory parent is not writable: " + parent.string() + ": " + error.message());
  }

  std::filesystem::remove(probe_path, error);
  if (error) {
    return std::unexpected("failed to clean up receive directory creation probe `" + probe_path.string() + "`: " + error.message());
  }
  return {};
}

ReceiveDirAssessment inspect_receive_dir(const core::Config& config) {
  ReceiveDirAssessment assessment;
  const auto resolved = resolve_receive_dir_path(config);
  if (!resolved.has_value()) {
    assessment.error = resolved.error();
    return assessment;
  }

  assessment.resolved = true;
  assessment.path = *resolved;
  assessment.exists = std::filesystem::exists(*resolved);
  const auto readiness = assessment.exists ? probe_existing_receive_dir(*resolved) : probe_creatable_receive_dir(*resolved);
  if (!readiness.has_value()) {
    assessment.error = readiness.error();
    return assessment;
  }

  assessment.ready = true;
  return assessment;
}

}  // namespace

Doctor::Doctor(const platform::ProcessRunner& runner) : runner_(runner) {
  register_network_checks(registry_);
  register_environment_checks(registry_);
  register_config_checks(registry_);
}

void Doctor::set_fail_threshold(const FailThreshold threshold) {
  fail_threshold_ = threshold;
}

FailThreshold Doctor::fail_threshold() const {
  return fail_threshold_;
}

int Doctor::exit_code_for_report(const model::DoctorReport& report) const {
  if (fail_threshold_ == FailThreshold::None) {
    return 0;
  }

  bool has_warning = false;
  bool has_error = false;
  for (const auto& check : report.checks) {
    if (check.status == model::CheckStatus::Fail) {
      if (check.severity == model::Severity::Error) {
        has_error = true;
      } else {
        has_warning = true;
      }
    }
  }

  if (fail_threshold_ == FailThreshold::Error) {
    return has_error ? 2 : 0;
  }
  if (fail_threshold_ == FailThreshold::Warning) {
    if (has_error) {
      return 2;
    }
    if (has_warning) {
      return 1;
    }
  }
  return 0;
}

model::DoctorReport Doctor::run(
  const platform::Paths& paths,
  const core::AdapterRegistry& adapters,
  const std::optional<std::string_view> app_hint) const {
  model::DoctorReport report;
  const auto config = core::load_config(paths);
  const core::Config effective_config = config.has_value() ? *config : core::Config{};
  const auto receive_dir = inspect_receive_dir(effective_config);
  const auto discovery_status = core::inspect_discovery_status(runner_);

  report.facts["wayland_session"] = env_equals("XDG_SESSION_TYPE", "wayland");
  report.facts["hyprland_desktop"] = env_contains("XDG_CURRENT_DESKTOP", "hyprland") || std::getenv("HYPRLAND_INSTANCE_SIGNATURE") != nullptr;
  report.facts["config_loaded"] = config.has_value();
  report.facts["systemctl_available"] = runner_.command_exists("systemctl");
  report.facts["hyprctl_available"] = runner_.command_exists("hyprctl");
  report.facts["obs_available"] = runner_.command_exists("obs");
  report.facts["xwaylandvideobridge_available"] = runner_.command_exists("xwaylandvideobridge");
  report.facts["tar_available"] = runner_.command_exists("tar");
  report.facts["sha256sum_available"] = runner_.command_exists("sha256sum");
  report.facts["v4l2loopback_present"] = std::filesystem::exists("/sys/module/v4l2loopback");
  report.facts["pipewire_active"] = user_service_active(runner_, "pipewire.service");
  report.facts["wireplumber_active"] = user_service_active(runner_, "wireplumber.service");
  report.facts["xdg_desktop_portal_active"] = user_service_active(runner_, "xdg-desktop-portal.service");
  report.facts["xdph_active"] = user_service_active(runner_, "xdg-desktop-portal-hyprland.service");
  report.facts["avahi_publish_service_available"] = discovery_status.avahi_publish_service_available;
  report.facts["avahi_browse_available"] = discovery_status.avahi_browse_available;
  report.facts["avahi_daemon_active"] = discovery_status.avahi_daemon_active;
  report.facts["discovery_announce_ready"] = discovery_status.ready_to_announce;
  report.facts["discovery_browse_ready"] = discovery_status.ready_to_browse;
  report.facts["data_dir_present"] = std::filesystem::exists(paths.data_dir);
  report.facts["config_dir_present"] = std::filesystem::exists(paths.config_dir);
  report.facts["receive_dir_resolved"] = receive_dir.resolved;
  report.facts["receive_dir_ready"] = receive_dir.ready;

  report.checks.push_back(make_check(
    "config.load",
    report.facts["config_loaded"],
    model::Severity::Error,
    report.facts["config_loaded"] ? "hyprshare config loaded." : "Failed to load hyprshare config: " + config.error(),
    {"Fix the config file syntax or permissions so hyprshare can read `" + paths.config_file.string() + "`."}));

  report.checks.push_back(make_check(
    "session.wayland",
    report.facts["wayland_session"],
    model::Severity::Error,
    report.facts["wayland_session"] ? "Wayland session detected." : "Wayland session not detected.",
    {"Run hyprshare inside a Wayland session for native portal-based screen sharing."}));
  report.checks.push_back(make_check(
    "desktop.hyprland",
    report.facts["hyprland_desktop"],
    model::Severity::Warning,
    report.facts["hyprland_desktop"] ? "Hyprland desktop detected." : "Hyprland desktop markers were not detected.",
    {"Hyprland-first features may degrade outside Hyprland until broader compositor support is added."}));
  report.checks.push_back(make_check(
    "tool.systemctl",
    report.facts["systemctl_available"],
    model::Severity::Warning,
    report.facts["systemctl_available"] ? "systemctl is available for user-service inspection." : "systemctl is not available.",
    {"Install or expose systemd user tooling so hyprshare can inspect portal and PipeWire services."}));
  report.checks.push_back(make_check(
    "files.receive_dir",
    report.facts["receive_dir_ready"],
    model::Severity::Error,
    report.facts["receive_dir_ready"]
      ? (receive_dir.exists ? "Inbox receive directory is writable: " : "Inbox receive directory can be created: ") +
          receive_dir.path.string()
      : "Inbox receive directory is not ready: " + receive_dir.error,
    {"Set `[files].receive_dir` to a writable location or fix the parent directory permissions."}));
  report.checks.push_back(make_check(
    "tool.tar",
    report.facts["tar_available"],
    model::Severity::Warning,
    report.facts["tar_available"] ? "tar is available for directory archives and bundle handling."
                                  : "tar is not available.",
    {"Install `tar` for folder uploads, archive extraction, and staged bundle import/export."}));
  report.checks.push_back(make_check(
    "tool.sha256sum",
    report.facts["sha256sum_available"],
    model::Severity::Warning,
    report.facts["sha256sum_available"] ? "sha256sum is available for payload verification."
                                        : "sha256sum is not available.",
    {"Install `sha256sum` to enable staged payload verification and inbox transfer integrity checks."}));
  report.checks.push_back(make_check(
    "service.pipewire",
    report.facts["pipewire_active"],
    model::Severity::Error,
    report.facts["pipewire_active"] ? "PipeWire user service is active." : "PipeWire user service is not active.",
    {"Start PipeWire in the user session: `systemctl --user start pipewire.service`."}));
  report.checks.push_back(make_check(
    "service.wireplumber",
    report.facts["wireplumber_active"],
    model::Severity::Error,
    report.facts["wireplumber_active"] ? "WirePlumber user service is active." : "WirePlumber user service is not active.",
    {"Start WirePlumber in the user session: `systemctl --user start wireplumber.service`."}));
  report.checks.push_back(make_check(
    "service.xdg_desktop_portal",
    report.facts["xdg_desktop_portal_active"],
    model::Severity::Error,
    report.facts["xdg_desktop_portal_active"] ? "xdg-desktop-portal is active." : "xdg-desktop-portal is not active.",
    {"Start the portal service: `systemctl --user start xdg-desktop-portal.service`."}));
  report.checks.push_back(make_check(
    "service.xdph",
    report.facts["xdph_active"],
    model::Severity::Warning,
    report.facts["xdph_active"] ? "xdg-desktop-portal-hyprland is active." : "xdg-desktop-portal-hyprland is not active.",
    {"Install and start `xdg-desktop-portal-hyprland` for Hyprland-native screencast support."}));

  model::CheckResult obs_check = make_check(
    "tool.obs",
    report.facts["obs_available"],
    model::Severity::Warning,
    report.facts["obs_available"] ? "OBS is available for virtual-camera fallbacks." : "OBS is not installed or not on PATH.",
    {"Install OBS if you want hyprshare to use the virtual camera fallback chain."});
  obs_check.status = report.facts["obs_available"] ? model::CheckStatus::Pass : model::CheckStatus::Warn;
  obs_check.severity = report.facts["obs_available"] ? model::Severity::Info : model::Severity::Warning;
  report.checks.push_back(obs_check);

  model::CheckResult loopback_check = make_check(
    "module.v4l2loopback",
    report.facts["v4l2loopback_present"],
    model::Severity::Warning,
    report.facts["v4l2loopback_present"] ? "v4l2loopback kernel module is present." : "v4l2loopback kernel module is not present.",
    {"Load `v4l2loopback` if your app needs the OBS virtual camera path."});
  loopback_check.status = report.facts["v4l2loopback_present"] ? model::CheckStatus::Pass : model::CheckStatus::Warn;
  loopback_check.severity = report.facts["v4l2loopback_present"] ? model::Severity::Info : model::Severity::Warning;
  report.checks.push_back(loopback_check);

  model::CheckResult avahi_publish_check = make_check(
    "tool.avahi_publish_service",
    report.facts["avahi_publish_service_available"],
    model::Severity::Warning,
    report.facts["avahi_publish_service_available"] ? "avahi-publish-service is available for inbox announcement."
                                                    : "avahi-publish-service is not available.",
    {"Install Avahi tools if you want `hyprshare inbox receive` to announce itself on the LAN."});
  avahi_publish_check.status =
    report.facts["avahi_publish_service_available"] ? model::CheckStatus::Pass : model::CheckStatus::Warn;
  avahi_publish_check.severity =
    report.facts["avahi_publish_service_available"] ? model::Severity::Info : model::Severity::Warning;
  report.checks.push_back(avahi_publish_check);

  model::CheckResult avahi_browse_check = make_check(
    "tool.avahi_browse",
    report.facts["avahi_browse_available"],
    model::Severity::Warning,
    report.facts["avahi_browse_available"] ? "avahi-browse is available for inbox discovery."
                                           : "avahi-browse is not available.",
    {"Install Avahi tools if you want `hyprshare discovery browse` and `inbox put --to-discovered`."});
  avahi_browse_check.status = report.facts["avahi_browse_available"] ? model::CheckStatus::Pass : model::CheckStatus::Warn;
  avahi_browse_check.severity = report.facts["avahi_browse_available"] ? model::Severity::Info : model::Severity::Warning;
  report.checks.push_back(avahi_browse_check);

  model::CheckResult avahi_daemon_check = make_check(
    "service.avahi_daemon",
    report.facts["avahi_daemon_active"],
    model::Severity::Warning,
    report.facts["avahi_daemon_active"] ? "avahi-daemon is active." : "avahi-daemon is not active.",
    {"Start `avahi-daemon.service` to enable LAN inbox announcement and discovery."});
  avahi_daemon_check.status = report.facts["avahi_daemon_active"] ? model::CheckStatus::Pass : model::CheckStatus::Warn;
  avahi_daemon_check.severity = report.facts["avahi_daemon_active"] ? model::Severity::Info : model::Severity::Warning;
  report.checks.push_back(avahi_daemon_check);

  if (app_hint.has_value()) {
    const auto profile = adapters.find_by_hint(*app_hint);
    report.checks.push_back(make_check(
      "adapter.match",
      profile.has_value(),
      model::Severity::Warning,
      profile.has_value() ? "Matched compatibility profile `" + profile->id + "`." : "No compatibility profile matched the requested app.",
      {"Use `hyprshare compat show <app>` to inspect shipped profiles or add a new adapter entry."}));
  }

  const auto registry_checks = registry_.run_all(paths, runner_);
  for (const auto& check : registry_checks) {
    report.checks.push_back(check);
  }

  return report;
}

}  // namespace hyprshare::diagnostics
