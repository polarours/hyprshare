#pragma once

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::config {

struct ValidationError {
  std::string field;
  std::string message;
  std::string severity;
};

struct ValidationRule {
  std::string field;
  std::function<bool(const std::string&)> validator;
  std::string error_message;
};

class ConfigValidator {
 public:
  ConfigValidator();
  ~ConfigValidator();

  void add_rule(const ValidationRule& rule);
  std::vector<ValidationRule> rules() const;

  std::expected<std::vector<ValidationError>, std::string> validate(const std::string& config_content);
  std::expected<void, std::string> validate_field(const std::string& field, const std::string& value);

  static ConfigValidator default_validator();

 private:
  std::vector<ValidationRule> rules_;
};

}  // namespace hyprshare::config
