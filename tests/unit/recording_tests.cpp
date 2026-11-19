#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <spa/param/video/raw.h>

#include "hyprshare/recording/screen_recorder.hpp"

#include <spa/param/video/raw.h>

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_recording_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  // The rows of a captured frame are padded, and the encoder expects them to
  // follow each other closely.
  {
    using hyprshare::recording::pack_video_frame;

    // A packed frame whose rows are padded.
    {
      const std::uint32_t width = 4;
      const std::uint32_t height = 2;
      const std::int32_t stride = 20;  // 4 pixels of 4 bytes, padded to 20
      std::vector<char> frame(static_cast<std::size_t>(stride) * height, 0);
      for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
          const auto offset = static_cast<std::size_t>(row) * stride + column * 4;
          frame[offset + 0] = static_cast<char>(row);
          frame[offset + 1] = static_cast<char>(column);
        }
      }

      const auto packed = pack_video_frame(
        frame.data(), frame.size(), width, height, stride, SPA_VIDEO_FORMAT_BGRx);
      expect(packed.has_value(), "a padded frame should be packed");
      if (packed.has_value()) {
        expect(packed->size() == width * height * 4, "the packed frame should hold the pixels only");
        for (std::uint32_t row = 0; row < height; ++row) {
          for (std::uint32_t column = 0; column < width; ++column) {
            const auto offset = (row * width + column) * 4;
            expect((*packed)[offset + 0] == static_cast<char>(row),
              "the rows should keep their content");
            expect((*packed)[offset + 1] == static_cast<char>(column),
              "the pixels should keep their content");
          }
        }
      }
    }

    // A frame whose rows already follow each other closely.
    {
      const std::uint32_t width = 2;
      const std::uint32_t height = 2;
      const std::int32_t stride = 8;
      std::vector<char> frame(static_cast<std::size_t>(stride) * height, 7);
      const auto packed = pack_video_frame(
        frame.data(), frame.size(), width, height, stride, SPA_VIDEO_FORMAT_BGRx);
      expect(packed.has_value(), "a frame without padding should be packed");
      if (packed.has_value()) {
        expect(packed->size() == width * height * 4, "the packed frame should hold the pixels only");
        expect((*packed)[0] == 7, "the content should be kept");
      }
    }

    // A planar frame: the chroma plane follows the luma plane.
    {
      const std::uint32_t width = 4;
      const std::uint32_t height = 4;
      const std::int32_t stride = 8;  // padded luma rows
      const std::size_t luma_size = static_cast<std::size_t>(stride) * height;
      const std::size_t chroma_size = (static_cast<std::size_t>(stride) / 2) * (height / 2);
      std::vector<char> frame(luma_size + chroma_size, 3);

      const auto packed = pack_video_frame(
        frame.data(), frame.size(), width, height, stride, SPA_VIDEO_FORMAT_NV12);
      expect(packed.has_value(), "a planar frame should be packed");
      if (packed.has_value()) {
        expect(packed->size() == width * height + (width / 2) * (height / 2) * 2,
          "the packed frame should hold the two planes closely together");
      }
    }

    // A format that cannot be packed is reported.
    {
      const std::vector<char> frame(64, 0);
      const auto packed = pack_video_frame(
        frame.data(), frame.size(), 4, 4, 16, SPA_VIDEO_FORMAT_ENCODED);
      expect(!packed.has_value(), "a format that cannot be packed should be reported");
    }
  }

  // The container extension
  expect(hyprshare::recording::recording_extension(hyprshare::recording::RecordingFormat::MP4) == ".mp4",
    "MP4 should use the .mp4 extension");
  expect(hyprshare::recording::recording_extension(hyprshare::recording::RecordingFormat::WebM) == ".webm",
    "WebM should use the .webm extension");
  expect(hyprshare::recording::recording_extension(hyprshare::recording::RecordingFormat::MKV) == ".mkv",
    "MKV should use the .mkv extension");

  // The pixel format mapping
  expect(hyprshare::recording::ffmpeg_pixel_format(SPA_VIDEO_FORMAT_BGRx).value_or("") == "bgra",
    "BGRx should map to bgra");
  expect(hyprshare::recording::ffmpeg_pixel_format(SPA_VIDEO_FORMAT_RGBx).value_or("") == "rgba",
    "RGBx should map to rgba");
  expect(hyprshare::recording::ffmpeg_pixel_format(SPA_VIDEO_FORMAT_NV12).value_or("") == "nv12",
    "NV12 should map to nv12");
  expect(!hyprshare::recording::ffmpeg_pixel_format(SPA_VIDEO_FORMAT_ENCODED).has_value(),
    "an encoded format should not be mapped to a raw pixel format");

  // The encoder command
  {
    hyprshare::recording::RecordingConfig config;
    config.format = hyprshare::recording::RecordingFormat::MP4;
    config.output_path = "/tmp/out.mp4";

    const auto command = hyprshare::recording::build_encoder_command(config, "bgra", 1920, 1080, 30);
    expect(command.find("ffmpeg") == 0, "the command should start with ffmpeg");
    expect(command.find("-f rawvideo") != std::string::npos, "the input should be raw video");
    expect(command.find("-pixel_format bgra") != std::string::npos, "the pixel format should be set");
    expect(command.find("-video_size 1920x1080") != std::string::npos, "the size should be set");
    expect(command.find("-framerate 30") != std::string::npos, "the frame rate should be set");
    expect(command.find("libx264") != std::string::npos, "MP4 should be encoded with libx264");
    expect(command.find("/tmp/out.mp4") != std::string::npos, "the output path should be set");

    config.format = hyprshare::recording::RecordingFormat::WebM;
    const auto webm = hyprshare::recording::build_encoder_command(config, "bgra", 640, 480, 25);
    expect(webm.find("libvpx") != std::string::npos, "WebM should be encoded with libvpx");
  }

  // The encoder itself: synthetic frames have to end up in a real video file.
  {
    const auto output = temp_root / "capture.mp4";
    const std::uint32_t width = 64;
    const std::uint32_t height = 48;
    const std::uint32_t frames = 12;

    hyprshare::recording::RecordingConfig config;
    config.format = hyprshare::recording::RecordingFormat::MP4;
    config.output_path = output;
    config.fps = 10;

    hyprshare::recording::FrameEncoder encoder;
    const auto started = encoder.start(config, "bgra", width, height, 10);
    expect(started.has_value(),
      "the encoder should start: " + (started.has_value() ? "" : started.error()));

    if (started.has_value()) {
      // A gradient, so that the encoder has something to work with.
      std::vector<unsigned char> frame(static_cast<std::size_t>(width) * height * 4, 0);
      for (std::uint32_t index = 0; index < frames; ++index) {
        for (std::size_t pixel = 0; pixel < frame.size(); pixel += 4) {
          frame[pixel + 0] = static_cast<unsigned char>(index * 20);
          frame[pixel + 1] = static_cast<unsigned char>(pixel % 256);
          frame[pixel + 2] = static_cast<unsigned char>(128);
          frame[pixel + 3] = 255;
        }
        const auto written = encoder.write_frame(frame.data(), frame.size());
        expect(written.has_value(), "a frame should be written to the encoder");
      }

      const auto stopped = encoder.stop();
      expect(stopped.has_value(),
        "the encoder should stop cleanly: " + (stopped.has_value() ? "" : stopped.error()));
    }

    expect(std::filesystem::exists(output), "the recording file should exist");
    if (std::filesystem::exists(output)) {
      expect(std::filesystem::file_size(output) > 0, "the recording file should not be empty");
    }

    // The file has to be a real video, not just bytes.
    const std::string probe =
      "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,codec_name "
      "-of csv=p=0 " + output.string() + " 2>/dev/null";
    FILE* pipe = popen(probe.c_str(), "r");
    expect(pipe != nullptr, "ffprobe should be available to check the recording");
    if (pipe != nullptr) {
      char buffer[256] = {0};
      std::string details;
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        details += buffer;
      }
      pclose(pipe);
      expect(details.find("64") != std::string::npos,
        "the recording should have the expected width: " + details);
      expect(details.find("48") != std::string::npos,
        "the recording should have the expected height: " + details);
      expect(details.find("h264") != std::string::npos,
        "the recording should be encoded with h264: " + details);
    }
  }

  std::filesystem::remove_all(temp_root);
  return test_support::report("recording");
}
