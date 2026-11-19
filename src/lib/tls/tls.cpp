#include "hyprshare/tls/tls.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstring>
#include <ctime>
#include <iostream>

namespace hyprshare::tls {

namespace {

struct SslDeleter {
  void operator()(SSL* ssl) const { SSL_free(ssl); }
};

struct SslCtxDeleter {
  void operator()(SSL_CTX* ctx) const { SSL_CTX_free(ctx); }
};

struct BIODeleter {
  void operator()(BIO* bio) const { BIO_free(bio); }
};

struct EVPPKeyDeleter {
  void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};

struct X509Deleter {
  void operator()(X509* cert) const { X509_free(cert); }
};

using SslPtr = std::unique_ptr<SSL, SslDeleter>;
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;
using BIOptr = std::unique_ptr<BIO, BIODeleter>;
using EVPPKeyPtr = std::unique_ptr<EVP_PKEY, EVPPKeyDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

std::string get_openssl_error() {
  const auto err = ERR_get_error();
  if (err == 0) {
    return "unknown OpenSSL error";
  }
  char buf[256];
  ERR_error_string_n(err, buf, sizeof(buf));
  return std::string(buf);
}

}  // namespace

std::expected<TlsContext, std::string> create_client_context() {
  SSL_CTX* raw_ctx = SSL_CTX_new(TLS_client_method());
  if (!raw_ctx) {
    return std::unexpected("failed to create SSL context: " + get_openssl_error());
  }

  SSL_CTX_set_options(raw_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

  TlsContext ctx;
  ctx.ssl_ctx = raw_ctx;
  return ctx;
}

std::expected<TlsContext, std::string> create_server_context() {
  SSL_CTX* raw_ctx = SSL_CTX_new(TLS_server_method());
  if (!raw_ctx) {
    return std::unexpected("failed to create SSL context: " + get_openssl_error());
  }

  SSL_CTX_set_options(raw_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

  TlsContext ctx;
  ctx.ssl_ctx = raw_ctx;
  return ctx;
}

std::expected<void, std::string> load_certificate(TlsContext& ctx, const std::filesystem::path& cert_path) {
  auto* ssl_ctx = static_cast<SSL_CTX*>(ctx.ssl_ctx);
  if (SSL_CTX_use_certificate_chain_file(ssl_ctx, cert_path.c_str()) != 1) {
    return std::unexpected("failed to load certificate: " + get_openssl_error());
  }
  return {};
}

std::expected<void, std::string> load_private_key(TlsContext& ctx, const std::filesystem::path& key_path) {
  auto* ssl_ctx = static_cast<SSL_CTX*>(ctx.ssl_ctx);
  if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
    return std::unexpected("failed to load private key: " + get_openssl_error());
  }

  if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
    return std::unexpected("private key does not match certificate: " + get_openssl_error());
  }

  return {};
}

std::expected<void, std::string> load_ca_certificate(TlsContext& ctx, const std::filesystem::path& ca_path) {
  auto* ssl_ctx = static_cast<SSL_CTX*>(ctx.ssl_ctx);
  if (SSL_CTX_load_verify_locations(ssl_ctx, ca_path.c_str(), nullptr) != 1) {
    return std::unexpected("failed to load CA certificate: " + get_openssl_error());
  }
  SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
  return {};
}

std::expected<void, std::string> set_fd(TlsContext& ctx, const int fd) {
  ctx.fd = fd;

  auto* ssl = SSL_new(static_cast<SSL_CTX*>(ctx.ssl_ctx));
  if (!ssl) {
    return std::unexpected("failed to create SSL: " + get_openssl_error());
  }

  if (SSL_set_fd(ssl, fd) != 1) {
    SSL_free(ssl);
    return std::unexpected("failed to set fd: " + get_openssl_error());
  }

  ctx.ssl = ssl;
  return {};
}

std::expected<void, std::string> enable_peer_verification(TlsContext& ctx) {
  auto* ssl_ctx = static_cast<SSL_CTX*>(ctx.ssl_ctx);
  if (SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
    return std::unexpected("failed to load the system trust store: " + get_openssl_error());
  }
  SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
  return {};
}

std::expected<void, std::string> set_peer_hostname(TlsContext& ctx, const std::string& host) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  if (!ssl) {
    return std::unexpected("TLS session is not established");
  }
  if (SSL_set1_host(ssl, host.c_str()) != 1) {
    return std::unexpected("failed to set the expected peer host name: " + get_openssl_error());
  }
  return {};
}

std::expected<void, std::string> set_connect_state(TlsContext& ctx) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  if (!ssl) {
    return std::unexpected("TLS session is not established");
  }
  SSL_set_connect_state(ssl);
  return {};
}

std::expected<void, std::string> set_accept_state(TlsContext& ctx) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  if (!ssl) {
    return std::unexpected("TLS session is not established");
  }
  SSL_set_accept_state(ssl);
  return {};
}

std::expected<void, std::string> handshake(TlsContext& ctx) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  const auto ret = SSL_do_handshake(ssl);
  if (ret != 1) {
    const auto err = SSL_get_error(ssl, ret);
    return std::unexpected("TLS handshake failed: " + get_openssl_error() + " (SSL_ERROR=" + std::to_string(err) + ")");
  }
  return {};
}

std::expected<int, std::string> tls_read(TlsContext& ctx, void* buf, const int num) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  const auto ret = SSL_read(ssl, buf, num);
  if (ret <= 0) {
    const auto err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_ZERO_RETURN) {
      return 0;
    }
    return std::unexpected("TLS read failed: " + get_openssl_error());
  }
  return ret;
}

std::expected<int, std::string> tls_write(TlsContext& ctx, const void* buf, const int num) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  const auto ret = SSL_write(ssl, buf, num);
  if (ret <= 0) {
    return std::unexpected("TLS write failed: " + get_openssl_error());
  }
  return ret;
}

std::expected<std::string, std::string> get_peer_cert_fingerprint(TlsContext& ctx) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  auto* peer_cert = SSL_get_peer_certificate(ssl);
  if (!peer_cert) {
    return std::unexpected("no peer certificate");
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;

  if (X509_digest(peer_cert, EVP_sha256(), digest, &digest_len) != 1) {
    X509_free(peer_cert);
    return std::unexpected("failed to compute certificate fingerprint");
  }

  X509_free(peer_cert);

  std::string result;
  result.reserve(digest_len * 3);
  for (unsigned int i = 0; i < digest_len; ++i) {
    if (i > 0) {
      result.push_back(':');
    }
    char hex[3];
    std::snprintf(hex, sizeof(hex), "%02x", digest[i]);
    result += hex;
  }

  return result;
}

std::expected<CertificateInfo, std::string> inspect_certificate(
  const std::filesystem::path& cert_path) {
  std::error_code error;
  if (!std::filesystem::exists(cert_path, error)) {
    return std::unexpected("certificate not found: " + cert_path.string());
  }

  BIO* bio = BIO_new_file(cert_path.c_str(), "r");
  if (bio == nullptr) {
    return std::unexpected("failed to open the certificate: " + get_openssl_error());
  }

  X509* certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (certificate == nullptr) {
    return std::unexpected("failed to read the certificate: " + get_openssl_error());
  }

  CertificateInfo info;

  char buffer[512];
  const X509_NAME* subject = X509_get_subject_name(certificate);
  if (subject != nullptr) {
    X509_NAME_oneline(subject, buffer, sizeof(buffer));
    info.subject = buffer;
  }
  const X509_NAME* issuer = X509_get_issuer_name(certificate);
  if (issuer != nullptr) {
    X509_NAME_oneline(issuer, buffer, sizeof(buffer));
    info.issuer = buffer;
  }

  const ASN1_INTEGER* serial = X509_get_serialNumber(certificate);
  if (serial != nullptr) {
    info.serial = ASN1_INTEGER_get(serial);
  }

  const auto format_time = [](const ASN1_TIME* time) -> std::string {
    if (time == nullptr) {
      return "";
    }
    struct tm parsed{};
    if (ASN1_TIME_to_tm(time, &parsed) != 1) {
      return "";
    }
    char formatted[64] = {0};
    if (std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z", &parsed) == 0) {
      return "";
    }
    return formatted;
  };
  info.not_before = format_time(X509_get0_notBefore(certificate));
  info.not_after = format_time(X509_get0_notAfter(certificate));

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_length = 0;
  if (X509_digest(certificate, EVP_sha256(), digest, &digest_length) == 1) {
    static const char* hex = "0123456789abcdef";
    std::string fingerprint;
    fingerprint.reserve(digest_length * 2);
    for (unsigned int index = 0; index < digest_length; ++index) {
      fingerprint += hex[(digest[index] >> 4) & 0x0F];
      fingerprint += hex[digest[index] & 0x0F];
    }
    info.sha256_fingerprint = fingerprint;
  }

  // A certificate that issues itself is self signed, when its own signature
  // checks out against its own key.
  if (X509_NAME_cmp(X509_get_subject_name(certificate), X509_get_issuer_name(certificate)) == 0) {
    EVP_PKEY* public_key = X509_get_pubkey(certificate);
    if (public_key != nullptr) {
      info.self_signed = X509_verify(certificate, public_key) == 1;
      EVP_PKEY_free(public_key);
    }
  }

  X509_free(certificate);
  return info;
}

std::expected<std::string, std::string> get_local_cert_fingerprint(TlsContext& ctx) {
  auto* ssl = static_cast<SSL*>(ctx.ssl);
  auto* local_cert = SSL_get_certificate(ssl);
  if (!local_cert) {
    return std::unexpected("no local certificate");
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;

  if (X509_digest(local_cert, EVP_sha256(), digest, &digest_len) != 1) {
    return std::unexpected("failed to compute certificate fingerprint");
  }

  std::string result;
  result.reserve(digest_len * 3);
  for (unsigned int i = 0; i < digest_len; ++i) {
    if (i > 0) {
      result.push_back(':');
    }
    char hex[3];
    std::snprintf(hex, sizeof(hex), "%02x", digest[i]);
    result += hex;
  }

  return result;
}

void close_context(TlsContext& ctx) {
  if (ctx.ssl) {
    SSL_shutdown(static_cast<SSL*>(ctx.ssl));
    SSL_free(static_cast<SSL*>(ctx.ssl));
    ctx.ssl = nullptr;
  }
  if (ctx.ssl_ctx) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ctx.ssl_ctx));
    ctx.ssl_ctx = nullptr;
  }
  ctx.fd = -1;
}

namespace {

// A common name that is an IPv4 literal has to be recorded as an IP address
// in the subject alternative name, not as a DNS name.
bool looks_like_ipv4(const std::string& value) {
  unsigned int octets[4] = {0, 0, 0, 0};
  int consumed = 0;
  if (sscanf(value.c_str(), "%u.%u.%u.%u%n", &octets[0], &octets[1], &octets[2], &octets[3],
           &consumed) != 4) {
    return false;
  }
  if (consumed != static_cast<int>(value.size())) {
    return false;
  }
  for (const unsigned int octet : octets) {
    if (octet > 255) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::expected<void, std::string> generate_self_signed_cert(
  const std::filesystem::path& cert_path,
  const std::filesystem::path& key_path,
  const std::string& common_name) {

  EVPPKeyPtr pkey(EVP_PKEY_new());
  if (!pkey) {
    return std::unexpected("failed to create EVP_PKEY: " + get_openssl_error());
  }

  EVP_PKEY_CTX* pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (!pkey_ctx) {
    return std::unexpected("failed to create key context: " + get_openssl_error());
  }

  EVP_PKEY_keygen_init(pkey_ctx);
  EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048);

  EVP_PKEY* raw_pkey = nullptr;
  EVP_PKEY_keygen(pkey_ctx, &raw_pkey);
  EVP_PKEY_CTX_free(pkey_ctx);

  if (!raw_pkey) {
    return std::unexpected("failed to generate key: " + get_openssl_error());
  }
  pkey.reset(raw_pkey);

  X509Ptr x509(X509_new());
  if (!x509) {
    return std::unexpected("failed to create X509: " + get_openssl_error());
  }

  ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1);
  X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(x509.get()), 365 * 24 * 60 * 60L);
  X509_set_pubkey(x509.get(), pkey.get());

  // Version 3 with the extensions a peer verification needs. A version 1
  // certificate carries no extensions at all, so it cannot be used as a trust
  // anchor and a host name can never be checked against it.
  X509_set_version(x509.get(), 2);

  // OpenSSL 3.x returns a const X509_NAME*; we own x509 and are still
  // constructing it, so casting away const to populate CN/issuer is safe.
  X509_NAME* name = const_cast<X509_NAME*>(X509_get_subject_name(x509.get()));
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
    reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0);

  // A self-signed certificate is its own issuer.
  X509_set_issuer_name(x509.get(), name);

  const bool common_name_is_ip = looks_like_ipv4(common_name);
  const std::string subject_alt_name =
    (common_name_is_ip ? "IP:" : "DNS:") + common_name;

  struct Extension {
    int nid;
    const char* value;
  };
  const Extension extensions[] = {
    {NID_basic_constraints, "critical,CA:TRUE"},
    {NID_key_usage, "critical,digitalSignature,keyEncipherment,keyCertSign"},
    {NID_ext_key_usage, "serverAuth,clientAuth"},
    {NID_subject_alt_name, subject_alt_name.c_str()},
  };

  for (const auto& extension : extensions) {
    X509_EXTENSION* handle = X509V3_EXT_conf_nid(nullptr, nullptr, extension.nid, extension.value);
    if (!handle) {
      return std::unexpected("failed to build the certificate extension: " + get_openssl_error());
    }
    const int added = X509_add_ext(x509.get(), handle, -1);
    X509_EXTENSION_free(handle);
    if (added != 1) {
      return std::unexpected("failed to add the certificate extension: " + get_openssl_error());
    }
  }

  if (X509_sign(x509.get(), pkey.get(), EVP_sha256()) <= 0) {
    return std::unexpected("failed to sign certificate: " + get_openssl_error());
  }

  BIOptr cert_bio(BIO_new_file(cert_path.c_str(), "w"));
  if (!cert_bio) {
    return std::unexpected("failed to open cert file for writing: " + cert_path.string());
  }
  PEM_write_bio_X509(cert_bio.get(), x509.get());

  BIOptr key_bio(BIO_new_file(key_path.c_str(), "w"));
  if (!key_bio) {
    return std::unexpected("failed to open key file for writing: " + key_path.string());
  }
  PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);

  return {};
}

}  // namespace hyprshare::tls
