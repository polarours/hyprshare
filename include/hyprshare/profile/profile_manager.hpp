#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace hyprshare::profile {

struct UserProfile {
  std::string id;
  std::string name;
  std::string email;
  std::filesystem::path config_dir;
  std::filesystem::path state_dir;
  std::int64_t created_at = 0;
  bool active = false;
};

class ProfileManager {
 public:
  ProfileManager();
  ~ProfileManager();

  std::expected<void, std::string> create_profile(const std::string& name, const std::string& email = "");
  std::expected<void, std::string> delete_profile(const std::string& profile_id);
  std::expected<void, std::string> switch_profile(const std::string& profile_id);
  std::expected<UserProfile, std::string> get_profile(const std::string& profile_id) const;
  std::vector<UserProfile> list_profiles() const;
  std::expected<UserProfile, std::string> active_profile() const;

  std::expected<void, std::string> export_profile(const std::string& profile_id, const std::filesystem::path& export_path);
  std::expected<void, std::string> import_profile(const std::filesystem::path& import_path);

  // The profile that is in use, or an empty string when there is none.
  std::string active_profile_id() const;
  void set_active_profile_id(const std::string& profile_id);

 private:
  std::filesystem::path profiles_dir() const;
  std::expected<void, std::string> create_directories();
};

}  // namespace hyprshare::profile
