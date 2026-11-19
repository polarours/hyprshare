#pragma once

#include <expected>
#include <filesystem>
#include <string_view>

#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::core {

std::expected<model::ClipboardText, std::string> read_clipboard_text(
  const platform::Paths& paths,
  const platform::ProcessRunner& runner);
std::expected<model::ClipboardText, std::string> write_clipboard_text(
  std::string_view text,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner);
std::expected<model::ClipboardText, std::string> load_clipboard_text_file(
  const std::filesystem::path& path);
std::expected<model::ClipboardText, std::string> clipboard_from_staged_share(
  const model::StagedFileShare& staged);
std::expected<model::StagedFileShare, std::string> stage_clipboard_text(
  const model::ClipboardText& clipboard,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner);

}  // namespace hyprshare::core
