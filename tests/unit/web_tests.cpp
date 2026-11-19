#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "hyprshare/web/multipart.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  // The boundary of a content type header.
  expect(hyprshare::web::multipart_boundary("multipart/form-data; boundary=abc123") == "abc123",
    "the boundary should be read from the content type");
  expect(hyprshare::web::multipart_boundary("multipart/form-data; boundary=\"quoted\"") == "quoted",
    "a quoted boundary should be unwrapped");
  expect(hyprshare::web::multipart_boundary("application/json").empty(),
    "a content type without a boundary should have none");

  // A body with two files.
  const std::string boundary = "----hyprshare";
  const std::string body =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"files\"; filename=\"first.txt\"\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "first content\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"files\"; filename=\"second.txt\"\r\n"
    "\r\n"
    "second content\r\n"
    "--" + boundary + "--\r\n";

  const auto files = hyprshare::web::parse_multipart(body, boundary);
  expect(files.size() == 2, "both files should be read");
  if (files.size() == 2) {
    expect(files[0].name == "files", "the field name should be read");
    expect(files[0].filename == "first.txt", "the file name should be read");
    expect(files[0].content == "first content", "the content should be read");
    expect(files[1].filename == "second.txt", "the second file name should be read");
    expect(files[1].content == "second content", "the second content should be read");
  }

  // A body without a boundary yields nothing.
  expect(hyprshare::web::parse_multipart(body, "").empty(),
    "a body without a boundary should yield no files");
  expect(hyprshare::web::parse_multipart("not a multipart body", "xyz").empty(),
    "a body that is not multipart should yield no files");

  return test_support::report("web");
}
