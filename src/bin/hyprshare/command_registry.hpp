#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/launcher.hpp"
#include "hyprshare/core/planner.hpp"
#include "hyprshare/diagnostics/doctor.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/tls/tls_transport.hpp"

namespace hyprshare::cli {

struct CommandContext {
  const platform::Paths& paths;
  const core::Config& config;
  const core::BackendRegistry& backends;
  const core::AdapterRegistry& adapters;
  const platform::ProcessRunner& runner;
  const diagnostics::Doctor& doctor;
  const core::SharePlanner& planner;
  const core::AppLauncher& launcher;
  bool json = false;
  bool dry_run = false;
  bool stage = false;
  bool insecure = false;
  tls::TlsTransportConfig tls_config;
};

using CommandHandler = std::expected<int, std::string>(*)(CommandContext&, std::vector<std::string>&);

struct CommandEntry {
  std::string_view name;
  CommandHandler handler;
  std::string_view usage;
};

class CommandRegistry {
 public:
  void register_command(std::string_view name, CommandHandler handler, std::string_view usage);
  std::expected<int, std::string> dispatch(const std::string& command, CommandContext& ctx, std::vector<std::string>& args) const;
  void print_usage() const;
  bool print_command_usage(std::string_view name) const;

 private:
  std::vector<CommandEntry> commands_;
};

}  // namespace hyprshare::cli
