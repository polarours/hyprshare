#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace hyprshare::util {

inline std::string trim(std::string_view value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return std::string(value.substr(start, end - start));
}

inline std::string to_lower(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lower;
}

inline bool starts_with(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

inline bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

inline std::vector<std::string> split(std::string_view value, char delimiter) {
  std::vector<std::string> out;
  std::string current;
  for (const char ch : value) {
    if (ch == delimiter) {
      out.push_back(trim(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  out.push_back(trim(current));
  return out;
}

inline std::string join(const std::vector<std::string>& parts, std::string_view delimiter) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index > 0) {
      stream << delimiter;
    }
    stream << parts[index];
  }
  return stream.str();
}

inline std::string strip_quotes(std::string_view value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
    return std::string(value.substr(1, value.size() - 2));
  }
  return std::string(value);
}

inline std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

inline std::string shell_quote(std::string_view value) {
  std::string quoted("'");
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
      continue;
    }
    quoted.push_back(ch);
  }
  quoted.push_back('\'');
  return quoted;
}

}  // namespace hyprshare::util

