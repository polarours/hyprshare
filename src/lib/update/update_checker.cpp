#include "hyprshare/update/update_checker.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <chrono>

#include "hyprshare/http/http_client.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::update {

namespace {

std::filesystem::path state_dir() {
  return std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare";
}

// The release manifest is a small key/value document:
//
//   version = "0.7.0"
//   download_url = "/path/to/hyprshare-0.7.0.tar.gz"
//   release_notes = "What changed"
//
// A manifest that is served over HTTP is usually JSON, so both shapes are
// understood.
std::string manifest_value(const std::string& line, const std::string& key) {
  const std::size_t equals = line.find('=');
  if (equals != std::string::npos && util::trim(line.substr(0, equals)) == key) {
    return util::strip_quotes(util::trim(line.substr(equals + 1)));
  }

  const std::string quoted_key = "\"" + key + "\"";
  const auto key_position = line.find(quoted_key);
  if (key_position == std::string::npos) {
    return "";
  }
  const auto colon = line.find(':', key_position + quoted_key.size());
  if (colon == std::string::npos) {
    return "";
  }
  const auto value_start = line.find('"', colon + 1);
  if (value_start == std::string::npos) {
    return "";
  }
  const auto value_end = line.find('"', value_start + 1);
  if (value_end == std::string::npos) {
    return "";
  }
  return line.substr(value_start + 1, value_end - value_start - 1);
}

// Extracts a field from a manifest, whether it is key/value or JSON.
std::string manifest_field(const std::string& content, const std::string& key) {
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    const auto value = manifest_value(line, key);
    if (!value.empty()) {
      return value;
    }
  }
  return "";
}

int compare_versions(const std::string& a, const std::string& b) {
  std::istringstream sa(a), sb(b);
  std::string pa, pb;
  while (std::getline(sa, pa, '.') && std::getline(sb, pb, '.')) {
    const int va = pa.empty() ? 0 : std::stoi(pa);
    const int vb = pb.empty() ? 0 : std::stoi(pb);
    if (va != vb) return va - vb;
  }
  return 0;
}

}  // namespace

UpdateChecker::UpdateChecker() {}

UpdateChecker::~UpdateChecker() {}

std::expected<UpdateInfo, std::string> UpdateChecker::check_for_updates() {
  UpdateInfo info;
  info.current_version = current_version_;
  info.latest_version = current_version_;

  std::string content;
  if (!release_url_.empty()) {
    http::RequestOptions options;
    http::Client client;
    const auto response = client.get(release_url_, options);
    if (!response.has_value()) {
      return std::unexpected("failed to fetch the release manifest: " + response.error());
    }
    if (response->status < 200 || response->status >= 300) {
      return std::unexpected("the release server answered with status " +
        std::to_string(response->status));
    }
    content = response->body;
  } else {
    const auto manifest = release_manifest();
    std::error_code error;
    if (!std::filesystem::exists(manifest, error)) {
      // No release information is reachable. Report that instead of claiming
      // that the installation is up to date.
      return info;
    }

    std::ifstream input(manifest);
    if (!input.is_open()) {
      return std::unexpected("failed to read release manifest: " + manifest.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
  }

  info.latest_version = manifest_field(content, "version");
  info.download_url = manifest_field(content, "download_url");
  info.release_notes = manifest_field(content, "release_notes");

  if (info.latest_version.empty()) {
    // The manifest did not say which version it describes.
    return info;
  }

  info.release_info_available = true;
  info.update_available = (compare_versions(info.latest_version, info.current_version) > 0);
  return info;
}

std::expected<void, std::string> UpdateChecker::download_update(const std::string& url) {
  if (url.empty()) {
    return std::unexpected("no download url available");
  }

  std::error_code error;
  std::filesystem::create_directories(download_dir(), error);
  if (error) {
    return std::unexpected("failed to create download directory: " + error.message());
  }

  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
    // The artifact is fetched over the network.
    const auto parsed = http::parse_url(url);
    if (!parsed.has_value()) {
      return std::unexpected(parsed.error());
    }

    std::string name = parsed->path;
    const auto separator = name.find_last_of('/');
    if (separator != std::string::npos) {
      name = name.substr(separator + 1);
    }
    if (name.empty()) {
      name = "release";
    }

    http::Client client;
    const auto response = client.get(url);
    if (!response.has_value()) {
      return std::unexpected("failed to download the release: " + response.error());
    }
    if (response->status < 200 || response->status >= 300) {
      return std::unexpected("the server answered with status " +
        std::to_string(response->status));
    }

    const auto destination = download_dir() / name;
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return std::unexpected("failed to open the download file: " + destination.string());
    }
    output << response->body;
    if (output.fail()) {
      return std::unexpected("failed to write the download file: " + destination.string());
    }
    return {};
  }

  // The artifact is a file on this machine.
  const std::filesystem::path source(url);
  if (!std::filesystem::exists(source, error)) {
    return std::unexpected("release artifact not found: " + url);
  }

  const auto destination = download_dir() / source.filename();
  std::filesystem::copy_file(source, destination,
    std::filesystem::copy_options::overwrite_existing, error);
  if (error) {
    return std::unexpected("failed to copy release artifact: " + error.message());
  }

  return {};
}

std::expected<UpdateApplyPlan, std::string> UpdateChecker::plan_update() {
  std::error_code error;
  const auto dir = download_dir();
  if (!std::filesystem::exists(dir, error)) {
    return std::unexpected("there is nothing to apply; run `hyprshare update download` first");
  }

  // The newest artifact in the download directory is the one to apply.
  std::filesystem::path archive;
  std::filesystem::file_time_type newest{};
  for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto modified = entry.last_write_time(error);
    if (archive.empty() || modified > newest) {
      archive = entry.path();
      newest = modified;
    }
  }

  if (archive.empty()) {
    return std::unexpected("there is nothing to apply; run `hyprshare update download` first");
  }

  const auto unpack_dir = dir / "unpacked";
  std::filesystem::remove_all(unpack_dir, error);
  std::filesystem::create_directories(unpack_dir, error);
  if (error) {
    return std::unexpected("failed to create the unpack directory: " + error.message());
  }

  platform::ProcessRunner runner;
  if (!runner.command_exists("tar")) {
    return std::unexpected("tar is needed to unpack the release");
  }

  const std::string command = "tar -xf " + util::shell_quote(archive.string()) +
    " -C " + util::shell_quote(unpack_dir.string());
  const auto result = runner.run_shell(command);
  if (result.exit_code != 0) {
    return std::unexpected("failed to unpack the release: " + util::trim(result.output));
  }

  // The installer is the script of the release, or its makefile. It may sit
  // in a directory of its own, so the search goes a few levels deep.
  static const char* candidates[] = {"install.sh", "install", "Makefile", "makefile"};
  std::filesystem::path installer;
  for (const char* candidate : candidates) {
    std::filesystem::recursive_directory_iterator it(
      unpack_dir, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; ++it) {
      if (it->path().filename() == candidate && it->is_regular_file()) {
        installer = it->path();
        break;
      }
      // The search does not have to walk the whole tree.
      if (it.depth() >= 3) {
        it.disable_recursion_pending();
      }
    }
    if (!installer.empty()) {
      break;
    }
  }

  if (installer.empty()) {
    return std::unexpected(
      "the release does not contain an installer; unpack it by hand in " + unpack_dir.string());
  }

  return UpdateApplyPlan{archive, unpack_dir, installer};
}

std::expected<void, std::string> UpdateChecker::apply_update() {
  const auto plan = plan_update();
  if (!plan.has_value()) {
    return std::unexpected(plan.error());
  }

  platform::ProcessRunner runner;
  // The installer runs in the directory it was unpacked into.
  const std::string command = "cd " + util::shell_quote(plan->installer.parent_path().string()) +
    " && ./" + plan->installer.filename().string();
  const auto result = runner.run_shell(command);
  if (result.exit_code != 0) {
    return std::unexpected("the installer failed: " + util::trim(result.output));
  }
  return {};
}

void UpdateChecker::set_current_version(const std::string& version) {
  current_version_ = version;
}

std::string UpdateChecker::current_version() const {
  return current_version_;
}

void UpdateChecker::set_release_manifest(const std::filesystem::path& path) {
  release_manifest_ = path;
}

void UpdateChecker::set_release_url(const std::string& url) {
  release_url_ = url;
}

std::string UpdateChecker::release_url() const {
  return release_url_;
}

std::filesystem::path UpdateChecker::release_manifest() const {
  if (!release_manifest_.empty()) {
    return release_manifest_;
  }
  if (const char* from_env = getenv("HYPRSHARE_RELEASE_MANIFEST")) {
    if (from_env[0] != '\0') {
      return std::filesystem::path(from_env);
    }
  }
  return state_dir() / "release.json";
}

std::filesystem::path UpdateChecker::download_dir() const {
  return state_dir() / "downloads";
}

}  // namespace hyprshare::update
