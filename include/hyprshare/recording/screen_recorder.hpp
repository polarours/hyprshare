#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hyprshare/platform/process.hpp"

namespace hyprshare::recording {

enum class RecordingStatus {
  Idle,
  Recording,
  Paused,
  Stopped,
};

enum class RecordingFormat {
  MP4,
  WebM,
  MKV,
};

struct RecordingConfig {
  uint32_t width = 1920;
  uint32_t height = 1080;
  uint32_t fps = 30;
  RecordingFormat format = RecordingFormat::MP4;
  std::filesystem::path output_path;
  bool record_audio = false;
};

// The file extension that belongs to a container.
std::string recording_extension(RecordingFormat format);

// Removes the padding between the rows of a captured frame, so that the rows
// follow each other closely, which is what the encoder expects.
//
// The capture source pads its rows, so a frame that is handed over as it
// came in would be read as skewed rows by the encoder.
std::optional<std::vector<char>> pack_video_frame(
  const void* data,
  std::size_t size,
  std::uint32_t width,
  std::uint32_t height,
  std::int32_t stride,
  std::uint32_t spa_video_format);

// The pixel format name that ffmpeg uses for a PipeWire video format.
// An empty result means the format cannot be encoded.
std::optional<std::string> ffmpeg_pixel_format(uint32_t spa_video_format);

// The ffmpeg command that turns raw frames into the output file.
std::string build_encoder_command(const RecordingConfig& config,
  const std::string& pixel_format,
  uint32_t width,
  uint32_t height,
  uint32_t fps);

// Feeds raw frames to an ffmpeg process.
class FrameEncoder {
 public:
  FrameEncoder() = default;
  ~FrameEncoder();

  FrameEncoder(const FrameEncoder&) = delete;
  FrameEncoder& operator=(const FrameEncoder&) = delete;

  std::expected<void, std::string> start(const RecordingConfig& config,
    const std::string& pixel_format,
    uint32_t width,
    uint32_t height,
    uint32_t fps);
  std::expected<void, std::string> write_frame(const void* data, std::size_t size);
  std::expected<void, std::string> stop();

  void pause();
  void resume();

  bool is_running() const;
  std::filesystem::path output_path() const;

 private:
  platform::ProcessRunner runner_;
  int pid_ = -1;
  int stdin_fd_ = -1;
  std::filesystem::path output_path_;
};

using RecordingCallback = std::function<void(RecordingStatus, const std::string&)>;

class ScreenRecorder {
 public:
  ScreenRecorder();
  ~ScreenRecorder();

  std::expected<void, std::string> start(const RecordingConfig& config);
  std::expected<void, std::string> pause();
  std::expected<void, std::string> resume();
  std::expected<void, std::string> stop();

  void set_recording_callback(RecordingCallback callback);

  RecordingStatus status() const;
  std::filesystem::path output_path() const;
  uint64_t duration_ms() const;
  // The number of frames that were written to the encoder.
  uint64_t frames_written() const;

 private:
  RecordingStatus status_ = RecordingStatus::Idle;
  RecordingConfig config_;
  RecordingCallback callback_;
  uint64_t start_time_ = 0;
  uint64_t frames_written_ = 0;
  // The capture pipeline. It is kept in an implementation structure so that
  // the recorder does not have to expose PipeWire in its header.
  struct Pipeline;
  std::shared_ptr<Pipeline> pipeline_;
};

}  // namespace hyprshare::recording
