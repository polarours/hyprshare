#include "hyprshare/config/config_validator.hpp"

#include <sstream>

namespace hyprshare::config {

ConfigValidator::ConfigValidator() {}

ConfigValidator::~ConfigValidator() {}

void ConfigValidator::add_rule(const ValidationRule& rule) {
  rules_.push_back(rule);
}

std::vector<ValidationRule> ConfigValidator::rules() const {
  return rules_;
}

std::expected<std::vector<ValidationError>, std::string> ConfigValidator::validate(const std::string& config_content) {
  std::vector<ValidationError> errors;
  std::istringstream stream(config_content);
  std::string line;
  std::string current_section;

  while (std::getline(stream, line)) {
    const std::string trimmed = [&]() -> std::string {
      auto start = line.find_first_not_of(" \t");
      if (start == std::string::npos) return {};
      auto end = line.find_last_not_of(" \t");
      return line.substr(start, end - start + 1);
    }();

    if (trimmed.empty() || trimmed[0] == '#') continue;

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      current_section = trimmed.substr(1, trimmed.size() - 2);
      continue;
    }

    const auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string::npos) continue;

    const auto key = [&]() -> std::string {
      auto s = trimmed.substr(0, eq_pos);
      auto start = s.find_first_not_of(" \t");
      auto end = s.find_last_not_of(" \t");
      return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }();

    const auto value = [&]() -> std::string {
      auto s = trimmed.substr(eq_pos + 1);
      auto start = s.find_first_not_of(" \t");
      auto end = s.find_last_not_of(" \t");
      return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }();

    const std::string full_key = current_section.empty() ? key : current_section + "." + key;

    for (const auto& rule : rules_) {
      if (rule.field == full_key && !rule.validator(value)) {
        ValidationError error;
        error.field = full_key;
        error.message = rule.error_message;
        error.severity = "error";
        errors.push_back(error);
      }
    }
  }

  return errors;
}

std::expected<void, std::string> ConfigValidator::validate_field(const std::string& field, const std::string& value) {
  for (const auto& rule : rules_) {
    if (rule.field == field && !rule.validator(value)) {
      return std::unexpected(rule.error_message);
    }
  }
  return {};
}

ConfigValidator ConfigValidator::default_validator() {
  ConfigValidator validator;

  validator.add_rule({"general.log_level", [](const std::string& value) {
    return value == "debug" || value == "info" || value == "warning" || value == "error" || value == "fatal";
  }, "Invalid log level"});

  validator.add_rule({"general.discoverability", [](const std::string& value) {
    return value == "paired-only" || value == "open" || value == "disabled";
  }, "Invalid discoverability setting"});

  validator.add_rule({"files.receive_dir", [](const std::string& value) {
    return !value.empty();
  }, "Receive directory cannot be empty"});

  validator.add_rule({"security.default_session_ttl_minutes", [](const std::string& value) {
    try {
      const auto ttl = std::stoul(value);
      return ttl > 0 && ttl <= 1440;
    } catch (...) {
      return false;
    }
  }, "Session TTL must be between 1 and 1440 minutes"});

  return validator;
}

}  // namespace hyprshare::config
