#include "cmd_search.hpp"

#include <iostream>
#include <filesystem>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/search/search_engine.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_search(CommandContext& ctx, std::vector<std::string>& args) {
  // args[0] is the command name ("search"); the subcommand is args[1].
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare search <files> --path <dir> [--pattern <glob>] [--no-recursive] [--sort-by name|size|date] [--order asc|desc]"));
  }

  static hyprshare::search::SearchEngine engine;

  if (args[1] == "files") {
    hyprshare::search::SearchQuery query;
    query.search_path = std::filesystem::current_path();
    query.recursive = true;
    query.include_hidden = false;
    query.sort_by = hyprshare::search::SortBy::Name;
    query.sort_order = hyprshare::search::SortOrder::Ascending;

    for (std::size_t i = 2; i < args.size(); ++i) {
      if (args[i] == "--path" && i + 1 < args.size()) {
        query.search_path = args[++i];
      } else if (args[i] == "--pattern" && i + 1 < args.size()) {
        query.pattern = args[++i];
      } else if (args[i] == "--no-recursive") {
        query.recursive = false;
      } else if (args[i] == "--include-hidden") {
        query.include_hidden = true;
      } else if (args[i] == "--sort-by" && i + 1 < args.size()) {
        const std::string val = args[++i];
        if (val == "size") query.sort_by = hyprshare::search::SortBy::Size;
        else if (val == "date") query.sort_by = hyprshare::search::SortBy::Date;
        else if (val == "type") query.sort_by = hyprshare::search::SortBy::Type;
        else query.sort_by = hyprshare::search::SortBy::Name;
      } else if (args[i] == "--order" && i + 1 < args.size()) {
        query.sort_order = (args[++i] == "desc") ? hyprshare::search::SortOrder::Descending : hyprshare::search::SortOrder::Ascending;
      }
    }

    const auto result = engine.search(query);
    if (!result.has_value()) {
      return std::unexpected("search failed: " + result.error());
    }

    if (ctx.json) {
      std::cout << "{\"results\":[";
      for (std::size_t i = 0; i < result->size(); ++i) {
        if (i > 0) std::cout << ",";
        const auto& r = result->at(i);
        std::cout << "{\"path\":\"" << r.path.string()
                  << "\",\"name\":\"" << r.name
                  << "\",\"size\":" << r.size
                  << ",\"is_dir\":" << (r.is_directory ? "true" : "false")
                  << "}";
      }
      std::cout << "],\"count\":" << result->size() << "}\n";
    } else {
      std::cout << "Found " << result->size() << " results in " << query.search_path.string() << "\n\n";
      for (const auto& r : *result) {
        std::cout << (r.is_directory ? "DIR  " : "FILE ")
                  << r.name << "\t" << r.size << " bytes\t" << r.path.string() << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown search subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
