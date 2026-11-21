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

    bool any_failed = false;
    for (const auto& result : *results) {
      if (result.exit_code != 0) {
        std::cerr << "Command failed with exit code " << result.exit_code << "\n";
        if (!result.error.empty()) {
          std::cerr << "  Error: " << result.error << "\n";
        }
        any_failed = true;
      }
    }

    return any_failed ? 1 : 0;
  }

  if (args[1] == "run-commands") {
    // Each remaining argument is a complete shell command line, e.g.
    // `hyprshare batch run-commands "echo hi" "false"` runs both.
    std::vector<std::expected<batch::BatchResult, std::string>> results;
    for (std::size_t i = 2; i < args.size(); ++i) {
      results.push_back(runner.run_line(args[i]));
    }

    bool any_failed = false;
    for (const auto& result : results) {
      if (!result.has_value()) {
        std::cerr << "error: " << result.error() << "\n";
        any_failed = true;
      } else if (result->exit_code != 0) {
        std::cerr << "Command failed with exit code " << result->exit_code << "\n";
        any_failed = true;
      }
    }
    return any_failed ? 1 : 0;
  }

  return std::unexpected("unknown batch subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
