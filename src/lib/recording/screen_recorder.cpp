#include "hyprshare/recording/screen_recorder.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

#include <spa/param/video/raw.h>

#ifdef HYPRSHARE_HAS_PORTAL
#include "hyprshare/portal/pipewire_stream.hpp"
#include "hyprshare/portal/portal_session.hpp"
#endif

namespace hyprshare::recording {

namespace {

// How long the recorder waits for the capture source to announce its format.
constexpr int kFormatTimeoutMs = 5000;
// How long ffmpeg is given to write the output file.
constexpr int kEncoderTimeoutMs = 30000;

}  // namespace

// The number of bytes a pixel takes in a packed format.
std::optional<std::size_t> packed_bytes_per_pixel(const uint32_t spa_format) {
  switch (spa_format) {
    case SPA_VIDEO_FORMAT_BGRx:
    case SPA_VIDEO_FORMAT_BGRA:
    case SPA_VIDEO_FORMAT_ARGB:
    case SPA_VIDEO_FORMAT_RGBx:
    case SPA_VIDEO_FORMAT_RGBA:
    case SPA_VIDEO_FORMAT_ABGR:
    case SPA_VIDEO_FORMAT_xRGB:
    case SPA_VIDEO_FORMAT_xBGR:
      return 4;
    case SPA_VIDEO_FORMAT_RGB:
    case SPA_VIDEO_FORMAT_BGR:
      return 3;
    case SPA_VIDEO_FORMAT_GRAY8:
      return 1;
    case SPA_VIDEO_FORMAT_YUY2:
    case SPA_VIDEO_FORMAT_YVYU:
    case SPA_VIDEO_FORMAT_UYVY:
      return 2;
    default:
      return std::nullopt;
  }
}

// Whether the format stores its chroma in a plane of its own.
bool is_planar_format(const uint32_t spa_format) {
  return spa_format == SPA_VIDEO_FORMAT_NV12 || spa_format == SPA_VIDEO_FORMAT_I420 ||
         spa_format == SPA_VIDEO_FORMAT_YV12;
}

// Removes the padding between the rows, so that the encoder sees the rows one
// after the other. The capture source pads its rows, and the encoder expects
// them to follow each other closely.
std::optional<std::vector<char>> pack_video_frame(
  const void* data,
  const std::size_t size,
  const uint32_t width,
  const uint32_t height,
  const int32_t stride,
  const uint32_t spa_format) {
  if (width == 0 || height == 0) {
    return std::nullopt;
  }

  const auto* bytes = static_cast<const char*>(data);

  if (is_planar_format(spa_format)) {
    // The luma plane comes first, then the chroma plane, which is half as
    // wide and half as tall.
    const std::size_t chroma_height = height / 2;
    const std::size_t chroma_width = width / 2;
    const auto luma_stride = stride > 0 ? static_cast<std::size_t>(stride) : width;
    const auto chroma_stride = luma_stride / 2;

    const std::size_t luma_size = luma_stride * height;
    const std::size_t chroma_plane_size = chroma_stride * chroma_height;
    // NV12 stores an interleaved UV plane; I420/YV12 store two separate ones.
    const bool semi_planar = spa_format == SPA_VIDEO_FORMAT_NV12;
    const std::size_t chroma_planes = semi_planar ? 1 : 2;
    if (size < luma_size + chroma_plane_size * chroma_planes) {
      return std::nullopt;
    }

    std::vector<char> packed;
    packed.reserve(width * height + chroma_width * chroma_height * 2);

    for (uint32_t row = 0; row < height; ++row) {
      packed.insert(packed.end(), bytes + (row * luma_stride), bytes + (row * luma_stride) + width);
    }
    // First chroma plane: interleaved UV for NV12, single plane for I420/YV12.
    for (uint32_t row = 0; row < chroma_height; ++row) {
      const auto* plane = bytes + luma_size + (row * chroma_stride);
      packed.insert(packed.end(), plane, plane + (semi_planar ? chroma_width * 2 : chroma_width));
    }
    // Second chroma plane (I420/YV12 only).
    if (!semi_planar) {
      const std::size_t second_plane = luma_size + chroma_plane_size;
      for (uint32_t row = 0; row < chroma_height; ++row) {
        const auto* plane = bytes + second_plane + (row * chroma_stride);
        packed.insert(packed.end(), plane, plane + chroma_width);
      }
    }
    return packed;
  }

  const auto bytes_per_pixel = packed_bytes_per_pixel(spa_format);
  if (!bytes_per_pixel.has_value()) {
    return std::nullopt;
  }

  const std::size_t row_size = static_cast<std::size_t>(width) * *bytes_per_pixel;
  const auto source_stride = stride > 0 ? static_cast<std::size_t>(stride) : row_size;

  if (source_stride == row_size) {
    // The rows already follow each other closely.
    return std::vector<char>(bytes, bytes + size);
  }

  if (size < source_stride * height) {
    return std::nullopt;
  }

  std::vector<char> packed;
  packed.reserve(row_size * height);
  for (uint32_t row = 0; row < height; ++row) {
    const auto* line = bytes + (row * source_stride);
    packed.insert(packed.end(), line, line + row_size);
  }
  return packed;
}

std::string recording_extension(const RecordingFormat format) {
  switch (format) {
    case RecordingFormat::MP4: return ".mp4";
    case RecordingFormat::WebM: return ".webm";
    case RecordingFormat::MKV: return ".mkv";
  }
  return ".mp4";
}

std::optional<std::string> ffmpeg_pixel_format(const uint32_t spa_video_format) {
  switch (spa_video_format) {
    case SPA_VIDEO_FORMAT_BGRx: return "bgra";
    case SPA_VIDEO_FORMAT_BGRA: return "bgra";
    case SPA_VIDEO_FORMAT_ARGB: return "bgra";
    case SPA_VIDEO_FORMAT_RGBx: return "rgba";
    case SPA_VIDEO_FORMAT_RGBA: return "rgba";
    case SPA_VIDEO_FORMAT_ABGR: return "rgba";
    case SPA_VIDEO_FORMAT_xRGB: return "rgb0";
    case SPA_VIDEO_FORMAT_xBGR: return "bgr0";
    case SPA_VIDEO_FORMAT_RGB: return "rgb24";
    case SPA_VIDEO_FORMAT_BGR: return "bgr24";
    case SPA_VIDEO_FORMAT_NV12: return "nv12";
    case SPA_VIDEO_FORMAT_NV21: return "nv21";
    case SPA_VIDEO_FORMAT_I420: return "yuv420p";
    case SPA_VIDEO_FORMAT_YV12: return "yuv420p";
    case SPA_VIDEO_FORMAT_YUY2: return "yuyv422";
    case SPA_VIDEO_FORMAT_YVYU: return "yuyv422";
    case SPA_VIDEO_FORMAT_UYVY: return "uyvy422";
    case SPA_VIDEO_FORMAT_GRAY8: return "gray";
    default: return std::nullopt;
  }
}

std::string build_encoder_command(const RecordingConfig& config,
  const std::string& pixel_format,
  const uint32_t width,
  const uint32_t height,
  const uint32_t fps) {
  std::string command = "ffmpeg -hide_banner -loglevel error -y";
  command += " -f rawvideo -pixel_format " + pixel_format;
  command += " -video_size " + std::to_string(width) + "x" + std::to_string(height);
  command += " -framerate " + std::to_string(fps);
  command += " -i -";

  switch (config.format) {
    case RecordingFormat::MP4:
    case RecordingFormat::MKV:
      command += " -c:v libx264 -preset veryfast -pix_fmt yuv420p";
      break;
    case RecordingFormat::WebM:
      command += " -c:v libvpx-vp9 -b:v 2M -pix_fmt yuv420p";
      break;
  }

  command += " " + config.output_path.string();
  return command;
}

FrameEncoder::~FrameEncoder() {
  if (pid_ > 0) {
    static_cast<void>(stop());
  }
}

std::expected<void, std::string> FrameEncoder::start(const RecordingConfig& config,
  const std::string& pixel_format,
  const uint32_t width,
  const uint32_t height,
  const uint32_t fps) {
  if (pid_ > 0) {
    return std::unexpected("the encoder is already running");
  }
  if (!runner_.command_exists("ffmpeg")) {
    return std::unexpected("ffmpeg is not available; it is needed to encode the recording");
  }
  if (pixel_format.empty()) {
    return std::unexpected("the capture format cannot be encoded");
  }

  std::error_code error;
  if (config.output_path.has_parent_path()) {
    std::filesystem::create_directories(config.output_path.parent_path(), error);
  }

  const auto command = build_encoder_command(config, pixel_format, width, height, fps);
  auto spawned = runner_.spawn_with_stdin(command);
  if (!spawned.has_value()) {
    return std::unexpected("failed to start the encoder: " + spawned.error());
  }

  pid_ = spawned->pid;
  stdin_fd_ = spawned->stdin_fd;
  output_path_ = config.output_path;
  return {};
}

std::expected<void, std::string> FrameEncoder::write_frame(const void* data, const std::size_t size) {
  if (stdin_fd_ < 0) {
    return std::unexpected("the encoder is not running");
  }
  return runner_.write_to_stdin(stdin_fd_, data, size);
}

std::expected<void, std::string> FrameEncoder::stop() {
  if (pid_ <= 0) {
    return {};
  }

  // Closing the input tells ffmpeg that the recording ended, so it can write
  // the container.
  if (stdin_fd_ >= 0) {
    static_cast<void>(runner_.close_stdin(stdin_fd_));
    stdin_fd_ = -1;
  }

  const auto exit_code = runner_.wait_for_process(pid_, kEncoderTimeoutMs);
  pid_ = -1;
  if (!exit_code.has_value()) {
    return std::unexpected("the encoder did not finish: " + exit_code.error());
  }
  if (*exit_code != 0) {
    return std::unexpected("the encoder failed with exit code " + std::to_string(*exit_code));
  }
  return {};
}

void FrameEncoder::pause() {
  if (pid_ > 0) {
    kill(pid_, SIGSTOP);
  }
}

void FrameEncoder::resume() {
  if (pid_ > 0) {
    kill(pid_, SIGCONT);
  }
}

bool FrameEncoder::is_running() const {
  return pid_ > 0;
}

std::filesystem::path FrameEncoder::output_path() const {
  return output_path_;
}

// The capture pipeline: a portal session, a PipeWire stream, and the encoder
// that the frames are written to.
#ifdef HYPRSHARE_HAS_PORTAL
struct ScreenRecorder::Pipeline {
  portal::PortalSession session;
  portal::PipeWireStream stream;
  FrameEncoder encoder;
  std::atomic<bool> stopped{false};
  std::mutex write_mutex;
};
#else
struct ScreenRecorder::Pipeline {
  FrameEncoder encoder;
  std::atomic<bool> stopped{false};
  std::mutex write_mutex;
};
#endif

ScreenRecorder::ScreenRecorder() {}

ScreenRecorder::~ScreenRecorder() {
  if (status_ == RecordingStatus::Recording) {
    static_cast<void>(stop());
  }
}

std::expected<void, std::string> ScreenRecorder::start(const RecordingConfig& config) {
  if (status_ == RecordingStatus::Recording) {
    return std::unexpected("already recording");
  }
  if (config.output_path.empty()) {
    return std::unexpected("the recording needs an output path");
  }

  auto pipeline = std::make_shared<Pipeline>();

#ifndef HYPRSHARE_HAS_PORTAL
  return std::unexpected(
    "screen recording needs the portal integration, which this build does not have");
#else
  auto session = pipeline->session.create_session();
  if (!session.has_value()) {
    return std::unexpected("failed to create the portal session: " + session.error());
  }
  auto sources = pipeline->session.select_sources({});
  if (!sources.has_value()) {
    return std::unexpected("failed to select the screen source: " + sources.error());
  }
  auto started = pipeline->session.start();
  if (!started.has_value()) {
    return std::unexpected("failed to start the portal session: " + started.error());
  }
  auto node_id = pipeline->session.get_pipewire_node_id();
  if (!node_id.has_value()) {
    return std::unexpected("the portal session did not provide a capture node: " + node_id.error());
  }

  auto connected = pipeline->stream.connect(*node_id);
  if (!connected.has_value()) {
    return std::unexpected("failed to connect to the capture source: " + connected.error());
  }
  auto streaming = pipeline->stream.start();
  if (!streaming.has_value()) {
    return std::unexpected("failed to start the capture stream: " + streaming.error());
  }

  // The source announces its format once the stream is running. The encoder
  // needs it before it can be started.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFormatTimeoutMs);
  std::optional<portal::VideoFormat> format;
  while (std::chrono::steady_clock::now() < deadline) {
    format = pipeline->stream.video_format();
    if (format.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!format.has_value()) {
    return std::unexpected("the capture source did not announce a video format");
  }

  const auto pixel_format = ffmpeg_pixel_format(format->format);
  if (!pixel_format.has_value()) {
    return std::unexpected("the capture format cannot be encoded");
  }

  RecordingConfig effective = config;
  if (effective.width == 0) effective.width = format->width;
  if (effective.height == 0) effective.height = format->height;
  if (effective.fps == 0) {
    effective.fps = format->framerate_num / format->framerate_denom;
  }
  if (effective.fps == 0) effective.fps = 30;

  auto encoder = pipeline->encoder.start(effective, *pixel_format,
    format->width, format->height, effective.fps);
  if (!encoder.has_value()) {
    return std::unexpected("failed to start the encoder: " + encoder.error());
  }

  config_ = effective;
  pipeline_ = pipeline;

  pipeline->stream.set_frame_callback([this, pipeline](const void* data, const uint32_t size,
                                                      const portal::FrameInfo& info) {
    // The rows of the captured frame are padded, so they are packed before
    // they go to the encoder.
    const auto packed = pack_video_frame(
      data, size, info.width, info.height, info.stride, info.format);
    if (!packed.has_value()) {
      return;
    }

    std::lock_guard<std::mutex> guard(pipeline->write_mutex);
    static_cast<void>(pipeline->encoder.write_frame(packed->data(), packed->size()));
    ++frames_written_;
  });

  start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  status_ = RecordingStatus::Recording;

  if (callback_) {
    callback_(RecordingStatus::Recording, "Recording started");
  }
  return {};
#endif
}

std::expected<void, std::string> ScreenRecorder::pause() {
  if (status_ != RecordingStatus::Recording) {
    return std::unexpected("not recording");
  }
  if (pipeline_) {
    pipeline_->encoder.pause();
  }
  status_ = RecordingStatus::Paused;
  if (callback_) {
    callback_(RecordingStatus::Paused, "Recording paused");
  }
  return {};
}

std::expected<void, std::string> ScreenRecorder::resume() {
  if (status_ != RecordingStatus::Paused) {
    return std::unexpected("not paused");
  }
  if (pipeline_) {
    pipeline_->encoder.resume();
  }
  status_ = RecordingStatus::Recording;
  if (callback_) {
    callback_(RecordingStatus::Recording, "Recording resumed");
  }
  return {};
}

std::expected<void, std::string> ScreenRecorder::stop() {
  if (status_ == RecordingStatus::Idle) {
    return std::unexpected("not recording");
  }

  std::expected<void, std::string> result{};

  if (pipeline_) {
    // The stream has to stop before the encoder is closed, so that no frame
    // is written to a pipe that is already gone.
#ifdef HYPRSHARE_HAS_PORTAL
    // The stream has to stop before the encoder is closed, so that no frame
    // is written to a pipe that is already gone.
    static_cast<void>(pipeline_->stream.stop());
#endif
    {
      std::lock_guard<std::mutex> guard(pipeline_->write_mutex);
      auto stopped = pipeline_->encoder.stop();
      if (!stopped.has_value()) {
        result = std::unexpected(stopped.error());
      }
    }
#ifdef HYPRSHARE_HAS_PORTAL
    pipeline_->stream.disconnect();
    pipeline_->session.close();
#endif
  }

  status_ = RecordingStatus::Stopped;
  if (callback_) {
    callback_(RecordingStatus::Stopped, "Recording stopped");
  }
  return result;
}

void ScreenRecorder::set_recording_callback(RecordingCallback callback) {
  callback_ = std::move(callback);
}

RecordingStatus ScreenRecorder::status() const {
  return status_;
}

std::filesystem::path ScreenRecorder::output_path() const {
  return config_.output_path;
}

uint64_t ScreenRecorder::duration_ms() const {
  if (start_time_ == 0) {
    return 0;
  }
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  return static_cast<uint64_t>(now) - start_time_;
}

uint64_t ScreenRecorder::frames_written() const {
  return frames_written_;
}

}  // namespace hyprshare::recording
