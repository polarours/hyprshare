#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::search {

enum class SortBy {
  Name,
  Size,
  Date,
  Type
};

enum class SortOrder {
  Ascending,
  Descending
};

struct SearchQuery {
  std::string pattern;
  std::filesystem::path search_path;
  bool recursive = true;
  bool include_hidden = false;
  SortBy sort_by = SortBy::Name;
  SortOrder sort_order = SortOrder::Ascending;
  std::uint64_t min_size = 0;
  std::uint64_t max_size = 0;
  std::string file_type;
};

struct SearchResult {
  std::filesystem::path path;
  std::string name;
  std::uint64_t size = 0;
  std::int64_t modified = 0;
  bool is_directory = false;
};

class SearchEngine {
 public:
  SearchEngine();
  ~SearchEngine();

  std::expected<std::vector<SearchResult>, std::string> search(const SearchQuery& query);
  std::expected<SearchResult, std::string> find_file(const std::string& name, const std::filesystem::path& search_path);
  std::expected<std::vector<SearchResult>, std::string> find_by_pattern(const std::string& pattern, const std::filesystem::path& search_path);

  void set_max_results(std::size_t max_results);
  std::size_t max_results() const;

 private:
  std::size_t max_results_ = 1000;
};

}  // namespace hyprshare::search
