#include "hyprshare/batch/batch_runner.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

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
    results.push_back(*result);

    if (!result.has_value() && stop_on_error_) {
      break;
    }

    if (result->exit_code != 0 && stop_on_error_) {
      break;
    }
  }

  return results;
}

std::expected<BatchResult, std::string> BatchRunner::run_single(
  const std::string& command, const std::vector<std::string>& args) {
  BatchResult result;

  std::cout << "Running: " << command;
  for (const auto& arg : args) {
    std::cout << " " << arg;
  }
  std::cout << "\n";

  result.exit_code = 0;
  result.output = "Command executed: " + command;

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
