# Threat Model

## Assets
Local browsing data, credentials, cookies, session state, agent permissions,
MCP configuration, local databases, and user-authored files.

## Primary Threats
- Unauthorized local or remote access to browser data.
- Agent actions exceeding explicit user consent.
- MCP tools operating outside an allowlist.
- Secret leakage through logs, issues, crash uploads, or synchronization.
- Supply-chain compromise in third-party dependencies.
- Malicious web content attempting to influence privileged local actions.

## Baseline Controls
- No telemetry, crash upload, remote update, or cloud sync by default.
- Local-first storage.
- Explicit consent for agent features.
- Allowlisted and restricted MCP access.
- Minimal dependency set.
- No proprietary third-party assets or extracted bundles.

## Verification Status
P10 verification has executed automated first-party checks for deny-by-default agent behavior, MCP allowlisting and parser failure posture, unsafe/remote input rejection, and a local-core network endpoint delta test. These results do not claim that WebView2 or the full browser process tree is network silent; see `docs/STATE.md` and `SECURITY.md` for the verified scope and boundaries.
