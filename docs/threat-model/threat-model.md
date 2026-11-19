# Threat Model

## Assets

- Local user session integrity.
- Sharing policies and compatibility decisions.
- Future peer identities and trust relationships.
- Session grants and audit trails.
- Local file and clipboard contents.

## Trust Boundaries

- The user session is trusted more than the LAN.
- Local root is outside hyprshare's security boundary.
- Remote peers must never be implicitly trusted.
- Local applications are not assumed to behave consistently across packaging or runtime modes.

## Current Attack Surface

At the current scaffold stage:

- Local CLI input.
- Shipped backend and compatibility metadata.
- Environment probing via shell commands.
- Future risk: any later daemon IPC or peer transport.

## Security Rules Already Reflected In The Codebase

- No custom cryptography.
- No privileged flows are executed automatically.
- No background peer-sharing service exists yet.
- Compatibility logic is data-driven and inspectable.

## Pending Threat Model Work

- Pairing and peer identity lifecycle.
- Session grant revocation semantics.
- Support bundle redaction policy.
- Narrow privileged helper boundaries for future network or device workflows.

