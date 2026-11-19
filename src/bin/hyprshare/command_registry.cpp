#include "command_registry.hpp"

#include <iostream>

namespace hyprshare::cli {

namespace {

// Usage blocks are stored as newline-separated invocation lines; print each with
// the same indent so the output stays aligned regardless of block length.
void print_usage_lines(const std::string_view usage) {
  std::size_t start = 0;
  while (start <= usage.size()) {
    const std::size_t end = usage.find('\n', start);
    if (end == std::string_view::npos) {
      std::cout << "  " << usage.substr(start) << "\n";
      break;
    }
    std::cout << "  " << usage.substr(start, end - start) << "\n";
    start = end + 1;
  }
}

}  // namespace

void CommandRegistry::register_command(
  const std::string_view name,
  const CommandHandler handler,
  const std::string_view usage) {
  commands_.push_back({name, handler, usage});
}

std::expected<int, std::string> CommandRegistry::dispatch(
  const std::string& command,
  CommandContext& ctx,
  std::vector<std::string>& args) const {
  for (const auto& entry : commands_) {
    if (entry.name == command) {
      return entry.handler(ctx, args);
    }
  }
  return std::unexpected("unknown command: " + command);
}

void CommandRegistry::print_usage() const {
  std::cout << "hyprshare " << HYPRSHARE_VERSION << "\n\n";
  std::cout << "Usage:\n";
  for (const auto& entry : commands_) {
    print_usage_lines(entry.usage);
  }
  std::cout << "\nOptions:\n";
  std::cout << "  --json       Output in JSON format\n";
  std::cout << "  --dry-run    Plan without executing\n";
  std::cout << "  --stage      Stage for later serving\n";
  std::cout << "  --tls        Enable TLS transport\n";
  std::cout << "  --tls-cert   Path to the TLS certificate\n";
  std::cout << "  --tls-key    Path to the TLS private key\n";
  std::cout << "  --tls-ca     Path to the CA certificate for peer verification\n";
  std::cout << "  --insecure   Skip TLS peer verification\n";
  std::cout << "  --help       Show this help message\n";
}

bool CommandRegistry::print_command_usage(const std::string_view name) const {
  for (const auto& entry : commands_) {
    if (entry.name == name) {
      std::cout << "hyprshare " << HYPRSHARE_VERSION << "\n\n";
      std::cout << "Usage:\n";
      print_usage_lines(entry.usage);
      return true;
    }
  }
  return false;
}

}  // namespace hyprshare::cli
