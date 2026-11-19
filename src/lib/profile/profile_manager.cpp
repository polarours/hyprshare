#include "hyprshare/profile/profile_manager.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "hyprshare/util/string.hpp"

namespace hyprshare::profile {

namespace {

// Reads a string field out of the metadata document of a profile.
std::string metadata_value(const std::string& content, const std::string& key) {
  const std::string quoted = "\"" + key + "\"";
  const auto position = content.find(quoted);
  if (position == std::string::npos) {
    return "";
  }

  const auto colon = content.find(':', position + quoted.size());
  if (colon == std::string::npos) {
    return "";
  }

  const auto start = content.find('"', colon + 1);
  if (start == std::string::npos) {
    return "";
  }
  const auto end = content.find('"', start + 1);
  if (end == std::string::npos) {
    return "";
  }
  return content.substr(start + 1, end - start - 1);
}

// Reads a number field out of the metadata document of a profile.
std::int64_t metadata_number(const std::string& content, const std::string& key) {
  const std::string quoted = "\"" + key + "\"";
  const auto position = content.find(quoted);
  if (position == std::string::npos) {
    return 0;
  }

  const auto colon = content.find(':', position + quoted.size());
  if (colon == std::string::npos) {
    return 0;
  }

  std::size_t end = colon + 1;
  while (end < content.size() && (std::isdigit(static_cast<unsigned char>(content[end])) != 0 ||
                                  content[end] == '-')) {
    ++end;
  }

  try {
    return std::stoll(content.substr(colon + 1, end - colon - 1));
  } catch (const std::exception&) {
    return 0;
  }
}

}  // namespace

ProfileManager::ProfileManager() {
  static_cast<void>(create_directories());
}

ProfileManager::~ProfileManager() {}

std::expected<void, std::string> ProfileManager::create_profile(const std::string& name, const std::string& email) {
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

  // The identifier carries milliseconds, so that two profiles created in the
  // same second do not share a directory. Two created in the same millisecond
  // still would, so a number is added until the directory is free.
  const auto base_id = "profile-" + std::to_string(now);
  auto profile_id = base_id;
  for (int suffix = 2; std::filesystem::exists(profiles_dir() / profile_id); ++suffix) {
    profile_id = base_id + "-" + std::to_string(suffix);
  }
  const auto profile_dir = profiles_dir() / profile_id;

  std::error_code error;
  std::filesystem::create_directories(profile_dir, error);
  if (error) {
    return std::unexpected("failed to create profile directory: " + error.message());
  }

  UserProfile profile;
  profile.id = profile_id;
  profile.name = name;
  profile.email = email;
  profile.config_dir = profile_dir / "config";
  profile.state_dir = profile_dir / "state";
  profile.created_at = now / 1000;
  profile.active = true;

  {
    std::ofstream profile_file(profile_dir / "profile.json");
    if (!profile_file.is_open()) {
      return std::unexpected("failed to write the profile metadata");
    }
    profile_file << "{\"id\":\"" << profile.id
                 << "\",\"name\":\"" << profile.name
                 << "\",\"email\":\"" << profile.email
                 << "\",\"created_at\":" << profile.created_at << "}\n";
  }

  // The first profile becomes the active one.
  if (active_profile_id().empty()) {
    set_active_profile_id(profile_id);
  }

  return {};
}

std::expected<void, std::string> ProfileManager::delete_profile(const std::string& profile_id) {
  const auto profile_dir = profiles_dir() / profile_id;
  if (!std::filesystem::exists(profile_dir)) {
    return std::unexpected("profile not found: " + profile_id);
  }

  std::error_code error;
  std::filesystem::remove_all(profile_dir, error);
  if (error) {
    return std::unexpected("failed to delete profile: " + error.message());
  }

  if (active_profile_id() == profile_id) {
    set_active_profile_id("");
  }
  return {};
}

std::expected<void, std::string> ProfileManager::switch_profile(const std::string& profile_id) {
  const auto profile_dir = profiles_dir() / profile_id;
  if (!std::filesystem::exists(profile_dir)) {
    return std::unexpected("profile not found: " + profile_id);
  }

  set_active_profile_id(profile_id);
  return {};
}

std::expected<UserProfile, std::string> ProfileManager::get_profile(const std::string& profile_id) const {
  const auto profile_dir = profiles_dir() / profile_id;
  if (!std::filesystem::exists(profile_dir)) {
    return std::unexpected("profile not found: " + profile_id);
  }

  UserProfile profile;
  profile.id = profile_id;
  profile.name = "User";
  profile.config_dir = profile_dir / "config";
  profile.state_dir = profile_dir / "state";
  profile.created_at = 0;
  profile.active = active_profile_id() == profile_id;

  // The metadata holds the name, the address, and the time of the profile.
  std::ifstream metadata(profile_dir / "profile.json");
  if (metadata.is_open()) {
    std::ostringstream buffer;
    buffer << metadata.rdbuf();
    const std::string content = buffer.str();

    const auto name = metadata_value(content, "name");
    if (!name.empty()) {
      profile.name = name;
    }
    const auto email = metadata_value(content, "email");
    if (!email.empty()) {
      profile.email = email;
    }
    profile.created_at = metadata_number(content, "created_at");
  }

  return profile;
}

std::vector<UserProfile> ProfileManager::list_profiles() const {
  std::vector<UserProfile> profiles;

  if (!std::filesystem::exists(profiles_dir())) {
    return profiles;
  }

  const auto active = active_profile_id();
  for (const auto& entry : std::filesystem::directory_iterator(profiles_dir())) {
    if (!entry.is_directory()) {
      continue;
    }

    const auto id = entry.path().filename().string();
    const auto loaded = get_profile(id);
    if (!loaded.has_value()) {
      continue;
    }
    profiles.push_back(*loaded);
  }

  std::sort(profiles.begin(), profiles.end(),
    [](const UserProfile& a, const UserProfile& b) { return a.id < b.id; });
  return profiles;
}

std::expected<UserProfile, std::string> ProfileManager::active_profile() const {
  const auto id = active_profile_id();
  if (id.empty()) {
    return std::unexpected("no active profile");
  }
  return get_profile(id);
}

std::expected<void, std::string> ProfileManager::export_profile(
  const std::string& profile_id, const std::filesystem::path& export_path) {
  const auto profile_dir = profiles_dir() / profile_id;
  if (!std::filesystem::exists(profile_dir)) {
    return std::unexpected("profile not found: " + profile_id);
  }

  std::error_code error;
  std::filesystem::copy(profile_dir, export_path, std::filesystem::copy_options::recursive, error);
  if (error) {
    return std::unexpected("failed to export profile: " + error.message());
  }

  return {};
}

std::expected<void, std::string> ProfileManager::import_profile(const std::filesystem::path& import_path) {
  if (!std::filesystem::exists(import_path)) {
    return std::unexpected("import path not found: " + import_path.string());
  }

  const auto profile_id = import_path.filename().string();
  const auto profile_dir = profiles_dir() / profile_id;

  std::error_code error;
  std::filesystem::copy(
    import_path,
    profile_dir,
    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
    error);
  if (error) {
    return std::unexpected("failed to import profile: " + error.message());
  }

  return {};
}

std::filesystem::path ProfileManager::profiles_dir() const {
  return std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare/profiles";
}

std::string ProfileManager::active_profile_id() const {
  std::ifstream active(profiles_dir() / "active");
  if (!active.is_open()) {
    return "";
  }
  std::string id;
  std::getline(active, id);
  return util::trim(id);
}

void ProfileManager::set_active_profile_id(const std::string& profile_id) {
  const auto path = profiles_dir() / "active";
  if (profile_id.empty()) {
    std::error_code error;
    std::filesystem::remove(path, error);
    return;
  }

  std::ofstream active(path, std::ios::trunc);
  if (active.is_open()) {
    active << profile_id << "\n";
  }
}

std::expected<void, std::string> ProfileManager::create_directories() {
  std::error_code error;
  std::filesystem::create_directories(profiles_dir(), error);
  if (error) {
    return std::unexpected("failed to create profiles directory: " + error.message());
  }
  return {};
}

}  // namespace hyprshare::profile
