# Contributing

## Expectations

`hyprshare` is meant to look credible to experienced Linux and open-source maintainers. Contributions should preserve clear module boundaries, data-driven compatibility, and explicit security reasoning.

## Development Workflow

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Review Bar

- New features must identify whether they are content sharing, capability sharing, or connectivity sharing.
- App-specific behavior belongs in compatibility data first, not in the core planner by default.
- Optional dependencies must remain module-scoped unless there is a strong reason to promote them.
- Every meaningful architecture change should land with an ADR or update an existing ADR.
- Security-sensitive behavior must update the threat model.

## Code Style

- C++23.
- Keep interfaces small and typed.
- Prefer deterministic tests with fake data over environment-dependent tests.
- Do not add silent privilege escalation paths.

## Compatibility Data Contributions

When adding or changing a compatibility profile:

- Include concrete evidence for the app and environment.
- Prefer declarative strategy ordering and facts over code hooks.
- Mark uncertain paths as `best_effort` or `experimental` rather than overstating support.

