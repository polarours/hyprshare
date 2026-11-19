#include "cmd_record.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/recording/screen_recorder.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_record(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare record <start|stop|pause|resume|status>"));
  }

  static hyprshare::recording::ScreenRecorder recorder;

  if (args[1] == "start") {
    const auto output = take_option(args, "--output");
    const auto width_str = take_option(args, "--width");
    const auto height_str = take_option(args, "--height");
    const auto fps_str = take_option(args, "--fps");
    const auto format_str = take_option(args, "--format");

    hyprshare::recording::RecordingConfig config;
    config.format = hyprshare::recording::RecordingFormat::MP4;
    if (format_str) {
      if (*format_str == "webm") {
        config.format = hyprshare::recording::RecordingFormat::WebM;
      } else if (*format_str == "mkv") {
        config.format = hyprshare::recording::RecordingFormat::MKV;
      } else if (*format_str != "mp4") {
        return std::unexpected("unknown recording format: " + *format_str);
      }
    }

    const auto extension = hyprshare::recording::recording_extension(config.format);
    if (output.has_value()) {
      config.output_path = *output;
    } else {
      config.output_path = "recording" + extension;
    }
    if (config.output_path.extension() != extension) {
      config.output_path = config.output_path.string() + extension;
    }

    if (width_str) config.width = static_cast<uint32_t>(std::stoul(*width_str));
    if (height_str) config.height = static_cast<uint32_t>(std::stoul(*height_str));
    if (fps_str) config.fps = static_cast<uint32_t>(std::stoul(*fps_str));

    auto result = recorder.start(config);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Recording started: " << config.output_path << "\n";
    return 0;
  }

  if (args[1] == "stop") {
    auto result = recorder.stop();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Recording stopped\n";
    return 0;
  }

  if (args[1] == "pause") {
    auto result = recorder.pause();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Recording paused\n";
    return 0;
  }

  if (args[1] == "resume") {
    auto result = recorder.resume();
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }
    std::cout << "Recording resumed\n";
    return 0;
  }

  if (args[1] == "status") {
    const auto status = recorder.status();
    std::string status_str;
    switch (status) {
      case hyprshare::recording::RecordingStatus::Idle: status_str = "idle"; break;
      case hyprshare::recording::RecordingStatus::Recording: status_str = "recording"; break;
      case hyprshare::recording::RecordingStatus::Paused: status_str = "paused"; break;
      case hyprshare::recording::RecordingStatus::Stopped: status_str = "stopped"; break;
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"" << status_str << "\",\"duration_ms\":" << recorder.duration_ms()
                << ",\"frames\":" << recorder.frames_written()
                << ",\"output\":\"" << recorder.output_path().string() << "\"}\n";
    } else {
      std::cout << "Recording: " << status_str << " (" << recorder.duration_ms() << "ms)\n";
      std::cout << "  Frames: " << recorder.frames_written() << "\n";
      if (!recorder.output_path().empty()) {
        std::cout << "  Output: " << recorder.output_path() << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown record subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
