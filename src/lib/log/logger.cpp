#include "hyprshare/log/logger.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

namespace hyprshare::log {

Logger::Logger() {}

Logger::~Logger() {
  if (file_stream_.is_open()) {
    file_stream_.close();
  }
}

void Logger::set_level(const Level level) {
  level_ = level;
}

Level Logger::level() const {
  return level_;
}

void Logger::set_file_output(const std::filesystem::path& path) {
  file_path_ = path;
  file_stream_.open(path, std::ios::app);
}

void Logger::set_console_output(const bool enabled) {
  console_output_ = enabled;
}

void Logger::set_callback(LogCallback callback) {
  callback_ = std::move(callback);
}

void Logger::debug(const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  log(Level::Debug, module, message, file, line);
}

void Logger::info(const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  log(Level::Info, module, message, file, line);
}

void Logger::warning(const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  log(Level::Warning, module, message, file, line);
}

void Logger::error(const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  log(Level::Error, module, message, file, line);
}

void Logger::fatal(const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  log(Level::Fatal, module, message, file, line);
}

std::vector<LogEntry> Logger::history() const {
  return history_;
}

void Logger::clear_history() {
  history_.clear();
}

void Logger::log(const Level level, const std::string& module, const std::string& message, const std::string& file, const std::uint32_t line) {
  if (level < level_) {
    return;
  }

  LogEntry entry;
  entry.level = level;
  entry.message = message;
  entry.module = module;
  entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  entry.file = file;
  entry.line = line;

  if (history_.size() >= max_history_) {
    history_.erase(history_.begin());
  }
  history_.push_back(entry);

  if (console_output_) {
    std::cout << format_entry(entry) << "\n";
  }

  if (file_stream_.is_open()) {
    file_stream_ << format_entry(entry) << "\n";
  }

  if (callback_) {
    callback_(entry);
  }
}

std::string Logger::format_entry(const LogEntry& entry) const {
  std::ostringstream stream;
  stream << "[" << level_string(entry.level) << "] "
         << entry.module << ": " << entry.message;
  if (!entry.file.empty()) {
    stream << " (" << entry.file << ":" << entry.line << ")";
  }
  return stream.str();
}

std::string Logger::level_string(const Level level) const {
  switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
  }
  return "UNKNOWN";
}

}  // namespace hyprshare::log
