# ADR 0004: Data-Driven Compatibility Before Code Hooks

## Status

Accepted

## Decision

Application compatibility should live in shipped data first, with code hooks reserved for cases that cannot be represented declaratively.

## Rationale

- Most compatibility differences are ordering, prerequisites, notes, and support-tier issues.
- Declarative profiles are easier to review, update, test, and explain to users.
- This prevents the core planner from collapsing into app-name branching.

