#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace hyprshare::platform {

struct CommandResult {
  int exit_code = -1;
  std::string output;
};

// A process that was started with a pipe on its standard input.
struct SpawnedProcess {
  int pid = -1;
  int stdin_fd = -1;
};

class ProcessRunner {
 public:
  CommandResult run_shell(std::string_view command) const;
  std::expected<int, std::string> spawn_detached_shell(std::string_view command) const;
  std::expected<void, std::string> terminate_detached_process_group(int pid) const;
  bool command_exists(std::string_view command) const;

  // Starts a process whose standard input is a pipe that the caller writes
  // to. The caller closes the pipe with close_stdin() to signal the end of
  // the input.
  std::expected<SpawnedProcess, std::string> spawn_with_stdin(std::string_view command) const;
  std::expected<void, std::string> close_stdin(int fd) const;
  // Writes to the pipe of a spawned process.
  std::expected<void, std::string> write_to_stdin(int fd, const void* data, std::size_t size) const;
  // Waits for a process to finish. Returns the exit code, or an error when
  // the process did not finish within the timeout.
  std::expected<int, std::string> wait_for_process(int pid, int timeout_ms) const;
};

}  // namespace hyprshare::platform
