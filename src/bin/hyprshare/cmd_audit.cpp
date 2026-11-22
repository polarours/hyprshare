#include "cmd_audit.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/security/security_auditor.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_audit(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare audit <run|check|list>"));
  }

  hyprshare::security::SecurityAuditor auditor;

  if (args[1] == "run") {
    auto report = auditor.run_audit();
    if (!report.has_value()) {
      std::cerr << "error: " << report.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"passed\":" << report->passed
                << ",\"failed\":" << report->failed
                << ",\"warnings\":" << report->warnings
                << ",\"score\":" << report->score
                << ",\"checks\":[";
      for (std::size_t i = 0; i < report->checks.size(); ++i) {
        if (i > 0) std::cout << ",";
        const auto& check = report->checks[i];
        const char* status_str = "skip";
        switch (check.status) {
          case hyprshare::security::CheckStatus::Pass: status_str = "pass"; break;
          case hyprshare::security::CheckStatus::Fail: status_str = "fail"; break;
          case hyprshare::security::CheckStatus::Warning: status_str = "warn"; break;
          case hyprshare::security::CheckStatus::Skip: status_str = "skip"; break;
        }
        std::cout << "{\"id\":\"" << check.id
                  << "\",\"name\":\"" << check.name
                  << "\",\"status\":\"" << status_str
                  << "\",\"remediation\":\"" << check.remediation << "\"}";
      }
      std::cout << "]}\n";
    } else {
      std::cout << "Security Audit Report\n";
      std::cout << "====================\n";
      std::cout << "Passed: " << report->passed << "\n";
      std::cout << "Failed: " << report->failed << "\n";
      std::cout << "Warnings: " << report->warnings << "\n";
      std::cout << "Score: " << report->score << "%\n\n";

      for (const auto& check : report->checks) {
        std::string status_str;
        switch (check.status) {
          case hyprshare::security::CheckStatus::Pass: status_str = "PASS"; break;
          case hyprshare::security::CheckStatus::Fail: status_str = "FAIL"; break;
          case hyprshare::security::CheckStatus::Warning: status_str = "WARN"; break;
          case hyprshare::security::CheckStatus::Skip: status_str = "SKIP"; break;
        }
        std::cout << "[" << status_str << "] " << check.name << "\n";
        if (!check.remediation.empty()) {
          std::cout << "  " << check.remediation << "\n";
        }
      }
    }
    return 0;
  }

  if (args[1] == "check") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare audit check <check-id>"));
    }

    auto report = auditor.run_check(args[2]);
    if (!report.has_value()) {
      std::cerr << "error: " << report.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"passed\":" << report->passed
                << ",\"failed\":" << report->failed
                << ",\"score\":" << report->score << "}\n";
    } else {
      for (const auto& check : report->checks) {
        std::string status_str;
        switch (check.status) {
          case hyprshare::security::CheckStatus::Pass: status_str = "PASS"; break;
          case hyprshare::security::CheckStatus::Fail: status_str = "FAIL"; break;
          case hyprshare::security::CheckStatus::Warning: status_str = "WARN"; break;
          case hyprshare::security::CheckStatus::Skip: status_str = "SKIP"; break;
        }
        std::cout << "[" << status_str << "] " << check.name << "\n";
        if (!check.remediation.empty()) {
          std::cout << "  " << check.remediation << "\n";
        }
      }
    }
    return 0;
  }

  if (args[1] == "list") {
    const auto checks = auditor.list_checks();
    if (ctx.json) {
      std::cout << "[";
      for (std::size_t i = 0; i < checks.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"id\":\"" << checks[i].id
                  << "\",\"name\":\"" << checks[i].name
                  << "\",\"severity\":\"" << static_cast<int>(checks[i].severity) << "\"}";
      }
      std::cout << "]\n";
    } else {
      for (const auto& check : checks) {
        std::cout << check.id << " - " << check.name << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown audit subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
