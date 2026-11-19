#include "cmd_batch.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/batch/batch_runner.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_batch([[maybe_unused]] CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare batch <run-file|run-commands>"));
  }

  hyprshare::batch::BatchRunner runner;

  if (args[1] == "run-file") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare batch run-file <file>"));
    }

    auto results = runner.run_file(args[2]);
    if (!results.has_value()) {
      std::cerr << "error: " << results.error() << "\n";
      return 1;
    }

    for (const auto& result : *results) {
      if (result.exit_code != 0) {
        std::cerr << "Command failed with exit code " << result.exit_code << "\n";
        if (!result.error.empty()) {
          std::cerr << "  Error: " << result.error << "\n";
        }
      }
    }

    return 0;
  }

  if (args[1] == "run-commands") {
    std::vector<hyprshare::batch::BatchCommand> commands;

    for (std::size_t i = 2; i < args.size(); ++i) {
      hyprshare::batch::BatchCommand cmd;
      cmd.command = args[i];
      commands.push_back(cmd);
    }

    auto results = runner.run_commands(commands);
    if (!results.has_value()) {
      std::cerr << "error: " << results.error() << "\n";
      return 1;
    }

    for (const auto& result : *results) {
      if (result.exit_code != 0) {
        std::cerr << "Command failed with exit code " << result.exit_code << "\n";
      }
    }

    return 0;
  }

  return std::unexpected("unknown batch subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
