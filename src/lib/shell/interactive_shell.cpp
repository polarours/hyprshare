#include "hyprshare/shell/interactive_shell.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace hyprshare::shell {

InteractiveShell::InteractiveShell() {}

InteractiveShell::~InteractiveShell() {
  stop();
}

void InteractiveShell::register_command(const ShellCommand& command) {
  commands_.push_back(command);
}

void InteractiveShell::register_commands(const std::vector<ShellCommand>& commands) {
  commands_.insert(commands_.end(), commands.begin(), commands.end());
}

std::expected<int, std::string> InteractiveShell::execute(const std::string& input) {
  const auto tokens = split_input(input);
  if (tokens.empty()) {
    return 0;
  }

  const auto& command_name = tokens[0];

  if (command_name == "help") {
    print_help();
    return 0;
  }

  if (command_name == "exit" || command_name == "quit") {
    stop();
    return 0;
  }

  if (command_name == "history") {
    for (std::size_t i = 0; i < history_.size(); ++i) {
      std::cout << i + 1 << ": " << history_[i] << "\n";
    }
    return 0;
  }

  if (command_name == "clear") {
    history_.clear();
    return 0;
  }

  for (const auto& cmd : commands_) {
    if (cmd.name == command_name) {
      std::vector<std::string> args(tokens.begin() + 1, tokens.end());
      return cmd.handler(args);
    }
  }

  return std::unexpected("unknown command: " + command_name + ". Type 'help' for available commands.");
}

void InteractiveShell::run() {
  running_ = true;
  std::string input;

  std::cout << "hyprshare interactive shell v0.4.0\n";
  std::cout << "Type 'help' for available commands, 'exit' to quit.\n\n";

  while (running_) {
    std::cout << prompt_;
    // The prompt has no newline, so it would sit in the buffer until the
    // next line is written and the shell would look like it had hung.
    std::cout.flush();
    if (!std::getline(std::cin, input)) {
      break;
    }

    if (input.empty()) {
      continue;
    }

    add_to_history(input);

    const auto result = execute(input);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
    }
  }

  std::cout << "Goodbye!\n";
}

void InteractiveShell::stop() {
  running_ = false;
}

void InteractiveShell::set_prompt(const std::string& prompt) {
  prompt_ = prompt;
}

void InteractiveShell::set_history_size(const std::size_t size) {
  history_size_ = size;
}

std::vector<std::string> InteractiveShell::history() const {
  return history_;
}

void InteractiveShell::clear_history() {
  history_.clear();
}

std::vector<std::string> InteractiveShell::split_input(const std::string& input) {
  std::vector<std::string> tokens;
  std::istringstream stream(input);
  std::string token;

  while (stream >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

void InteractiveShell::print_help() {
  std::cout << "Available commands:\n";
  for (const auto& cmd : commands_) {
    std::cout << "  " << cmd.name << " - " << cmd.description << "\n";
  }
  std::cout << "\nBuilt-in commands:\n";
  std::cout << "  help     - Show this help message\n";
  std::cout << "  history  - Show command history\n";
  std::cout << "  clear    - Clear command history\n";
  std::cout << "  exit     - Exit the shell\n";
}

void InteractiveShell::add_to_history(const std::string& input) {
  if (history_.size() >= history_size_) {
    history_.erase(history_.begin());
  }
  history_.push_back(input);
}

}  // namespace hyprshare::shell
