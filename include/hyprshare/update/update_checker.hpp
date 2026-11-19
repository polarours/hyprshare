#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace hyprshare::update {

struct UpdateInfo {
  std::string current_version;
  std::string latest_version;
  bool update_available = false;
  // False when no release information could be obtained. The checker never
  // contacts a release server on its own, so it only knows about releases
  // described by the configured manifest.
  bool release_info_available = false;
  std::string download_url;
  std::string release_notes;
  std::int64_t release_date = 0;
};

// What it would take to apply a downloaded release.
struct UpdateApplyPlan {
  std::filesystem::path archive;
  std::filesystem::path unpack_dir;
  std::filesystem::path installer;
};

class UpdateChecker {
 public:
  UpdateChecker();
  ~UpdateChecker();

  std::expected<UpdateInfo, std::string> check_for_updates();
  std::expected<void, std::string> download_update(const std::string& url);
  // Unpacks the downloaded release and looks for the installer in it.
  std::expected<UpdateApplyPlan, std::string> plan_update();
  // Unpacks the downloaded release and runs the installer in it.
  std::expected<void, std::string> apply_update();

  void set_current_version(const std::string& version);
  std::string current_version() const;

  // The file that describes the latest release. When it is not set, the
  // checker falls back to $HYPRSHARE_RELEASE_MANIFEST and then to
  // release.json in the state directory.
  void set_release_manifest(const std::filesystem::path& path);
  std::filesystem::path release_manifest() const;

  // A release manifest that is fetched over HTTP. When it is set, it takes
  // precedence over the local manifest.
  void set_release_url(const std::string& url);
  std::string release_url() const;

  // The directory downloads are written to.
  std::filesystem::path download_dir() const;

 private:
  std::string current_version_;
  std::filesystem::path release_manifest_;
  std::string release_url_;
};

}  // namespace hyprshare::update
