#include "cmd_tls.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/cert_store.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_tls_cmd(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare tls <generate|verify|info|status>"));
  }

  if (args[1] == "generate") {
    const auto common_name = args.size() > 2 ? args[2] : "hyprshare-device";
    const auto cert_path = ctx.paths.state_dir / "keys" / "device.pem";
    const auto key_path = ctx.paths.state_dir / "keys" / "device-key.pem";

    // The certificate is written into a directory that does not exist yet on
    // a fresh installation.
    std::error_code error;
    std::filesystem::create_directories(cert_path.parent_path(), error);
    if (error) {
      std::cerr << "error: failed to create the key directory: " << error.message() << "\n";
      return 1;
    }

    auto result = hyprshare::tls::generate_self_signed_cert(cert_path, key_path, common_name);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"ok\",\"cert\":\"" << cert_path.string()
                << "\",\"key\":\"" << key_path.string() << "\"}\n";
    } else {
      std::cout << "Certificate generated:\n";
      std::cout << "  Cert: " << cert_path << "\n";
      std::cout << "  Key:  " << key_path << "\n";
    }
    return 0;
  }

  if (args[1] == "verify") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare tls verify <cert-file>"));
    }

    const auto cert_path = args[2];
    // The certificate is loaded into a context, so the context has to exist.
    auto context = hyprshare::tls::create_client_context();
    if (!context.has_value()) {
      std::cerr << "error: " << context.error() << "\n";
      return 1;
    }

    auto load_result = hyprshare::tls::load_certificate(*context, cert_path);
    if (!load_result.has_value()) {
      std::cerr << "error: " << load_result.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"status\":\"ok\",\"cert\":\"" << cert_path << "\"}\n";
    } else {
      std::cout << "Certificate verified: " << cert_path << "\n";
    }
    return 0;
  }

  if (args[1] == "info") {
    if (args.size() < 3) {
      return std::unexpected(std::string("usage: hyprshare tls info <cert-file>"));
    }

    const auto cert_path = args[2];
    const auto info = hyprshare::tls::inspect_certificate(cert_path);
    if (!info.has_value()) {
      std::cerr << "error: " << info.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      std::cout << "{\"cert\":\"" << cert_path << "\"";
      std::cout << ",\"subject\":\"" << info->subject << "\"";
      std::cout << ",\"issuer\":\"" << info->issuer << "\"";
      std::cout << ",\"not_before\":\"" << info->not_before << "\"";
      std::cout << ",\"not_after\":\"" << info->not_after << "\"";
      std::cout << ",\"serial\":" << info->serial;
      std::cout << ",\"self_signed\":" << std::string(info->self_signed ? "true" : "false");
      std::cout << ",\"sha256\":\"" << info->sha256_fingerprint << "\"}\n";
    } else {
      std::cout << "Certificate: " << cert_path << "\n";
      std::cout << "  Subject:  " << info->subject << "\n";
      std::cout << "  Issuer:   " << info->issuer << "\n";
      std::cout << "  Valid:    " << info->not_before << " - " << info->not_after << "\n";
      std::cout << "  Serial:   " << info->serial << "\n";
      std::cout << "  SHA-256:  " << info->sha256_fingerprint << "\n";
      std::cout << "  Self signed: " << (info->self_signed ? "yes" : "no") << "\n";
    }
    return 0;
  }

  if (args[1] == "status") {
    hyprshare::tls::CertStore cert_store(ctx.paths.state_dir);
    const bool has_cert = cert_store.has_device_cert();

    if (ctx.json) {
      std::cout << "{\"has_cert\":" << (has_cert ? "true" : "false") << "}\n";
    } else {
      std::cout << "Device certificate: " << (has_cert ? "exists" : "not found") << "\n";
      if (has_cert) {
        std::cout << "  Cert: " << cert_store.cert_path() << "\n";
        std::cout << "  Key:  " << cert_store.key_path() << "\n";
      }
    }
    return 0;
  }

  return std::unexpected("unknown tls subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
