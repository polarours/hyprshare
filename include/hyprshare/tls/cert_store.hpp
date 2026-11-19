#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace hyprshare::tls {

class CertStore {
 public:
  explicit CertStore(const std::filesystem::path& state_dir);

  std::expected<void, std::string> ensure_device_identity(const std::string& device_id);
  std::expected<void, std::string> load_device_cert();
  std::expected<void, std::string> load_device_key();

  std::filesystem::path cert_path() const;
  std::filesystem::path key_path() const;
  std::filesystem::path ca_path() const;

  bool has_device_cert() const;

 private:
  std::filesystem::path keys_dir_;
  std::filesystem::path cert_path_;
  std::filesystem::path key_path_;
  std::filesystem::path ca_path_;
};

}  // namespace hyprshare::tls
