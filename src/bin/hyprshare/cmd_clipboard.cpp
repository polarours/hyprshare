#include "cmd_clipboard.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/clipboard_share.hpp"
#include "hyprshare/core/file_share.hpp"

namespace hyprshare::cli {

using hyprshare::model::ClipboardText;

std::expected<int, std::string> handle_clipboard(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown clipboard subcommand"));
  }

  if (args[1] == "read") {
    const auto clipboard = hyprshare::core::read_clipboard_text(ctx.paths, ctx.runner);
    if (!clipboard.has_value()) {
      std::cerr << "error: " << clipboard.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_clipboard_text_json(*clipboard);
    } else {
      std::cout << clipboard->text;
    }
    return 0;
  }

  if (args[1] == "receive" && args.size() >= 3) {
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    if (!staged->checksum_path.empty()) {
      const auto verification = hyprshare::core::verify_staged_file_share(*staged, ctx.runner);
      if (!verification.has_value()) {
        std::cerr << "error: " << verification.error() << "\n";
        return 1;
      }
    }

    const auto clipboard = hyprshare::core::clipboard_from_staged_share(*staged);
    if (!clipboard.has_value()) {
      std::cerr << "error: " << clipboard.error() << "\n";
      return 1;
    }

    const auto written = hyprshare::core::write_clipboard_text(clipboard->text, ctx.paths, ctx.runner);
    if (!written.has_value()) {
      std::cerr << "error: " << written.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_clipboard_text_json(*written);
    } else {
      print_clipboard_write_text(*written);
    }
    return 0;
  }

  if (args[1] == "write") {
    const auto text = take_option(args, "--text");
    const auto from_file = take_option(args, "--from-file");
    if (text.has_value() == from_file.has_value()) {
      std::cerr << "error: clipboard write requires exactly one of --text or --from-file\n";
      return 1;
    }

    std::expected<ClipboardText, std::string> clipboard = std::unexpected("clipboard input was not loaded");
    if (text.has_value()) {
      clipboard = hyprshare::core::write_clipboard_text(*text, ctx.paths, ctx.runner);
    } else {
      const auto loaded = hyprshare::core::load_clipboard_text_file(*from_file);
      if (!loaded.has_value()) {
        std::cerr << "error: " << loaded.error() << "\n";
        return 1;
      }
      clipboard = hyprshare::core::write_clipboard_text(loaded->text, ctx.paths, ctx.runner);
    }

    if (!clipboard.has_value()) {
      std::cerr << "error: " << clipboard.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_clipboard_text_json(*clipboard);
    } else {
      print_clipboard_write_text(*clipboard);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown clipboard subcommand"));
}

}  // namespace hyprshare::cli
