#include "hyprshare/batch/batch_runner.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include "hyprshare/platform/process.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::batch {

BatchRunner::BatchRunner() {}

BatchRunner::~BatchRunner() {}

std::expected<std::vector<BatchResult>, std::string> BatchRunner::run_file(const std::string& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return std::unexpected("failed to open batch file: " + file_path);
  }

  std::vector<BatchCommand> commands;
  std::string line;

  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    auto command = parse_line(line);
    if (!command.has_value()) {
      return std::unexpected("failed to parse line: " + command.error());
    }
    commands.push_back(*command);
  }

  return run_commands(commands);
}

std::expected<std::vector<BatchResult>, std::string> BatchRunner::run_commands(
  const std::vector<BatchCommand>& commands) {
  std::vector<BatchResult> results;

  for (const auto& command : commands) {
    auto result = run_single(command.command, command.args);
    if (!result.has_value()) {
      // A launch failure is a real failure — never dereference an unexpected.
      BatchResult failed;
      failed.exit_code = -1;
      failed.error = result.error();
      results.push_back(failed);
      if (stop_on_error_ && !command.continue_on_error) {
        break;
      }
      continue;
    }

    const bool failed = result->exit_code != 0;
    results.push_back(*result);

    // `||` on a line means "continue even if this one fails"; stop_on_error_
    // is the global default. Either condition permits continuing.
    if (failed && stop_on_error_ && !command.continue_on_error) {
      break;
    }
  }

  return results;
}

std::expected<BatchResult, std::string> BatchRunner::run_single(
  const std::string& command, const std::vector<std::string>& args) {
  if (command.empty()) {
    return std::unexpected("empty command");
  }
  // Build a shell command with every token quoted so arguments containing
  // spaces or shell metacharacters stay a single argument.
  std::ostringstream stream;
  stream << util::shell_quote(command);
  for (const auto& arg : args) {
    stream << ' ' << util::shell_quote(arg);
  }

  const platform::ProcessRunner runner;
  const platform::CommandResult executed = runner.run_shell(stream.str());

  BatchResult result;
  result.exit_code = executed.exit_code;
  result.output = executed.output;
  if (executed.exit_code != 0) {
    result.error = "command exited with code " + std::to_string(executed.exit_code);
  }
  return result;
}

std::expected<BatchResult, std::string> BatchRunner::run_line(const std::string& line) {
  if (line.empty()) {
    return std::unexpected("empty command line");
  }
  const platform::ProcessRunner runner;
  const platform::CommandResult executed = runner.run_shell(line);
  BatchResult result;
  result.exit_code = executed.exit_code;
  result.output = executed.output;
  if (executed.exit_code != 0) {
    result.error = "command exited with code " + std::to_string(executed.exit_code);
  }
  return result;
}

void BatchRunner::set_stop_on_error(const bool stop) {
  stop_on_error_ = stop;
}

bool BatchRunner::stop_on_error() const {
  return stop_on_error_;
}

std::expected<BatchCommand, std::string> BatchRunner::parse_line(const std::string& line) {
  BatchCommand command;
  std::istringstream stream(line);
  std::string token;

  if (!(stream >> token)) {
    return std::unexpected("empty command");
  }

  command.command = token;

  while (stream >> token) {
    if (token == "||") {
      command.continue_on_error = true;
    } else {
      command.args.push_back(token);
    }
  }

  return command;
}

}  // namespace hyprshare::batch
