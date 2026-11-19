#include "cmd_profiling.hpp"

#include <iostream>
#include <filesystem>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/profiling/profiler.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_profiling(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("profiling"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare profiling <start|stop|report|clear>"));
  }

  static hyprshare::profiling::Profiler profiler;

  if (args[1] == "start") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profiling start <name>"));
    }
    profiler.start(args[2]);
    std::cout << "Started profiling: " << args[2] << "\n";
    return 0;
  }

  if (args[1] == "stop") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare profiling stop <name>"));
    }
    profiler.stop(args[2]);
    std::cout << "Stopped profiling: " << args[2] << "\n";
    return 0;
  }

  if (args[1] == "report") {
    const auto report = profiler.report();
    if (ctx.json) {
      std::cout << "{\"total_duration_us\":" << report.total_duration_us
                << ",\"total_bytes\":" << report.total_bytes
                << ",\"avg_throughput_mbps\":" << report.avg_throughput_mbps
                << ",\"entries\":[";
      for (std::size_t i = 0; i < report.entries.size(); ++i) {
        if (i > 0) std::cout << ",";
        const auto& e = report.entries[i];
        std::cout << "{\"name\":\"" << e.name
                  << "\",\"duration_us\":" << e.duration_us
                  << ",\"bytes\":" << e.bytes_processed
                  << ",\"throughput_mbps\":" << e.throughput_mbps
                  << "}";
      }
      std::cout << "]}";
      std::cout << "\n";
    } else {
      profiler.print_report();
    }
    return 0;
  }

  if (args[1] == "clear") {
    profiler.reset();
    std::cout << "Profiler cleared\n";
    return 0;
  }

  return std::unexpected("unknown profiling subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
