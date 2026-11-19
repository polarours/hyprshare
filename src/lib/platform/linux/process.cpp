#include "hyprshare/platform/process.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <chrono>
#include <thread>
#include <chrono>
#include <thread>
#include <unistd.h>

#include "hyprshare/util/string.hpp"

extern char** environ;

namespace hyprshare::platform {

CommandResult ProcessRunner::run_shell(const std::string_view command) const {
  // The output goes to a file instead of a pipe. A program that leaves a
  // background process behind, such as wl-copy, keeps the pipe open, and the
  // read would then never see the end of the output.
  static std::atomic<unsigned long> counter{0};
  const auto output_path = std::filesystem::temp_directory_path() /
    ("hyprshare-run-" + std::to_string(getpid()) + "-" +
      std::to_string(counter.fetch_add(1)) + ".log");

  // The process is started with posix_spawn instead of fork. Forking a process
  // that has several threads running can leave the child stuck on a lock that
  // another thread held at that moment, and the child would then never finish.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output_path.c_str(),
    O_WRONLY | O_CREAT | O_TRUNC, 0644);
  // The error output goes to the same file. This has to happen after the
  // file was opened onto the standard output, or it would replace it.
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, STDIN_FILENO);

  std::string shell_command(command);
  char shell_path[] = "/bin/sh";
  // A non-login shell is enough for the commands this runner executes. A login
  // shell (-l) sources the user's profile, which on this machine starts a
  // heavy shell setup (gitstatus and friends): a `command -v tar` took ~40 ms
  // with -l and ~4 ms with -c, and doctor/status spawn dozens of these.
  char shell_flag[] = "-c";
  char* arguments[] = {
    shell_path,
    shell_flag,
    shell_command.data(),
    nullptr};

  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, "/bin/sh", &actions, nullptr, arguments, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned != 0) {
    return {.exit_code = -1, .output = "failed to start process: " + std::string(std::strerror(spawned))};
  }

  int raw_status = 0;
  while (waitpid(pid, &raw_status, 0) < 0) {
    if (errno != EINTR) {
      return {.exit_code = -1, .output = "failed to wait for the process"};
    }
  }

  std::string output;
  {
    std::ifstream input(output_path, std::ios::binary);
    if (input.is_open()) {
      std::ostringstream buffer;
      buffer << input.rdbuf();
      output = buffer.str();
    }
  }

  std::error_code error;
  std::filesystem::remove(output_path, error);

  if (raw_status == -1) {
    return {.exit_code = -1, .output = "failed to start process"};
  }
  if (WIFSIGNALED(raw_status)) {
    return {.exit_code = -1, .output = "the process was terminated by signal " +
      std::to_string(WTERMSIG(raw_status)) + "\n" + util::trim(output)};
  }

  const int exit_code = WIFEXITED(raw_status) ? WEXITSTATUS(raw_status) : -1;
  return {.exit_code = exit_code, .output = util::trim(output)};
}

std::expected<int, std::string> ProcessRunner::spawn_detached_shell(const std::string_view command) const {
  std::string shell_command(command);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDWR, 0);
  posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, STDIN_FILENO);

  // The child gets a session of its own, so that it can be stopped as a
  // group later on.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);

  char shell_path[] = "/bin/sh";
  char login_flag[] = "-lc";
  char* arguments[] = {
    shell_path,
    login_flag,
    shell_command.data(),
    nullptr};

  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, "/bin/sh", &actions, &attributes, arguments, environ);
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);

  if (spawned != 0) {
    return std::unexpected("failed to start the process: " + std::string(std::strerror(spawned)));
  }

  return static_cast<int>(pid);
}

std::expected<void, std::string> ProcessRunner::terminate_detached_process_group(const int pid) const {
  if (pid <= 0) {
    return std::unexpected("invalid detached process pid");
  }

  if (kill(-pid, SIGTERM) != 0) {
    if (errno == ESRCH) {
      return {};
    }
    return std::unexpected("failed to terminate detached process group: " + std::string(std::strerror(errno)));
  }
  return {};
}

bool ProcessRunner::command_exists(const std::string_view command) const {
  const CommandResult result = run_shell("command -v " + std::string(command));
  return result.exit_code == 0 && !result.output.empty();
}

std::expected<SpawnedProcess, std::string> ProcessRunner::spawn_with_stdin(
  const std::string_view command) const {
  int pipe_fds[2] = {-1, -1};
  if (pipe(pipe_fds) != 0) {
    return std::unexpected("failed to create the input pipe: " + std::string(std::strerror(errno)));
  }

  std::string shell_command(command);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
  // The child must not keep the writing end of the pipe, or the program that
  // reads from it would never see the end of the input.
  posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_RDWR, 0);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

  char shell_path[] = "/bin/sh";
  char command_flag[] = "-c";
  char* arguments[] = {
    shell_path,
    command_flag,
    shell_command.data(),
    nullptr};

  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, "/bin/sh", &actions, nullptr, arguments, environ);
  posix_spawn_file_actions_destroy(&actions);

  if (spawned != 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return std::unexpected("failed to start the process: " + std::string(std::strerror(spawned)));
  }

  close(pipe_fds[0]);
  return SpawnedProcess{.pid = static_cast<int>(pid), .stdin_fd = pipe_fds[1]};
}

std::expected<void, std::string> ProcessRunner::close_stdin(const int fd) const {
  if (fd < 0) {
    return std::unexpected("invalid input pipe");
  }
  if (close(fd) != 0) {
    return std::unexpected("failed to close the input pipe: " + std::string(std::strerror(errno)));
  }
  return {};
}

std::expected<void, std::string> ProcessRunner::write_to_stdin(
  const int fd, const void* data, const std::size_t size) const {
  if (fd < 0) {
    return std::unexpected("invalid input pipe");
  }

  const char* bytes = static_cast<const char*>(data);
  std::size_t written = 0;
  while (written < size) {
    const ssize_t result = write(fd, bytes + written, size - written);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected("failed to write to the input pipe: " + std::string(std::strerror(errno)));
    }
    written += static_cast<std::size_t>(result);
  }
  return {};
}

std::expected<int, std::string> ProcessRunner::wait_for_process(const int pid, const int timeout_ms) const {
  if (pid <= 0) {
    return std::unexpected("invalid process id");
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true) {
    int status = 0;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status)) {
        return std::unexpected("the process was terminated by signal " +
          std::to_string(WTERMSIG(status)));
      }
      return std::unexpected("the process ended without an exit code");
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected("failed to wait for the process: " + std::string(std::strerror(errno)));
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected("the process did not finish within " + std::to_string(timeout_ms) + "ms");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace hyprshare::platform
