#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace hyprshare::security {

enum class Severity {
  Low,
  Medium,
  High,
  Critical
};

enum class CheckStatus {
  Pass,
  Fail,
  Warning,
  Skip
};

struct SecurityCheck {
  std::string id;
  std::string name;
  std::string description;
  Severity severity = Severity::Low;
  CheckStatus status = CheckStatus::Skip;
  std::string remediation;
};

struct SecurityReport {
  std::vector<SecurityCheck> checks;
  uint32_t passed = 0;
  uint32_t failed = 0;
  uint32_t warnings = 0;
  uint32_t skipped = 0;
  double score = 0;
};

class SecurityAuditor {
 public:
  SecurityAuditor();
  ~SecurityAuditor();

  std::expected<SecurityReport, std::string> run_audit();
  std::expected<SecurityReport, std::string> run_check(const std::string& check_id);

  std::vector<SecurityCheck> list_checks() const;

 private:
  void register_checks();
  std::expected<SecurityCheck, std::string> check_tls_config();
  std::expected<SecurityCheck, std::string> check_file_permissions();
  std::expected<SecurityCheck, std::string> check_network_security();
  std::expected<SecurityCheck, std::string> check_service_security();

  std::vector<SecurityCheck> checks_;
};

}  // namespace hyprshare::security
