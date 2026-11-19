#include "cmd_doctor.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/diagnostics/support_bundle.hpp"

namespace hyprshare::cli {

using hyprshare::diagnostics::FailThreshold;
using hyprshare::diagnostics::SupportBundle;
using hyprshare::diagnostics::SupportBundleOptions;
using hyprshare::model::DoctorReport;

std::expected<int, std::string> handle_doctor(CommandContext& ctx, std::vector<std::string>& args) {
  const auto app = take_option(args, "--app");
  const auto fail_on = take_option(args, "--fail-on");

  FailThreshold threshold = FailThreshold::None;
  if (fail_on.has_value()) {
    if (*fail_on == "warning") {
      threshold = FailThreshold::Warning;
    } else if (*fail_on == "error") {
      threshold = FailThreshold::Error;
    } else {
      return std::unexpected("invalid --fail-on value: " + *fail_on + " (expected 'warning' or 'error')");
    }
  }

  auto doctor = ctx.doctor;
  doctor.set_fail_threshold(threshold);
  const DoctorReport report = doctor.run(ctx.paths, ctx.adapters, app.has_value() ? std::optional<std::string_view>(*app) : std::nullopt);
  if (ctx.json) {
    print_doctor_json(report);
  } else {
    print_doctor_text(report);
  }
  return doctor.exit_code_for_report(report);
}

std::expected<int, std::string> handle_check(CommandContext& ctx, std::vector<std::string>&) {
  auto doctor = ctx.doctor;
  doctor.set_fail_threshold(FailThreshold::Error);
  const DoctorReport report = doctor.run(ctx.paths, ctx.adapters);
  if (ctx.json) {
    print_doctor_json(report);
  } else {
    print_doctor_text(report);
  }
  return doctor.exit_code_for_report(report);
}

std::expected<int, std::string> handle_support(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2 || args[1] != "bundle") {
    return std::unexpected(std::string("unknown support subcommand"));
  }

  const auto output_dir = take_option(args, "--to");
  const auto app = take_option(args, "--app");
  const DoctorReport report = ctx.doctor.run(ctx.paths, ctx.adapters, app.has_value() ? std::optional<std::string_view>(*app) : std::nullopt);
  SupportBundleOptions options;
  if (output_dir.has_value()) {
    options.output_dir = *output_dir;
  }
  if (app.has_value()) {
    options.app_hint = *app;
  }

  const auto bundle = hyprshare::diagnostics::write_support_bundle(ctx.paths, ctx.config, ctx.backends, ctx.adapters, report, options);
  if (!bundle.has_value()) {
    std::cerr << "error: " << bundle.error() << "\n";
    return 1;
  }
  if (ctx.json) {
    print_support_bundle_json(*bundle);
  } else {
    print_support_bundle_text(*bundle);
  }
  return 0;
}

}  // namespace hyprshare::cli
