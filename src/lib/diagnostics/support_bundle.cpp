#include "hyprshare/diagnostics/support_bundle.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>

#include "hyprshare/util/string.hpp"

namespace hyprshare::diagnostics {

namespace {

std::string yes_no(const bool value) { return value ? "yes" : "no"; }

std::expected<void, std::string> write_text_file(
  const std::filesystem::path& path,
  const std::string& content) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return std::unexpected("failed to write support bundle file: " + path.string());
  }
  file << content;
  return {};
}

std::filesystem::path default_bundle_dir(const platform::Paths& paths) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return paths.state_dir / "support" / ("hyprshare-support-" + std::to_string(seconds));
}

std::string summary_text(
  const platform::Paths& paths,
  const core::Config& config,
  const core::BackendRegistry& backends,
  const core::AdapterRegistry& adapters,
  const SupportBundleOptions& options) {
  std::ostringstream stream;
  stream << "hyprshare support bundle\n";
  stream << "version: " << HYPRSHARE_VERSION << "\n";
  if (options.app_hint.has_value()) {
    stream << "app_hint: " << *options.app_hint << "\n";
  }
  stream << "\npaths\n";
  stream << "source_dir: " << paths.source_dir << "\n";
  stream << "data_dir: " << paths.data_dir << "\n";
  stream << "config_dir: " << paths.config_dir << "\n";
  stream << "config_file: " << paths.config_file << "\n";
  stream << "state_dir: " << paths.state_dir << "\n";
  stream << "cache_dir: " << paths.cache_dir << "\n";
  stream << "runtime_dir: " << paths.runtime_dir << "\n";
  stream << "\nconfig\n";
  stream << "general.default_scope: " << config.general.default_scope << "\n";
  stream << "general.discoverability: " << config.general.discoverability << "\n";
  stream << "general.log_level: " << config.general.log_level << "\n";
  stream << "screen.preferred_backends: " << util::join(config.screen.preferred_backends, ", ") << "\n";
  stream << "screen.allow_fallback_to_obs: " << yes_no(config.screen.allow_fallback_to_obs) << "\n";
  stream << "files.receive_dir: " << config.files.receive_dir << "\n";
  stream << "files.folder_shares_read_only_by_default: " << yes_no(config.files.folder_shares_read_only_by_default) << "\n";
  stream << "security.require_explicit_clipboard_approval: " << yes_no(config.security.require_explicit_clipboard_approval) << "\n";
  stream << "security.default_session_ttl_minutes: " << config.security.default_session_ttl_minutes << "\n";
  stream << "\ncatalogs\n";
  stream << "backends: " << backends.all().size() << "\n";
  stream << "compat_profiles: " << adapters.all().size() << "\n";
  return stream.str();
}

std::string doctor_text(const model::DoctorReport& report) {
  std::ostringstream stream;
  stream << "facts\n";
  for (const auto& [fact, value] : report.facts) {
    stream << fact << ": " << yes_no(value) << "\n";
  }
  stream << "\nchecks\n";
  for (const auto& check : report.checks) {
    stream << check.id << " [" << model::to_string(check.status) << "/" << model::to_string(check.severity) << "] "
           << check.summary << "\n";
    for (const auto& remediation : check.remediation) {
      stream << "  fix: " << remediation << "\n";
    }
  }
  return stream.str();
}

std::string backends_text(const core::BackendRegistry& backends) {
  std::ostringstream stream;
  for (const auto& backend : backends.all()) {
    stream << backend.id << "\n";
    stream << "  capability: " << model::to_string(backend.capability) << "\n";
    stream << "  support_tier: " << model::to_string(backend.support_tier) << "\n";
    stream << "  optional: " << yes_no(backend.optional) << "\n";
    stream << "  launch_supported: " << yes_no(backend.launch_supported) << "\n";
    stream << "  prerequisites: " << util::join(backend.prerequisites, ", ") << "\n";
    stream << "  summary: " << backend.summary << "\n";
  }
  return stream.str();
}

std::string compat_apps_text(const core::AdapterRegistry& adapters) {
  std::ostringstream stream;
  for (const auto& profile : adapters.all()) {
    stream << profile.id << " (" << profile.display_name << ")\n";
    stream << "  matches: " << util::join(profile.matches, ", ") << "\n";
    stream << "  launch_commands: " << util::join(profile.launch_commands, ", ") << "\n";
    stream << "  launch_env: " << util::join(profile.launch_env, ", ") << "\n";
    for (const auto& strategy : profile.screen_strategies) {
      stream << "  screen_strategy: " << strategy.backend_id;
      if (!strategy.conditions.empty()) {
        stream << " when " << util::join(strategy.conditions, ", ");
      }
      if (!strategy.expose_as.empty()) {
        stream << " expose_as=" << strategy.expose_as;
      }
      stream << "\n";
    }
    for (const auto& note : profile.notes) {
      stream << "  note: " << note << "\n";
    }
  }
  return stream.str();
}

std::string manifest_text(const SupportBundle& bundle) {
  std::ostringstream stream;
  stream << "directory: " << bundle.directory << "\n";
  stream << "files:\n";
  for (const auto& file : bundle.files) {
    stream << "- " << file.filename().string() << "\n";
  }
  return stream.str();
}

}  // namespace

std::expected<SupportBundle, std::string> write_support_bundle(
  const platform::Paths& paths,
  const core::Config& config,
  const core::BackendRegistry& backends,
  const core::AdapterRegistry& adapters,
  const model::DoctorReport& report,
  const SupportBundleOptions& options) {
  SupportBundle bundle;
  bundle.directory = options.output_dir.empty() ? default_bundle_dir(paths) : options.output_dir;

  std::error_code error;
  if (std::filesystem::exists(bundle.directory, error)) {
    return std::unexpected("support bundle output already exists: " + bundle.directory.string());
  }
  std::filesystem::create_directories(bundle.directory, error);
  if (error) {
    return std::unexpected("failed to create support bundle directory `" + bundle.directory.string() + "`: " + error.message());
  }

  const std::vector<std::pair<std::string, std::string>> files{
    {"summary.txt", summary_text(paths, config, backends, adapters, options)},
    {"doctor.txt", doctor_text(report)},
    {"backends.txt", backends_text(backends)},
    {"compat-apps.txt", compat_apps_text(adapters)},
  };

  for (const auto& [name, content] : files) {
    const auto path = bundle.directory / name;
    const auto written = write_text_file(path, content);
    if (!written.has_value()) {
      return std::unexpected(written.error());
    }
    bundle.files.push_back(path);
  }

  const auto manifest_path = bundle.directory / "manifest.txt";
  const auto manifest_written = write_text_file(manifest_path, manifest_text(bundle));
  if (!manifest_written.has_value()) {
    return std::unexpected(manifest_written.error());
  }
  bundle.files.push_back(manifest_path);
  return bundle;
}

}  // namespace hyprshare::diagnostics
