#include "hyprshare/security/security_auditor.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#include "hyprshare/platform/process.hpp"

namespace hyprshare::security {

namespace fs = std::filesystem;

SecurityAuditor::SecurityAuditor() {
  register_checks();
}

SecurityAuditor::~SecurityAuditor() {}

// Runs the check implementation matching an id, never a fixed one.
std::expected<SecurityCheck, std::string> SecurityAuditor::run_one_check(const std::string& id) {
  if (id == "tls_config") return check_tls_config();
  if (id == "file_permissions") return check_file_permissions();
  if (id == "network_security") return check_network_security();
  if (id == "service_security") return check_service_security();
  return std::unexpected("unknown check: " + id);
}

// Applies a fresh check result onto the stored check entry.
void apply_result(SecurityCheck& target, const SecurityCheck& fresh) {
  target.status = fresh.status;
  target.remediation = fresh.remediation;
}

std::expected<SecurityReport, std::string> SecurityAuditor::run_audit() {
  SecurityReport report;

  for (auto& check : checks_) {
    auto result = run_one_check(check.id);
    if (result.has_value()) {
      apply_result(check, *result);
    } else {
      check.status = CheckStatus::Warning;
      check.remediation = "check failed to run: " + result.error();
    }

    switch (check.status) {
      case CheckStatus::Pass: report.passed++; break;
      case CheckStatus::Fail: report.failed++; break;
      case CheckStatus::Warning: report.warnings++; break;
      case CheckStatus::Skip: report.skipped++; break;
    }

    report.checks.push_back(check);
  }

  const auto total = report.passed + report.failed + report.warnings;
  if (total > 0) {
    report.score = 100.0 * report.passed / total;
  }

  return report;
}

std::expected<SecurityReport, std::string> SecurityAuditor::run_check(const std::string& check_id) {
  for (auto& check : checks_) {
    if (check.id == check_id) {
      SecurityReport report;
      auto result = run_one_check(check.id);
      if (result.has_value()) {
        apply_result(check, *result);
      } else {
        check.status = CheckStatus::Warning;
        check.remediation = "check failed to run: " + result.error();
      }

      switch (check.status) {
        case CheckStatus::Pass: report.passed++; break;
        case CheckStatus::Fail: report.failed++; break;
        case CheckStatus::Warning: report.warnings++; break;
        case CheckStatus::Skip: report.skipped++; break;
      }

      report.checks.push_back(check);
      report.score = (check.status == CheckStatus::Pass) ? 100.0 : 0.0;
      return report;
    }
  }

  return std::unexpected("check not found: " + check_id);
}

std::vector<SecurityCheck> SecurityAuditor::list_checks() const {
  return checks_;
}

void SecurityAuditor::register_checks() {
  checks_.push_back({
    .id = "tls_config",
    .name = "TLS Configuration",
    .description = "Check if TLS is properly configured",
    .severity = Severity::High,
    .status = CheckStatus::Skip,
    .remediation = "",
  });

  checks_.push_back({
    .id = "file_permissions",
    .name = "File Permissions",
    .description = "Check file permissions for security",
    .severity = Severity::Medium,
    .status = CheckStatus::Skip,
    .remediation = "",
  });

  checks_.push_back({
    .id = "network_security",
    .name = "Network Security",
    .description = "Check network security settings",
    .severity = Severity::High,
    .status = CheckStatus::Skip,
    .remediation = "",
  });

  checks_.push_back({
    .id = "service_security",
    .name = "Service Security",
    .description = "Check the daemon service is not running as root",
    .severity = Severity::Medium,
    .status = CheckStatus::Skip,
    .remediation = "",
  });
}

static fs::path home_dir() {
  const char* home = getenv("HOME");
  return home ? fs::path(home) : fs::temp_directory_path();
}

std::expected<SecurityCheck, std::string> SecurityAuditor::check_tls_config() {
  SecurityCheck check;
  check.id = "tls_config";
  check.name = "TLS Configuration";
  check.description = "Check if TLS is properly configured";
  check.severity = Severity::High;

  const fs::path keys = home_dir() / ".local/state/hyprshare/keys";
  const fs::path cert = keys / "device.pem";
  const fs::path key = keys / "device.key";
  if (fs::exists(cert) && fs::exists(key)) {
    // A key that is readable by group/other is a real security problem.
    struct stat st{};
    bool key_permissions_ok = true;
    if (stat(key.c_str(), &st) == 0 && (st.st_mode & (S_IRGRP | S_IROTH | S_IWGRP | S_IWOTH))) {
      key_permissions_ok = false;
    }
    if (key_permissions_ok) {
      check.status = CheckStatus::Pass;
      check.remediation = "TLS certificate and key found with safe key permissions.";
    } else {
      check.status = CheckStatus::Warning;
      check.remediation = "Private key is group/world accessible. Run: chmod 600 " + key.string();
    }
  } else if (fs::exists(cert)) {
    check.status = CheckStatus::Warning;
    check.remediation = "Certificate found but no private key. Run 'hyprshare tls generate'.";
  } else {
    check.status = CheckStatus::Warning;
    check.remediation = "No TLS certificate found. Run 'hyprshare tls generate' to create one.";
  }

  return check;
}

std::expected<SecurityCheck, std::string> SecurityAuditor::check_file_permissions() {
  SecurityCheck check;
  check.id = "file_permissions";
  check.name = "File Permissions";
  check.description = "Check hyprshare state directories are not world-writable";
  check.severity = Severity::Medium;

  std::vector<fs::path> dirs = {
    home_dir() / ".config/hyprshare",
    home_dir() / ".local/state/hyprshare",
    home_dir() / ".local/state/hyprshare/keys",
  };
  std::vector<std::string> unsafe;
  for (const auto& dir : dirs) {
    if (!fs::exists(dir)) continue;
    struct stat st{};
    if (stat(dir.c_str(), &st) == 0 && (st.st_mode & S_IWOTH)) {
      unsafe.push_back(dir.string());
    }
  }

  if (unsafe.empty()) {
    check.status = CheckStatus::Pass;
    check.remediation = "No hyprshare directory is world-writable.";
  } else {
    check.status = CheckStatus::Fail;
    check.remediation = "World-writable directories: ";
    for (const auto& p : unsafe) check.remediation += p + " ";
    check.remediation += "(fix with chmod o-w)";
  }
  return check;
}

std::expected<SecurityCheck, std::string> SecurityAuditor::check_network_security() {
  SecurityCheck check;
  check.id = "network_security";
  check.name = "Network Security";
  check.description = "Check that inbound listeners are protected by a token";
  check.severity = Severity::High;

  // hyprshare's inbox/session listeners are token-protected by default.
  // Verify the config file does not explicitly disable the token.
  const fs::path config = home_dir() / ".config/hyprshare/config.toml";
  bool token_disabled = false;
  std::ifstream file(config);
  if (file) {
    std::string line;
    while (std::getline(file, line)) {
      const auto pos = line.find_first_not_of(" \t");
      if (pos == std::string::npos || line[pos] == '#') continue;
      if (line.find("token_required") != std::string::npos &&
          (line.find("false") != std::string::npos || line.find("= 0") != std::string::npos)) {
        token_disabled = true;
      }
    }
  }

  if (token_disabled) {
    check.status = CheckStatus::Fail;
    check.remediation = "Inbox token protection appears disabled in " + config.string() +
      ". Re-enable token_required = true.";
  } else {
    check.status = CheckStatus::Pass;
    check.remediation = "Inbound listeners use token authentication by default.";
  }
  return check;
}

std::expected<SecurityCheck, std::string> SecurityAuditor::check_service_security() {
  SecurityCheck check;
  check.id = "service_security";
  check.name = "Service Security";
  check.description = "Check the daemon service unit is not running as root";
  check.severity = Severity::Medium;

  // The daemon ships as a systemd --user unit. Verify that unit file exists
  // and that the process, if running, is not uid 0.
  const fs::path unit = home_dir() / ".config/systemd/user/hyprshare-daemon.service";
  const platform::ProcessRunner runner;
  const auto uid_result = runner.run_shell("id -u");
  const bool running_as_root = uid_result.exit_code == 0 && uid_result.output == "0";

  if (running_as_root) {
    check.status = CheckStatus::Fail;
    check.remediation = "hyprshare is running as root. Use the systemd --user unit instead.";
  } else if (fs::exists(unit)) {
    check.status = CheckStatus::Pass;
    check.remediation = "User daemon unit present; service does not run as root.";
  } else {
    check.status = CheckStatus::Warning;
    check.remediation = "No user daemon unit at " + unit.string() +
      ". Install it with 'hyprshare daemon install'.";
  }
  return check;
}

}  // namespace hyprshare::security
