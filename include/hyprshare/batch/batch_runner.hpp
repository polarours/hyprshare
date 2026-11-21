#pragma once

#include <expected>
#include <string>
#include <vector>

namespace hyprshare::batch {

struct BatchCommand {
  std::string command;
  std::vector<std::string> args;
  bool continue_on_error = false;
};

struct BatchResult {
  int exit_code = 0;
  std::string output;
  std::string error;
};

class BatchRunner {
 public:
  BatchRunner();
  ~BatchRunner();

  std::expected<std::vector<BatchResult>, std::string> run_file(const std::string& file_path);
  std::expected<std::vector<BatchResult>, std::string> run_commands(const std::vector<BatchCommand>& commands);
  std::expected<BatchResult, std::string> run_single(const std::string& command, const std::vector<std::string>& args);
  // Runs a whole line as a shell command (used by `batch run-commands`, where
  // each argument is a complete command string, e.g. "echo hi").
  std::expected<BatchResult, std::string> run_line(const std::string& line);

  void set_stop_on_error(bool stop);
  bool stop_on_error() const;

 private:
  std::expected<BatchCommand, std::string> parse_line(const std::string& line);

  bool stop_on_error_ = true;
};

}  // namespace hyprshare::batch
