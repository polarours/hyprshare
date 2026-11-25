#include "cmd_profiling.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/profiling/profiler.hpp"

namespace hyprshare::cli {

namespace {

// `profiling start` and `profiling stop` are separate processes, so the entries
// live in a small store on disk. Keeping them in a static Profiler meant the
// report was always empty after a start: every invocation had its own state.
struct StoredEntry {
  std::string name;
  std::int64_t start_us = 0;
  std::int64_t end_us = 0;
  std::uint64_t bytes = 0;
};

std::filesystem::path store_path(const CommandContext& ctx) {
  return ctx.paths.state_dir / "profiling" / "entries.tsv";
}

std::int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::string sanitize(const std::string& value) {
  std::string out = value;
  for (auto& ch : out) {
    if (ch == '\t' || ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }
  return out;
}

std::vector<StoredEntry> load_entries(const CommandContext& ctx) {
  std::vector<StoredEntry> entries;
  std::ifstream in(store_path(ctx));
  if (!in.is_open()) {
    return entries;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream stream(line);
    StoredEntry entry;
    std::string start_raw;
    std::string end_raw;
    std::string bytes_raw;
    if (!std::getline(stream, entry.name, '\t') || !std::getline(stream, start_raw, '\t') ||
        !std::getline(stream, end_raw, '\t') || !std::getline(stream, bytes_raw, '\t')) {
      continue;
    }
    entry.start_us = std::strtoll(start_raw.c_str(), nullptr, 10);
    entry.end_us = std::strtoll(end_raw.c_str(), nullptr, 10);
    entry.bytes = std::strtoull(bytes_raw.c_str(), nullptr, 10);
    entries.push_back(entry);
  }
  return entries;
}

void save_entries(const CommandContext& ctx, const std::vector<StoredEntry>& entries) {
  const auto path = store_path(ctx);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return;
  }
  for (const auto& entry : entries) {
    out << entry.name << '\t' << entry.start_us << '\t' << entry.end_us << '\t' << entry.bytes
        << '\n';
  }
}

StoredEntry* find_entry(std::vector<StoredEntry>& entries, const std::string& name) {
  for (auto& entry : entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

std::int64_t duration_of(const StoredEntry& entry) {
  if (entry.start_us <= 0 || entry.end_us <= 0 || entry.end_us < entry.start_us) {
    return 0;
  }
  return entry.end_us - entry.start_us;
}

}  // namespace

std::expected<int, std::string> handle_profiling(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("profiling"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare profiling <start|stop|report|clear>"));
  }

  if (args[1] == "start") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profiling start <name>"));
    }
    const auto name = sanitize(args[2]);
    auto entries = load_entries(ctx);
    auto* entry = find_entry(entries, name);
    if (entry == nullptr) {
      StoredEntry fresh;
      fresh.name = name;
      entries.push_back(fresh);
      entry = &entries.back();
    }
    entry->start_us = now_us();
    entry->end_us = 0;
    const auto start_us = entry->start_us;
    save_entries(ctx, entries);

    if (ctx.json) {
      std::cout << "{\"status\":\"started\",\"name\":\"" << name
                << "\",\"start_us\":" << start_us << "}\n";
    } else {
      std::cout << "Started profiling: " << name << "\n";
    }
    return 0;
  }

  if (args[1] == "stop") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profiling stop <name>"));
    }
    const auto name = sanitize(args[2]);
    auto entries = load_entries(ctx);
    auto* entry = find_entry(entries, name);
    if (entry == nullptr || entry->start_us <= 0) {
      return std::unexpected("no profiling session named `" + name + "` was started");
    }
    entry->end_us = now_us();
    const auto duration = duration_of(*entry);
    save_entries(ctx, entries);

    if (ctx.json) {
      std::cout << "{\"status\":\"stopped\",\"name\":\"" << name
                << "\",\"duration_us\":" << duration << "}\n";
    } else {
      std::cout << "Stopped profiling: " << name << " (" << duration << " us)\n";
    }
    return 0;
  }

  if (args[1] == "report") {
    const auto entries = load_entries(ctx);

    std::int64_t total_duration = 0;
    std::uint64_t total_bytes = 0;
    for (const auto& entry : entries) {
      total_duration += duration_of(entry);
      total_bytes += entry.bytes;
    }
    const double throughput =
      total_duration > 0
        ? static_cast<double>(total_bytes) * 8.0 / static_cast<double>(total_duration)
        : 0.0;

    if (ctx.json) {
      std::cout << "{\"total_duration_us\":" << total_duration
                << ",\"total_bytes\":" << total_bytes
                << ",\"avg_throughput_mbps\":" << throughput << ",\"entries\":[";
      for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
          std::cout << ",";
        }
        const auto& entry = entries[i];
        const auto duration = duration_of(entry);
        const double entry_throughput =
          duration > 0 ? static_cast<double>(entry.bytes) * 8.0 / static_cast<double>(duration)
                       : 0.0;
        std::cout << "{\"name\":\"" << json_escape(entry.name) << "\",\"duration_us\":"
                  << duration
                  << ",\"bytes\":" << entry.bytes
                  << ",\"throughput_mbps\":" << entry_throughput
                  << ",\"running\":" << ((entry.end_us <= 0) ? "true" : "false") << "}";
      }
      std::cout << "]}\n";
      return 0;
    }

    if (entries.empty()) {
      std::cout << "No profiling data recorded yet. Use `hyprshare profiling start <name>`.\n";
      return 0;
    }
    std::cout << "Profiling report\n";
    for (const auto& entry : entries) {
      std::cout << "  " << entry.name << ": " << duration_of(entry) << " us"
                << (entry.end_us <= 0 ? " (still running)" : "") << "\n";
    }
    std::cout << "  total: " << total_duration << " us\n";
    std::cout << "  bytes: " << total_bytes << "\n";
    return 0;
  }

  if (args[1] == "clear") {
    std::error_code error;
    std::filesystem::remove(store_path(ctx), error);
    if (ctx.json) {
      std::cout << "{\"status\":\"cleared\"}\n";
    } else {
      std::cout << "Profiler cleared\n";
    }
    return 0;
  }

  return std::unexpected("unknown profiling subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
