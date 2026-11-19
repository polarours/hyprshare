#include "hyprshare/web/multipart.hpp"

#include <algorithm>
#include <cctype>

namespace hyprshare::web {

namespace {

std::string trim(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

// Reads a quoted or bare attribute value out of a header line.
std::string header_attribute(const std::string& line, const std::string& attribute) {
  const auto position = line.find(attribute + "=");
  if (position == std::string::npos) {
    return "";
  }

  std::size_t start = position + attribute.size() + 1;
  if (start < line.size() && line[start] == '"') {
    ++start;
    const auto end = line.find('"', start);
    if (end == std::string::npos) {
      return "";
    }
    return line.substr(start, end - start);
  }

  const auto end = line.find(';', start);
  return trim(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
}

}  // namespace

std::string multipart_boundary(const std::string& content_type) {
  const auto position = content_type.find("boundary=");
  if (position == std::string::npos) {
    return "";
  }

  std::string boundary = trim(content_type.substr(position + 9));
  if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') {
    boundary = boundary.substr(1, boundary.size() - 2);
  }
  return boundary;
}

std::vector<MultipartFile> parse_multipart(const std::string& body, const std::string& boundary) {
  std::vector<MultipartFile> files;
  if (boundary.empty()) {
    return files;
  }

  const std::string delimiter = "--" + boundary;
  std::size_t position = body.find(delimiter);
  if (position == std::string::npos) {
    return files;
  }

  while (position != std::string::npos) {
    const auto part_start = position + delimiter.size();
    // The closing delimiter is followed by two dashes.
    if (body.compare(part_start, 2, "--") == 0) {
      break;
    }

    // The part starts after the line break that follows the delimiter.
    std::size_t content_start = part_start;
    if (body.compare(content_start, 2, "\r\n") == 0) {
      content_start += 2;
    } else if (content_start < body.size() && body[content_start] == '\n') {
      ++content_start;
    }

    const auto next = body.find(delimiter, content_start);
    if (next == std::string::npos) {
      break;
    }

    // The part ends before the line break that precedes the next delimiter.
    std::size_t content_end = next;
    if (content_end >= 2 && body.compare(content_end - 2, 2, "\r\n") == 0) {
      content_end -= 2;
    } else if (content_end > 0 && body[content_end - 1] == '\n') {
      --content_end;
    }

    const std::string part = body.substr(content_start, content_end - content_start);
    const auto header_end = part.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      const std::string headers = part.substr(0, header_end);
      const std::string content = part.substr(header_end + 4);

      MultipartFile file;
      const auto disposition = headers.find("Content-Disposition:");
      if (disposition != std::string::npos) {
        const auto line_end = headers.find("\r\n", disposition);
        const std::string line = headers.substr(disposition, line_end == std::string::npos
          ? std::string::npos
          : line_end - disposition);
        file.name = header_attribute(line, "name");
        file.filename = header_attribute(line, "filename");
      }
      file.content = content;
      files.push_back(file);
    }

    position = next;
  }

  return files;
}

}  // namespace hyprshare::web
