#include "hyprshare/core/planner.hpp"

#include <algorithm>

#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

double base_score(const model::SupportTier tier) {
  switch (tier) {
    case model::SupportTier::Primary:
      return 100.0;
    case model::SupportTier::Supported:
      return 80.0;
    case model::SupportTier::BestEffort:
      return 60.0;
    case model::SupportTier::Experimental:
      return 30.0;
  }
  return 0.0;
}

std::vector<model::Strategy> default_screen_strategies(const Config& config) {
  std::vector<model::Strategy> strategies;
  strategies.push_back({
    .backend_id = "portal-screencast",
    .conditions = {"wayland_session", "pipewire_active", "wireplumber_active", "xdg_desktop_portal_active", "xdph_active"},
    .expose_as = "screen",
  });

  if (config.screen.allow_fallback_to_obs) {
    strategies.push_back({
      .backend_id = "obs-virtual-camera",
      .conditions = {"obs_available", "v4l2loopback_present"},
      .expose_as = "camera",
    });
  }

  return strategies;
}

double preference_boost(const std::vector<std::string>& preferred_backends, const std::string& backend_id) {
  for (std::size_t index = 0; index < preferred_backends.size(); ++index) {
    if (preferred_backends[index] == backend_id) {
      return 12.0 - static_cast<double>(index);
    }
  }
  return 0.0;
}

std::string build_app_launch_command(const std::string& executable, const std::vector<std::string>& env_overrides) {
  std::string command;
  if (!env_overrides.empty()) {
    command = "env";
    for (const auto& entry : env_overrides) {
      command += " ";
      command += util::shell_quote(entry);
    }
    command += " ";
  }
  command += util::shell_quote(executable);
  return command;
}

void append_note_if_missing(std::vector<std::string>& notes, const std::string& note) {
  if (std::find(notes.begin(), notes.end(), note) == notes.end()) {
    notes.push_back(note);
  }
}

}  // namespace

model::SharePlan SharePlanner::plan_screen(
  const std::string_view app_hint,
  const model::FactMap& facts,
  const Config& config,
  const BackendRegistry& backends,
  const AdapterRegistry& adapters) const {
  model::SharePlan plan;
  plan.capability = model::CapabilityKind::Screen;
  plan.app_hint = std::string(app_hint);

  std::vector<model::Strategy> strategies;
  if (const auto profile = adapters.find_by_hint(app_hint); profile.has_value()) {
    strategies = profile->screen_strategies;
    plan.notes = profile->notes;
    if (strategies.empty()) {
      strategies = default_screen_strategies(config);
      plan.notes.push_back("App profile did not define screen strategies, so the generic chain was used.");
    }
  } else {
    strategies = default_screen_strategies(config);
    if (!plan.app_hint.empty()) {
      plan.notes.push_back("No app adapter matched the requested app hint. The generic strategy chain was used.");
    }
  }

  for (const auto& strategy : strategies) {
    const auto backend = backends.find(strategy.backend_id);
    if (!backend.has_value()) {
      continue;
    }

    model::PlanCandidate candidate;
    candidate.backend = *backend;
    for (const auto& prerequisite : backend->prerequisites) {
      const auto fact = facts.find(prerequisite);
      if (fact == facts.end() || !fact->second) {
        candidate.unmet_conditions.push_back(prerequisite);
      }
    }

    for (const auto& condition : strategy.conditions) {
      const auto fact = facts.find(condition);
      if (fact == facts.end() || !fact->second) {
        candidate.unmet_conditions.push_back(condition);
      }
    }

    std::sort(candidate.unmet_conditions.begin(), candidate.unmet_conditions.end());
    candidate.unmet_conditions.erase(
      std::unique(candidate.unmet_conditions.begin(), candidate.unmet_conditions.end()),
      candidate.unmet_conditions.end());

    candidate.viable = candidate.unmet_conditions.empty();
    candidate.score = base_score(candidate.backend.support_tier) +
                      preference_boost(config.screen.preferred_backends, candidate.backend.id) -
                      static_cast<double>(candidate.unmet_conditions.size() * 9);
    plan.candidates.push_back(std::move(candidate));
  }

  std::sort(plan.candidates.begin(), plan.candidates.end(), [](const model::PlanCandidate& left, const model::PlanCandidate& right) {
    return left.score > right.score;
  });

  for (const auto& candidate : plan.candidates) {
    if (candidate.viable) {
      plan.selected_backend_id = candidate.backend.id;
      break;
    }
  }

  if (plan.selected_backend_id.empty() && !plan.candidates.empty()) {
    plan.notes.push_back("No backend is currently fully viable. Run `hyprshare doctor` to inspect unmet prerequisites.");
  }

  return plan;
}

model::AppLaunchPlan SharePlanner::plan_app_launch(
  const std::string_view app_hint,
  const model::FactMap& facts,
  const Config& config,
  const BackendRegistry& backends,
  const AdapterRegistry& adapters,
  const std::span<const std::string> available_launch_commands) const {
  model::AppLaunchPlan launch_plan;
  launch_plan.app_hint = std::string(app_hint);

  const auto profile = adapters.find_by_hint(app_hint);
  if (!profile.has_value()) {
    launch_plan.notes.push_back("No compatibility profile matched the requested app.");
    return launch_plan;
  }

  launch_plan.profile_id = profile->id;
  launch_plan.display_name = profile->display_name;
  launch_plan.env_overrides = profile->launch_env;
  launch_plan.notes = profile->notes;

  if (!available_launch_commands.empty()) {
    launch_plan.resolved_command = available_launch_commands.front();
  } else if (!profile->launch_commands.empty()) {
    launch_plan.notes.push_back("No known launch command from the app profile is currently available on PATH.");
  } else {
    launch_plan.notes.push_back("The app profile does not declare launch commands yet.");
  }

  const model::SharePlan share_plan = plan_screen(app_hint, facts, config, backends, adapters);
  for (const auto& note : share_plan.notes) {
    append_note_if_missing(launch_plan.notes, note);
  }

  for (const auto& candidate : share_plan.candidates) {
    if (candidate.viable && candidate.backend.launch_supported) {
      launch_plan.selected_backend_id = candidate.backend.id;
      break;
    }
  }

  if (launch_plan.selected_backend_id.empty()) {
    launch_plan.selected_backend_id = share_plan.selected_backend_id;
  }

  if (launch_plan.resolved_command.empty()) {
    return launch_plan;
  }

  if (launch_plan.selected_backend_id == "obs-virtual-camera") {
    launch_plan.actions.push_back({
      .kind = model::LaunchActionKind::StartHelper,
      .description = "Start OBS virtual camera",
      .command = "obs --startvirtualcam --minimize-to-tray --multi",
    });
    append_note_if_missing(
      launch_plan.notes,
      "OBS virtual camera is preferred for app-aware launch because hyprshare can orchestrate it directly.");
  } else if (launch_plan.selected_backend_id == "portal-screencast") {
    append_note_if_missing(launch_plan.notes, "The app will still need to initiate screen sharing from its native UI after launch.");
  } else if (launch_plan.selected_backend_id.empty()) {
    append_note_if_missing(launch_plan.notes, "No viable screen-share backend is available. The launch plan can only start the app.");
  }

  launch_plan.actions.push_back({
    .kind = model::LaunchActionKind::LaunchApp,
    .description = "Launch target app",
    .command = build_app_launch_command(launch_plan.resolved_command, launch_plan.env_overrides),
  });
  launch_plan.ready = true;
  return launch_plan;
}

}  // namespace hyprshare::core
