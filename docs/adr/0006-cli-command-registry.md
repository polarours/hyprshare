# ADR 0006: CLI Command Registry Pattern

## Status

Accepted

## Context

The original CLI implementation used a monolithic `main.cpp` with a 3,000+ line if/else chain dispatching 55+ subcommands. This made the code difficult to maintain, test, and extend.

## Decision

We adopted a command registry pattern with:
- `CommandContext` struct holding shared state (paths, config, backends, etc.)
- `CommandHandler` function type: `std::expected<int, std::string>(*)(CommandContext&, std::vector<std::string>&)`
- `CommandRegistry` class mapping command strings to handlers
- Each command group in its own `cmd_*.cpp` file

## Consequences

- `main.cpp` reduced from 3,102 to 151 lines
- Commands are independently testable
- New commands can be added by registering a handler
- Shared utilities (arg parsing, output formatting) are centralized

## Alternatives Considered

1. **CLI framework (CLI11, argparse)** - Rejected to maintain zero external dependencies
2. **Single-file dispatch** - Rejected due to maintainability concerns
