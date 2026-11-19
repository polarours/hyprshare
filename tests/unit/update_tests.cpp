#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "hyprshare/update/update_checker.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

// A server that answers a manifest and an artifact.
class ReleaseServer {
 public:
  ReleaseServer() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 8) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    thread_ = std::thread([this]() { serve(); });
  }

  ~ReleaseServer() { stop(); }

  bool valid() const { return listen_fd_ >= 0 && port_ != 0; }
  std::string url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }

  void stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
  }

 private:
  void serve() {
    while (running_) {
      const int client = accept(listen_fd_, nullptr, nullptr);
      if (client < 0) {
        break;
      }
      handle(client);
      close(client);
    }
  }

  void handle(const int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }

    std::string response;
    if (request.rfind("GET /latest.json", 0) == 0) {
      const std::string manifest =
        "{\"version\": \"9.9.9\", \"download_url\": \"" + url("/hyprshare-9.9.9.tar.gz") +
        "\", \"release_notes\": \"Test release\"}";
      response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(manifest.size()) +
                 "\r\nConnection: close\r\n\r\n" + manifest;
    } else if (request.rfind("GET /hyprshare-9.9.9.tar.gz", 0) == 0) {
      const std::string artifact = "the release artifact";
      response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(artifact.size()) +
                 "\r\nConnection: close\r\n\r\n" + artifact;
    } else {
      response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }

    send(client, response.data(), response.size(), MSG_NOSIGNAL);
  }

 private:
  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::atomic<bool> running_{true};
  std::thread thread_;
};

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_update_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  // The checker resolves the download directory from $HOME.
  const char* original_home = getenv("HOME");
  const std::string saved_home = original_home != nullptr ? original_home : "";
  setenv("HOME", temp_root.c_str(), 1);

  ReleaseServer server;
  expect(server.valid(), "the release server should start");

  if (server.valid()) {
    hyprshare::update::UpdateChecker checker;
    checker.set_current_version("0.6.0");

    // Without a manifest there is no release information.
    {
      const auto info = checker.check_for_updates();
      expect(info.has_value(), "the check should succeed");
      if (info.has_value()) {
        expect(!info->release_info_available, "without a manifest there is no release information");
        expect(!info->update_available, "without a manifest no update is reported");
      }
    }

    // A manifest that cannot be reached is an error.
    {
      hyprshare::update::UpdateChecker broken;
      broken.set_current_version("0.6.0");
      broken.set_release_url("http://127.0.0.1:1/latest.json");
      const auto info = broken.check_for_updates();
      expect(!info.has_value(), "an unreachable manifest should be reported");
    }

    // A manifest over the network.
    {
      checker.set_release_url(server.url("/latest.json"));
      const auto info = checker.check_for_updates();
      expect(info.has_value(), "the remote manifest should be fetched");
      if (info.has_value()) {
        expect(info->release_info_available, "the release information should be available");
        expect(info->latest_version == "9.9.9", "the latest version should be read");
        expect(info->update_available, "a newer version should be reported");
        expect(info->download_url.find("hyprshare-9.9.9.tar.gz") != std::string::npos,
          "the download url should be read");
        expect(info->release_notes == "Test release", "the release notes should be read");
      }
    }

    {
      const auto info = checker.check_for_updates();
      if (info.has_value()) {
        const auto downloaded = checker.download_update(info->download_url);
        expect(downloaded.has_value(),
          "the artifact should download: " + (downloaded.has_value() ? "" : downloaded.error()));

        const auto artifact = checker.download_dir() / "hyprshare-9.9.9.tar.gz";
        expect(std::filesystem::exists(artifact), "the artifact should be in the download directory");
        if (std::filesystem::exists(artifact)) {
          std::ifstream in(artifact);
          std::string content;
          std::getline(in, content);
          expect(content == "the release artifact", "the artifact content should match");
        }
      }
    }
  }

  // Applying a release: the artifact is unpacked and the installer in it is
  // found and run.
  {
    const auto downloads = temp_root / ".local/state/hyprshare/downloads";
    std::filesystem::create_directories(downloads / "package");

    const auto installer = downloads / "package" / "install.sh";
    {
      std::ofstream out(installer);
      out << "#!/bin/sh\n";
      out << "echo installed > installed.txt\n";
    }
    {
      std::error_code error;
      std::filesystem::permissions(installer,
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_read |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_read |
        std::filesystem::perms::others_exec,
        error);
    }

    const std::string command =
      "tar -czf " + (downloads / "release.tar.gz").string() +
      " -C " + downloads.string() + " package";
    expect(std::system(command.c_str()) == 0, "the test release should be packed");

    hyprshare::update::UpdateChecker checker;
    checker.set_current_version("0.6.0");

    const auto plan = checker.plan_update();
    expect(plan.has_value(),
      "the release should be unpacked: " + (plan.has_value() ? "" : plan.error()));
    if (plan.has_value()) {
      expect(plan->installer.filename() == "install.sh",
        "the installer should be found in the release");
    }

    const auto applied = checker.apply_update();
    expect(applied.has_value(),
      "the installer should run: " + (applied.has_value() ? "" : applied.error()));
    // The installer runs in the directory it was unpacked into.
    expect(std::filesystem::exists(downloads / "unpacked" / "package" / "installed.txt"),
      "the installer should have done its work");
  }

  if (original_home != nullptr) {
    setenv("HOME", saved_home.c_str(), 1);
  } else {
    unsetenv("HOME");
  }
  std::filesystem::remove_all(temp_root);

  return test_support::report("update");
}
