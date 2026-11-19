#include "cmd_config.hpp"

#include <iostream>
#include <fstream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/config.hpp"

namespace hyprshare::cli {

namespace {

std::string json_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped.push_back(c);
    }
  }
  return escaped;
}

hyprshare::core::Config load_for_edit(const hyprshare::platform::Paths& paths) {
  auto loaded = hyprshare::core::load_config(paths);
  return loaded.has_value() ? *loaded : hyprshare::core::Config{};
}

}  // namespace

std::expected<int, std::string> handle_config(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare config <show|set|get>"));
  }

  if (args[1] == "show") {
    if (ctx.json) {
      print_config_json(ctx.config, ctx.paths);
    } else {
      print_config_text(ctx.config, ctx.paths);
    }
    return 0;
  }

  if (args[1] == "set") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare config set <key> <value>"));
    }

    const auto key = args[2];
    const auto value = args[3];

    auto config = load_for_edit(ctx.paths);

    auto applied = hyprshare::core::set_config_value(config, key, value);
    if (!applied.has_value()) {
      return std::unexpected(applied.error());
    }

    auto saved = hyprshare::core::save_config(ctx.paths, config);
    if (!saved.has_value()) {
      return std::unexpected(saved.error());
    }

    if (ctx.json) {
      std::cout << "{\"key\":\"" << json_escape(key)
                << "\",\"value\":\"" << json_escape(value)
                << "\",\"path\":\"" << json_escape(ctx.paths.config_file.string()) << "\"}\n";
    } else {
      std::cout << "Set " << key << " = " << value << " (" << ctx.paths.config_file.string() << ")\n";
    }
    return 0;
  }

  if (args[1] == "get") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare config get <key>"));
    }

    const auto key = args[2];

    auto config = load_for_edit(ctx.paths);
    auto value = hyprshare::core::get_config_value(config, key);
    if (!value.has_value()) {
      return std::unexpected(value.error());
    }

    if (ctx.json) {
      std::cout << "{\"key\":\"" << json_escape(key)
                << "\",\"value\":\"" << json_escape(*value) << "\"}\n";
    } else {
      std::cout << *value << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown config subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
