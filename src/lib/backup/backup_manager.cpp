#include "hyprshare/backup/backup_manager.hpp"

#include <chrono>
#include <fstream>

namespace hyprshare::backup {

namespace {

// Copy a directory tree while skipping a single entry. The backup directory
// lives inside the state directory, so a plain recursive copy would nest every
// previously created backup inside the new one and grow without bound.
std::error_code copy_tree_skipping(
  const std::filesystem::path& source,
  const std::filesystem::path& destination,
  const std::filesystem::path& skip) {
  std::error_code error;
  if (!std::filesystem::exists(source, error)) {
    return error;
  }

  std::filesystem::create_directories(destination, error);
  if (error) {
    return error;
  }

  for (const auto& entry : std::filesystem::directory_iterator(source, error)) {
    if (error) {
      return error;
    }
    const auto child = entry.path();
    if (std::filesystem::weakly_canonical(child, error) == skip) {
      continue;
    }
    if (entry.is_directory()) {
      error = copy_tree_skipping(child, destination / child.filename(), skip);
      if (error) {
        return error;
      }
    } else {
      std::filesystem::copy_file(child, destination / child.filename(),
        std::filesystem::copy_options::overwrite_existing, error);
      if (error) {
        return error;
      }
    }
  }

  return error;
}

}  // namespace

BackupManager::BackupManager() {
  static_cast<void>(create_directories());
}

BackupManager::~BackupManager() {}

std::expected<std::string, std::string> BackupManager::create_backup(const std::string& description) {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

  // Millisecond precision keeps ids unique when several backups are created
  // within the same second; a second-resolution id would silently overwrite
  // the previous backup.
  const auto id_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

  const auto backup_id = "backup-" + std::to_string(id_stamp);
  const auto backup_path = backup_dir() / backup_id;

  std::error_code error;
  std::filesystem::create_directories(backup_path, error);
  if (error) {
    return std::unexpected("failed to create backup directory: " + error.message());
  }

  BackupManifest manifest;
  manifest.backup_id = backup_id;
  manifest.description = description;
  manifest.created_at = now;

  const auto config_dir = std::filesystem::path(getenv("HOME")) / ".config/hyprshare";
  if (std::filesystem::exists(config_dir)) {
    std::filesystem::copy(config_dir, backup_path / "config", std::filesystem::copy_options::recursive, error);
    if (!error) {
      manifest.entries.push_back({"config", "config", 0, now});
    }
  }

  const auto state_dir = std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare";
  if (std::filesystem::exists(state_dir)) {
    std::error_code canonical_error;
    const auto skip = std::filesystem::weakly_canonical(backup_dir(), canonical_error);
    error = canonical_error ? canonical_error : std::error_code{};
    if (!error) {
      error = copy_tree_skipping(state_dir, backup_path / "state", skip);
    }
    if (!error) {
      manifest.entries.push_back({"state", "state", 0, now});
    }
  }

  std::ofstream manifest_file(backup_path / "manifest.json");
  manifest_file << "{\"backup_id\":\"" << manifest.backup_id
                << "\",\"description\":\"" << manifest.description
                << "\",\"created_at\":" << manifest.created_at << "}\n";

  return backup_id;
}

std::expected<void, std::string> BackupManager::restore_backup(const std::string& backup_id) {
  const auto backup_path = backup_dir() / backup_id;
  if (!std::filesystem::exists(backup_path)) {
    return std::unexpected("backup not found: " + backup_id);
  }

  const auto config_dir = std::filesystem::path(getenv("HOME")) / ".config/hyprshare";
  const auto state_dir = std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare";

  std::error_code error;
  if (std::filesystem::exists(backup_path / "config")) {
    std::filesystem::copy(backup_path / "config", config_dir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
      return std::unexpected("failed to restore config: " + error.message());
    }
  }

  if (std::filesystem::exists(backup_path / "state")) {
    std::filesystem::copy(backup_path / "state", state_dir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
      return std::unexpected("failed to restore state: " + error.message());
    }
  }

  return {};
}

std::expected<void, std::string> BackupManager::delete_backup(const std::string& backup_id) {
  const auto backup_path = backup_dir() / backup_id;
  if (!std::filesystem::exists(backup_path)) {
    return std::unexpected("backup not found: " + backup_id);
  }

  std::error_code error;
  std::filesystem::remove_all(backup_path, error);
  if (error) {
    return std::unexpected("failed to delete backup: " + error.message());
  }

  return {};
}

std::vector<BackupManifest> BackupManager::list_backups() const {
  std::vector<BackupManifest> backups;

  if (!std::filesystem::exists(backup_dir())) {
    return backups;
  }

  for (const auto& entry : std::filesystem::directory_iterator(backup_dir())) {
    if (entry.is_directory()) {
      const auto manifest_path = entry.path() / "manifest.json";
      if (std::filesystem::exists(manifest_path)) {
        BackupManifest manifest;
        manifest.backup_id = entry.path().filename().string();
        manifest.description = "Backup";
        backups.push_back(manifest);
      }
    }
  }

  return backups;
}

std::expected<BackupManifest, std::string> BackupManager::inspect_backup(const std::string& backup_id) {
  const auto backup_path = backup_dir() / backup_id;
  if (!std::filesystem::exists(backup_path)) {
    return std::unexpected("backup not found: " + backup_id);
  }

  BackupManifest manifest;
  manifest.backup_id = backup_id;
  manifest.description = "Backup";
  manifest.created_at = 0;

  for (const auto& entry : std::filesystem::directory_iterator(backup_path)) {
    manifest.entries.push_back({entry.path().filename().string(), entry.path().filename().string(), 0, 0});
  }

  return manifest;
}

std::filesystem::path BackupManager::backup_dir() const {
  return std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare/backups";
}

std::expected<void, std::string> BackupManager::create_directories() {
  std::error_code error;
  std::filesystem::create_directories(backup_dir(), error);
  if (error) {
    return std::unexpected("failed to create backup directory: " + error.message());
  }
  return {};
}

}  // namespace hyprshare::backup
