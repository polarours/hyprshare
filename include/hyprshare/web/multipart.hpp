#pragma once

#include <string>
#include <vector>

namespace hyprshare::web {

// One part of a multipart form.
struct MultipartFile {
  std::string name;
  std::string filename;
  std::string content;
};

// Reads the parts of a multipart body.
//
// The boundary is the one the Content-Type header carries, without the
// leading dashes.
std::vector<MultipartFile> parse_multipart(const std::string& body, const std::string& boundary);

// The boundary of a Content-Type header, or an empty string when the header
// does not name one.
std::string multipart_boundary(const std::string& content_type);

}  // namespace hyprshare::web
