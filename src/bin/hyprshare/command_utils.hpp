#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyprshare::cli {

bool take_flag(std::vector<std::string>& args, std::string_view flag);

std::optional<std::string> take_option(std::vector<std::string>& args, std::string_view name);

std::expected<std::uint16_t, std::string> parse_port_value(std::string_view value);

std::expected<std::uint32_t, std::string> parse_ttl_value(std::string_view value);

std::expected<std::uint32_t, std::string> parse_count_value(std::string_view value);

std::expected<std::uint32_t, std::string> parse_timeout_value(std::string_view value);

std::expected<std::uintmax_t, std::string> parse_byte_count_value(std::string_view value);

std::expected<std::pair<std::string, std::uint16_t>, std::string> parse_endpoint_value(std::string_view endpoint);

std::expected<std::string, std::string> generate_receiver_token();

std::expected<std::optional<std::string>, std::string> resolve_receiver_token(
  const std::optional<std::string>& token,
  bool generate_token,
  bool json);

std::filesystem::path expand_home_path(const std::filesystem::path& path);

std::expected<std::filesystem::path, std::string> resolve_user_path(const std::filesystem::path& path);

}  // namespace hyprshare::cli
