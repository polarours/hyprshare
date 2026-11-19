#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace hyprshare::backup {

struct BackupEntry {
  std::string name;
  std::string path;
  std::uint64_t size = 0;
  std::int64_t timestamp = 0;
};

struct BackupManifest {
  std::string backup_id;
  std::string description;
  std::int64_t created_at = 0;
  std::vector<BackupEntry> entries;
};

class BackupManager {
 public:
  BackupManager();
  ~BackupManager();

  std::expected<std::string, std::string> create_backup(const std::string& description = "");
  std::expected<void, std::string> restore_backup(const std::string& backup_id);
  std::expected<void, std::string> delete_backup(const std::string& backup_id);
  std::vector<BackupManifest> list_backups() const;
  std::expected<BackupManifest, std::string> inspect_backup(const std::string& backup_id);

 private:
  std::filesystem::path backup_dir() const;
  std::expected<void, std::string> create_directories();
};

}  // namespace hyprshare::backup
