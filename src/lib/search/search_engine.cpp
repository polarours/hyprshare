#include "hyprshare/search/search_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>

namespace hyprshare::search {

namespace {

// POSIX-style wildcard match: '*' matches any sequence, '?' matches one char.
// Anything else matches literally.
bool glob_match(const std::string& pattern, const std::string& text) {
  const char* p = pattern.c_str();
  const char* t = text.c_str();
  const char* star = nullptr;
  const char* match = nullptr;

  while (*t) {
    if (*p == '*') {
      star = p++;
      match = t;
    } else if (*p == '?' || (*p != '\0' && *p == *t)) {
      ++p;
      ++t;
    } else if (star) {
      p = star + 1;
      ++match;
      t = match;
    } else {
      return false;
    }
  }
  while (*p == '*') {
    ++p;
  }
  return *p == '\0';
}

}  // namespace

SearchEngine::SearchEngine() {}

SearchEngine::~SearchEngine() {}

std::expected<std::vector<SearchResult>, std::string> SearchEngine::search(const SearchQuery& query) {
  std::vector<SearchResult> results;

  if (!std::filesystem::exists(query.search_path)) {
    return std::unexpected("search path not found: " + query.search_path.string());
  }

  auto options = std::filesystem::directory_options::skip_permission_denied;

  const auto consider_entry = [&query, this, &results](const std::filesystem::directory_entry& entry) {
    if (results.size() >= max_results_) {
      return;
    }

    const auto name = entry.path().filename().string();
    if (!query.include_hidden && !name.empty() && name[0] == '.') {
      return;
    }

    SearchResult result;
    result.path = entry.path();
    result.name = name;
    result.is_directory = entry.is_directory();

    if (entry.is_regular_file()) {
      result.size = entry.file_size();
    }

    std::error_code error;
    result.modified = std::filesystem::last_write_time(entry.path(), error).time_since_epoch().count();

    if (!query.pattern.empty() && !glob_match(query.pattern, result.name)) {
      return;
    }

    if (query.min_size > 0 && result.size < query.min_size) {
      return;
    }

    if (query.max_size > 0 && result.size > query.max_size) {
      return;
    }

    results.push_back(result);
  };

  if (query.recursive) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(query.search_path, options)) {
      consider_entry(entry);
      if (results.size() >= max_results_) {
        break;
      }
    }
  } else {
    // Non-recursive: only the direct children of the search path.
    for (const auto& entry : std::filesystem::directory_iterator(query.search_path, options)) {
      consider_entry(entry);
    }
  }

  std::sort(results.begin(), results.end(), [&query](const SearchResult& a, const SearchResult& b) {
    switch (query.sort_by) {
      case SortBy::Name:
        return query.sort_order == SortOrder::Ascending ? a.name < b.name : a.name > b.name;
      case SortBy::Size:
        return query.sort_order == SortOrder::Ascending ? a.size < b.size : a.size > b.size;
      case SortBy::Date:
        return query.sort_order == SortOrder::Ascending ? a.modified < b.modified : a.modified > b.modified;
      case SortBy::Type:
        return query.sort_order == SortOrder::Ascending ? a.is_directory < b.is_directory : a.is_directory > b.is_directory;
    }
    return false;
  });

  return results;
}

std::expected<SearchResult, std::string> SearchEngine::find_file(const std::string& name, const std::filesystem::path& search_path) {
  SearchQuery query;
  query.pattern = name;
  query.search_path = search_path;
  query.recursive = true;

  auto results = search(query);
  if (!results.has_value()) {
    return std::unexpected(results.error());
  }

  if (results->empty()) {
    return std::unexpected("file not found: " + name);
  }

  return results->front();
}

std::expected<std::vector<SearchResult>, std::string> SearchEngine::find_by_pattern(
  const std::string& pattern,
  const std::filesystem::path& search_path) {
  SearchQuery query;
  query.pattern = pattern;
  query.search_path = search_path;
  query.recursive = true;

  return search(query);
}

void SearchEngine::set_max_results(const std::size_t max_results) {
  max_results_ = max_results;
}

std::size_t SearchEngine::max_results() const {
  return max_results_;
}

}  // namespace hyprshare::search
