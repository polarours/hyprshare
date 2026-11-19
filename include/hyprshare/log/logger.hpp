#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::log {

enum class Level {
  Debug,
  Info,
  Warning,
  Error,
  Fatal
};

struct LogEntry {
  Level level;
  std::string message;
  std::string module;
  std::int64_t timestamp;
  std::string file;
  std::uint32_t line;
};

using LogCallback = std::function<void(const LogEntry&)>;

class Logger {
 public:
  Logger();
  ~Logger();

  void set_level(Level level);
  Level level() const;

  void set_file_output(const std::filesystem::path& path);
  void set_console_output(bool enabled);
  void set_callback(LogCallback callback);

  void debug(const std::string& module, const std::string& message, const std::string& file = "", std::uint32_t line = 0);
  void info(const std::string& module, const std::string& message, const std::string& file = "", std::uint32_t line = 0);
  void warning(const std::string& module, const std::string& message, const std::string& file = "", std::uint32_t line = 0);
  void error(const std::string& module, const std::string& message, const std::string& file = "", std::uint32_t line = 0);
  void fatal(const std::string& module, const std::string& message, const std::string& file = "", std::uint32_t line = 0);

  std::vector<LogEntry> history() const;
  void clear_history();

 private:
  void log(Level level, const std::string& module, const std::string& message, const std::string& file, std::uint32_t line);
  std::string format_entry(const LogEntry& entry) const;
  std::string level_string(Level level) const;

  Level level_ = Level::Info;
  bool console_output_ = true;
  std::filesystem::path file_path_;
  std::ofstream file_stream_;
  LogCallback callback_;
  std::vector<LogEntry> history_;
  std::size_t max_history_ = 1000;
};

}  // namespace hyprshare::log
