# CX Build Security Policy

## Security posture

CX Build uses a local-first, deny-by-default design for agent and MCP capabilities. Security-sensitive configuration is validated and intended to fail closed.

P10 automated tests include unit, integration, UI smoke, local-core network, parser, backup and restore, and performance coverage. The measured P10 line coverage gate is greater than 80 percent.

## Reporting a vulnerability

Do not publish sensitive exploit details in a public issue. For a private repository, use the repository owner's private security-reporting channel or GitHub private vulnerability reporting when enabled. Include reproduction steps, affected commit, impact, and any proposed fix.

## Verified first-party controls

Current automated coverage verifies behaviors including:
- agent permissions default denied and persist explicit decisions
- unknown agent actions are rejected
- MCP servers must be explicitly allowlisted local executable paths
- malformed MCP allowlist JSON fails closed, including trailing commas
- MCP requests are rate-limited per server
- MCP payload bodies are not written to the local metadata log
- configuration corruption falls back to defaults
- remote UNC paths are rejected by configuration and backup APIs
- database restore validates integrity and the CX schema before promotion
- unsafe navigation schemes are rejected
- a local-only core test produces no increase in process-owned established TCP or UDP endpoints

## Network boundary

The no-outbound automated test applies to the tested first-party local-core path. It is not a claim that the complete browser is network silent.

Normal browsing uses the network. CX renders web content using Microsoft WebView2. The WebView2 Runtime is a third-party trust boundary and may communicate with Microsoft or Windows infrastructure independently of first-party CX services.

## MCP boundary

MCP is local stdio only in the current implementation. An allowlisted executable is still a separate local process and may itself perform arbitrary actions, including networking. Allowlisting is an explicit trust decision, not sandboxing.

## Installer boundary

The installer is per-user, contains no advertising bundle or update service, and has an optional PATH task. P09 verified cleanup of its tested install location, Start Menu entries, uninstall registration, and optional PATH entry.

Code signing is conditional on a valid certificate. The P09 verification machine had signtool.exe but no valid current-user code-signing certificate, so that package was unsigned.

## Current assessment

No critical first-party defect is known from the P10 automated test suite as of 2026-10-04. This statement is limited to the tested code and is not a substitute for an independent penetration test, renderer security review, Windows platform review, or legal/compliance audit.

See docs/THREAT_MODEL.md and docs/ARCHITECTURE.md for additional boundaries.
