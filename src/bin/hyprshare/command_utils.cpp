#include "command_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hyprshare::cli {

bool take_flag(std::vector<std::string>& args, const std::string_view flag) {
  const auto it = std::find(args.begin(), args.end(), flag);
  if (it == args.end()) {
    return false;
  }
  args.erase(it);
  return true;
}

std::optional<std::string> take_option(std::vector<std::string>& args, const std::string_view name) {
  for (std::size_t index = 0; index < args.size(); ++index) {
    if (args[index] == name) {
      if (index + 1 >= args.size()) {
        return std::nullopt;
      }
      std::string value = args[index + 1];
      args.erase(args.begin() + static_cast<std::ptrdiff_t>(index), args.begin() + static_cast<std::ptrdiff_t>(index + 2));
      return value;
    }
  }
  return std::nullopt;
}

std::expected<std::uint16_t, std::string> parse_port_value(const std::string_view value) {
  try {
    const auto parsed = std::stoul(std::string(value));
    if (parsed > 65535UL) {
      return std::unexpected("port out of range: " + std::string(value));
    }
    return static_cast<std::uint16_t>(parsed);
  } catch (const std::exception&) {
    return std::unexpected("invalid port value: " + std::string(value));
  }
}

std::expected<std::uint32_t, std::string> parse_ttl_value(const std::string_view value) {
  try {
    const auto parsed = std::stoul(std::string(value));
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected("ttl out of range: " + std::string(value));
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::unexpected("invalid ttl value: " + std::string(value));
  }
}

std::expected<std::uint32_t, std::string> parse_count_value(const std::string_view value) {
  try {
    const auto parsed = std::stoul(std::string(value));
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected("count out of range: " + std::string(value));
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::unexpected("invalid count value: " + std::string(value));
  }
}

std::expected<std::uint32_t, std::string> parse_timeout_value(const std::string_view value) {
  try {
    const auto parsed = std::stoul(std::string(value));
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected("timeout out of range: " + std::string(value));
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::unexpected("invalid timeout value: " + std::string(value));
  }
}

std::expected<std::uintmax_t, std::string> parse_byte_count_value(const std::string_view value) {
  if (value.empty() || std::any_of(value.begin(), value.end(), [](const unsigned char ch) {
        return !std::isdigit(ch);
      })) {
    return std::unexpected("invalid byte count value: " + std::string(value));
  }

  try {
    return static_cast<std::uintmax_t>(std::stoull(std::string(value)));
  } catch (const std::exception&) {
    return std::unexpected("byte count out of range: " + std::string(value));
  }
}

std::expected<std::pair<std::string, std::uint16_t>, std::string> parse_endpoint_value(const std::string_view endpoint) {
  const auto separator = endpoint.rfind(':');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= endpoint.size()) {
    return std::unexpected("endpoint must be in host:port form");
  }

  const auto port = parse_port_value(endpoint.substr(separator + 1));
  if (!port.has_value()) {
    return std::unexpected(port.error());
  }

  return std::pair<std::string, std::uint16_t>{
    std::string(endpoint.substr(0, separator)),
    *port,
  };
}

std::expected<std::string, std::string> generate_receiver_token() {
  std::ifstream input("/dev/urandom", std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open /dev/urandom for receiver token generation");
  }

  std::array<unsigned char, 16> bytes{};
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return std::unexpected("failed to read random bytes for receiver token generation");
  }

  constexpr char hex_digits[] = "0123456789abcdef";
  std::string token;
  token.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    token.push_back(hex_digits[(byte >> 4) & 0x0F]);
    token.push_back(hex_digits[byte & 0x0F]);
  }
  return token;
}

std::expected<std::optional<std::string>, std::string> resolve_receiver_token(
  const std::optional<std::string>& token,
  const bool generate_token,
  const bool json) {
  if (token.has_value() && generate_token) {
    return std::unexpected("--token and --generate-token cannot be used together");
  }
  if (generate_token && json) {
    return std::unexpected("--generate-token cannot be used with --json yet because the token must be visible before the command starts");
  }
  if (!generate_token) {
    return token;
  }
  return generate_receiver_token();
}

std::filesystem::path expand_home_path(const std::filesystem::path& path) {
  const std::string value = path.string();
  if (value == "~") {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return home;
    }
    return {};
  }
  if (value.rfind("~/", 0) == 0) {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / value.substr(2);
    }
    return {};
  }
  return path;
}

std::expected<std::filesystem::path, std::string> resolve_user_path(const std::filesystem::path& path) {
  const auto expanded = expand_home_path(path);
  if (expanded.empty()) {
    return std::unexpected("failed to expand path: " + path.string());
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(expanded, error);
  if (error) {
    return std::unexpected("failed to resolve path `" + path.string() + "`: " + error.message());
  }
  return absolute;
}

}  // namespace hyprshare::cli
