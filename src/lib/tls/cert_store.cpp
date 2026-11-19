#include "hyprshare/tls/cert_store.hpp"

#include <fstream>

#include "hyprshare/tls/tls.hpp"

namespace hyprshare::tls {

CertStore::CertStore(const std::filesystem::path& state_dir)
  : keys_dir_(state_dir / "keys"),
    cert_path_(keys_dir_ / "device.pem"),
    key_path_(keys_dir_ / "device-key.pem"),
    ca_path_(keys_dir_ / "ca.pem") {
  std::error_code error;
  std::filesystem::create_directories(keys_dir_, error);
}

std::expected<void, std::string> CertStore::ensure_device_identity(const std::string& device_id) {
  if (has_device_cert()) {
    return {};
  }

  const auto result = generate_self_signed_cert(cert_path_, key_path_, device_id);
  if (!result.has_value()) {
    return std::unexpected("failed to generate device certificate: " + result.error());
  }

  return {};
}

std::expected<void, std::string> CertStore::load_device_cert() {
  if (!std::filesystem::exists(cert_path_)) {
    return std::unexpected("device certificate not found: " + cert_path_.string());
  }
  return {};
}

std::expected<void, std::string> CertStore::load_device_key() {
  if (!std::filesystem::exists(key_path_)) {
    return std::unexpected("device key not found: " + key_path_.string());
  }
  return {};
}

std::filesystem::path CertStore::cert_path() const {
  return cert_path_;
}

std::filesystem::path CertStore::key_path() const {
  return key_path_;
}

std::filesystem::path CertStore::ca_path() const {
  return ca_path_;
}

bool CertStore::has_device_cert() const {
  return std::filesystem::exists(cert_path_) && std::filesystem::exists(key_path_);
}

}  // namespace hyprshare::tls
