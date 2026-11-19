#include "cmd_app.hpp"

#include <iostream>

#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/diagnostics/doctor.hpp"
#include "hyprshare/core/planner.hpp"

namespace hyprshare::cli {

using hyprshare::model::AppLaunchPlan;
using hyprshare::model::DoctorReport;

std::expected<int, std::string> handle_app(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 3 || args[1] != "launch") {
    return std::unexpected(std::string("usage: hyprshare app launch <app>"));
  }

  const auto profile = ctx.adapters.find_by_hint(args[2]);
  if (!profile.has_value()) {
    std::cerr << "error: no compatibility profile matched `" << args[2] << "`\n";
    return 1;
  }

  const DoctorReport report = ctx.doctor.run(ctx.paths, ctx.adapters, std::optional<std::string_view>(args[2]));
  const auto available_commands = ctx.launcher.resolve_available_commands(*profile);
  const AppLaunchPlan plan = ctx.planner.plan_app_launch(args[2], report.facts, ctx.config, ctx.backends, ctx.adapters, available_commands);

  if (ctx.json) {
    print_launch_plan_json(plan, ctx.dry_run);
  } else {
    print_launch_plan_text(plan, ctx.dry_run);
  }

  if (ctx.dry_run) {
    return plan.ready ? 0 : 1;
  }

  if (!plan.ready) {
    return 1;
  }

  const auto execution = ctx.launcher.execute(plan);
  if (!execution.has_value()) {
    std::cerr << "error: " << execution.error() << "\n";
    return 1;
  }
  return 0;
}

}  // namespace hyprshare::cli
