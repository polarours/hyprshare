#pragma once

#include <atomic>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::shell {

struct ShellCommand {
  std::string name;
  std::string description;
  std::function<std::expected<int, std::string>(const std::vector<std::string>&)> handler;
};

class InteractiveShell {
 public:
  InteractiveShell();
  ~InteractiveShell();

  void register_command(const ShellCommand& command);
  void register_commands(const std::vector<ShellCommand>& commands);

  std::expected<int, std::string> execute(const std::string& input);
  void run();
  void stop();

  void set_prompt(const std::string& prompt);
  void set_history_size(std::size_t size);

  std::vector<std::string> history() const;
  void clear_history();

 private:
  std::vector<std::string> split_input(const std::string& input);
  void print_help();
  void add_to_history(const std::string& input);

  std::vector<ShellCommand> commands_;
  std::vector<std::string> history_;
  std::string prompt_ = "hyprshare> ";
  std::size_t history_size_ = 1000;
  std::atomic<bool> running_{false};
};

}  // namespace hyprshare::shell
