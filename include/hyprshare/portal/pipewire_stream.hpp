#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hyprshare::portal {

struct FrameInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  // The number of bytes between the start of one row and the next. It is
  // usually larger than the width of a row, because the rows are padded.
  int32_t stride = 0;
  uint64_t pts = 0;
};

// The video format that was negotiated with the capture source.
struct VideoFormat {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  int32_t stride = 0;
  uint32_t framerate_num = 0;
  uint32_t framerate_denom = 1;
  // The size of one frame in bytes.
  uint64_t frame_size = 0;
};

using FrameCallback = std::function<void(const void* data, uint32_t size, const FrameInfo& info)>;

class PipeWireStream {
 public:
  PipeWireStream();
  ~PipeWireStream();

  PipeWireStream(const PipeWireStream&) = delete;
  PipeWireStream& operator=(const PipeWireStream&) = delete;

  std::expected<void, std::string> connect(uint32_t node_id);
  std::expected<void, std::string> start();
  std::expected<void, std::string> stop();

  void set_frame_callback(FrameCallback callback);
  bool is_connected() const;

  // The negotiated video format. It is only available once the source has
  // been connected and the format has been agreed on.
  std::optional<VideoFormat> video_format() const;

  void disconnect();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hyprshare::portal
