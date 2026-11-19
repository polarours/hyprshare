#include "hyprshare/core/clipboard_share.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "hyprshare/core/file_share.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::expected<void, std::string> ensure_directory(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    return std::unexpected("failed to create directory `" + path.string() + "`: " + error.message());
  }
  return {};
}

std::string make_clipboard_temp_name() {
  static std::size_t counter = 0;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  ++counter;
  return "clipboard-" + std::to_string(millis) + "-" + std::to_string(counter) + ".txt";
}

std::expected<std::filesystem::path, std::string> write_text_file(
  const std::string_view text,
  const std::filesystem::path& path) {
  auto directory = ensure_directory(path.parent_path());
  if (!directory.has_value()) {
    return std::unexpected(directory.error());
  }

  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    return std::unexpected("failed to open clipboard text file for writing: " + path.string());
  }
  output << text;
  return path;
}

std::expected<std::string, std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open clipboard text file for reading: " + path.string());
  }

  std::ostringstream stream;
  stream << input.rdbuf();
  return stream.str();
}

std::filesystem::path temp_clipboard_path(const platform::Paths& paths) {
  return paths.runtime_dir / "clipboard" / make_clipboard_temp_name();
}

std::expected<std::pair<std::string, std::string>, std::string> read_backend_command(
  const std::filesystem::path& output_path,
  const platform::ProcessRunner& runner) {
  const std::string quoted_output = util::shell_quote(output_path.string());
  if (runner.command_exists("wl-paste")) {
    return std::pair<std::string, std::string>{
      "wl-clipboard",
      "wl-paste --type text/plain > " + quoted_output,
    };
  }
  if (runner.command_exists("xclip")) {
    return std::pair<std::string, std::string>{
      "xclip",
      "xclip -selection clipboard -out > " + quoted_output,
    };
  }
  if (runner.command_exists("xsel")) {
    return std::pair<std::string, std::string>{
      "xsel",
      "xsel --clipboard --output > " + quoted_output,
    };
  }
  return std::unexpected("no supported text clipboard reader found; install wl-clipboard, xclip, or xsel");
}

std::expected<std::pair<std::string, std::string>, std::string> write_backend_command(
  const std::filesystem::path& input_path,
  const platform::ProcessRunner& runner) {
  const std::string quoted_input = util::shell_quote(input_path.string());
  if (runner.command_exists("wl-copy")) {
    return std::pair<std::string, std::string>{
      "wl-clipboard",
      "wl-copy --type text/plain < " + quoted_input,
    };
  }
  if (runner.command_exists("xclip")) {
    return std::pair<std::string, std::string>{
      "xclip",
      "xclip -selection clipboard -in < " + quoted_input,
    };
  }
  if (runner.command_exists("xsel")) {
    return std::pair<std::string, std::string>{
      "xsel",
      "xsel --clipboard --input < " + quoted_input,
    };
  }
  return std::unexpected("no supported text clipboard writer found; install wl-clipboard, xclip, or xsel");
}

model::ClipboardText make_clipboard_text(
  std::string text,
  std::string backend,
  const std::int64_t captured_at_unix = unix_time_now()) {
  model::ClipboardText clipboard;
  clipboard.text = std::move(text);
  clipboard.backend = std::move(backend);
  clipboard.byte_count = clipboard.text.size();
  clipboard.captured_at_unix = captured_at_unix;
  return clipboard;
}

}  // namespace

std::expected<model::ClipboardText, std::string> read_clipboard_text(
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) {
  const auto temp_path = temp_clipboard_path(paths);
  auto directory = ensure_directory(temp_path.parent_path());
  if (!directory.has_value()) {
    return std::unexpected(directory.error());
  }

  const auto command = read_backend_command(temp_path, runner);
  if (!command.has_value()) {
    return std::unexpected(command.error());
  }

  const auto result = runner.run_shell(command->second);
  if (result.exit_code != 0) {
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
    return std::unexpected("failed to read text clipboard through " + command->first + ": " + result.output);
  }

  const auto text = read_text_file(temp_path);
  std::error_code remove_error;
  std::filesystem::remove(temp_path, remove_error);
  if (!text.has_value()) {
    return std::unexpected(text.error());
  }
  return make_clipboard_text(*text, command->first);
}

std::expected<model::ClipboardText, std::string> write_clipboard_text(
  const std::string_view text,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) {
  const auto temp_path = temp_clipboard_path(paths);
  const auto written = write_text_file(text, temp_path);
  if (!written.has_value()) {
    return std::unexpected(written.error());
  }

  const auto command = write_backend_command(temp_path, runner);
  if (!command.has_value()) {
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
    return std::unexpected(command.error());
  }

  const auto result = runner.run_shell(command->second);
  std::error_code remove_error;
  std::filesystem::remove(temp_path, remove_error);
  if (result.exit_code != 0) {
    return std::unexpected("failed to write text clipboard through " + command->first + ": " + result.output);
  }

  return make_clipboard_text(std::string(text), command->first);
}

std::expected<model::ClipboardText, std::string> load_clipboard_text_file(
  const std::filesystem::path& path) {
  if (path.empty()) {
    return std::unexpected("clipboard text file path must not be empty");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return std::unexpected("failed to resolve clipboard text file `" + path.string() + "`: " + error.message());
  }
  if (!std::filesystem::exists(absolute)) {
    return std::unexpected("clipboard text file does not exist: " + absolute.string());
  }
  if (!std::filesystem::is_regular_file(absolute)) {
    return std::unexpected("clipboard text path is not a regular file: " + absolute.string());
  }

  const auto text = read_text_file(absolute);
  if (!text.has_value()) {
    return std::unexpected(text.error());
  }
  return make_clipboard_text(*text, "file");
}

std::expected<model::ClipboardText, std::string> clipboard_from_staged_share(
  const model::StagedFileShare& staged) {
  if (staged.manifest.capability != model::CapabilityKind::Clipboard) {
    return std::unexpected("staged session is not a clipboard share: " + staged.session_id);
  }

  const auto clipboard_path = std::filesystem::path(staged.payload_path) / "clipboard.txt";
  if (!std::filesystem::exists(clipboard_path)) {
    return std::unexpected("staged clipboard payload is missing clipboard.txt: " + clipboard_path.string());
  }

  const auto text = read_text_file(clipboard_path);
  if (!text.has_value()) {
    return std::unexpected(text.error());
  }
  return make_clipboard_text(*text, "staged-session");
}

std::expected<model::StagedFileShare, std::string> stage_clipboard_text(
  const model::ClipboardText& clipboard,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) {
  const auto cache_root = paths.cache_dir / "clipboard";
  const auto clipboard_path = cache_root / make_clipboard_temp_name();
  const auto written = write_text_file(clipboard.text, clipboard_path);
  if (!written.has_value()) {
    return std::unexpected(written.error());
  }

  const auto cleanup = [&clipboard_path]() {
    std::error_code remove_error;
    std::filesystem::remove(clipboard_path, remove_error);
  };

  auto manifest = build_file_manifest(clipboard_path);
  if (!manifest.has_value()) {
    cleanup();
    return std::unexpected(manifest.error());
  }
  manifest->capability = model::CapabilityKind::Clipboard;
  manifest->share_name = "clipboard.txt";
  manifest->root_path = "clipboard";
  if (!manifest->entries.empty()) {
    manifest->entries.front().relative_path = "clipboard.txt";
  }
  manifest->notes.push_back("Text clipboard snapshot.");
  manifest->notes.push_back("Source backend: " + clipboard.backend + ".");

  auto staged = stage_file_share(*manifest, paths, runner);
  cleanup();
  if (!staged.has_value()) {
    return std::unexpected(staged.error());
  }
  return staged;
}

}  // namespace hyprshare::core
