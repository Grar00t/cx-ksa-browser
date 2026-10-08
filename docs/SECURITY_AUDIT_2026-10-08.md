# Security audit baseline (2026-10-08)

Scope: commit `3adfe08499b980a104e2102eabfd80043ca9029c`; reviewed `src/browser`, `src/app_window.cpp`, `src/agent`, `src/mcp`, `src/storage`, and `src/config`. This document records observed implementation gaps before remediation. It does not claim that a control exists merely because a setting or UI label exists.

## Findings

| ID | Severity | Finding | Source evidence | Failing contract test |
|---|---|---|---|---|
| CX-SEC-001 | High | WebView2 is created without explicitly disabling password autosave, general autofill, status-bar UI, DevTools, or default context menus. SmartScreen opt-in is not wired. | `src/app_window.cpp:838-895` configures only browser arguments and background color; no `ICoreWebView2Settings*` calls exist. | `HardeningAudit.WebViewSettingsAreExplicitAndFailClosed` |
| CX-SEC-002 | High | Permission, download, and web-resource events are not registered, so the shell has no deny-by-default policy or per-origin prompt for those surfaces. | `src/app_window.cpp:897-947` registers navigation, source-change, completion, and new-window handlers only. | `HardeningAudit.SensitiveWebViewEventsHaveDenyByDefaultHandlers` |
| CX-SEC-003 | High | Popup/new-window requests are automatically converted into a tab after only a scheme check; there is no user confirmation or origin policy. | `src/app_window.cpp:1073-1097`. | `HardeningAudit.NewWindowsRequireExplicitConsent` |
| CX-SEC-004 | High | Agent permissions are persistent, global capability booleans, not grants bound to a tab and origin. | `src/agent/permissions.cpp:62-79`; `src/storage/migrations.h:42-46`. | `HardeningAudit.AgentPermissionGrantsBindTabAndOrigin` |
| CX-SEC-005 | High | MCP server identity is an ID plus executable path and arguments. No executable hash or server version is stored or verified before `CreateProcessW`. | `src/mcp/allowlist_manager.cpp:142-203,576-616`; `src/mcp/mcp_client.cpp:173-281`. | `HardeningAudit.McpIdentityIsHashAndVersionPinned` |
| CX-SEC-006 | High | MCP request and response frames are size/newline checked but are not parsed or schema-validated as JSON-RPC; method, tool, argument shape, IDs, and unknown fields are unchecked. | `src/mcp/mcp_client.cpp:342-429,481-531`. | `HardeningAudit.JsonRpcIsParsedAndStrictlyValidated` |
| CX-SEC-007 | Medium | MCP rate limiting is keyed only by server ID, not by tool and tab. | `src/mcp/rate_limiter.cpp:14-32`. | `HardeningAudit.RateLimitsBindToolAndTab` |
| CX-SEC-008 | High | The agent/MCP log is plain tab-separated text without a hash chain or systematic secret redaction. | `src/agent/agent_core.cpp:55-71`; MCP sanitization at `src/mcp/mcp_client.cpp:99-120` only replaces control characters and truncates. | `HardeningAudit.AuditLogIsHashChainedAndRedacted` |
| CX-SEC-009 | Medium | Database/config/log directories are created with inherited ACLs and files are opened without explicit owner-only ACL hardening or reparse-point checks. Portable-mode placement is not selected by the runtime. | `src/storage/database.cpp:67-113`; `src/config/config_manager.cpp:595-610`; `src/agent/agent_core.cpp:12-44`; `src/main.cpp:18-95`. | `HardeningAudit.LocalDataHasAclPortableExportAndWipeControls` |
| CX-SEC-010 | High | No local-model adapter or endpoint validation exists. Consequently there is no enforced loopback-only model endpoint and no tested agent-off behavior on model failure. | Repository tree at the audited commit has no local-model module; `src/main.cpp:18-95` constructs no model adapter. | `HardeningAudit.LocalModelAdapterIsLoopbackOnly` |
| CX-SEC-011 | High | There is no page-content trust boundary, hostile-content marker, or sensitive-action confirmation model for form submit, purchase, file transfer, credential fields, or cross-origin navigation. | `src/agent/permissions.cpp:11-36` defines only broad capabilities; `src/agent/agent_core.cpp:105-125` authorizes by action string alone. | `HardeningAudit.PageContentIsDataAndSensitiveActionsNeedConfirmation` |
| CX-SEC-012 | Medium | The WebView2 version is pinned but its downloaded package is not hash verified; vendored dependency hashes are not recorded. Required linker hardening flags and warnings-as-errors are not explicit. | `CMakeLists.txt:14-40,48-183`. | `HardeningAudit.BuildPinsHashesAndEnablesMitigations` |
| CX-SEC-013 | Medium | CI has build/test/coverage/package jobs, but no dependency-review or secret-scanning job. | `.github/workflows/ci.yml:1-92`. | `HardeningAudit.CiScansDependenciesAndSecrets` |

## Existing controls that were verified

- Top-level navigation allows only HTTP, HTTPS, and exact `about:blank`; `javascript:`, `data:`, `file:`, and unknown schemes fail closed in `src/browser/navigation_controller.cpp:486-496`.
- Navigation completions are correlated by WebView2 navigation ID and update the originating tab in `src/browser/navigation_controller.cpp:213-322`; existing test `BrowserTest.NavigationCompletionUpdatesOriginatingTabById` covers late completion after a tab switch.
- MCP config parsing rejects unknown fields and caps the config file at 1 MiB in `src/mcp/allowlist_manager.cpp:18,142-301,400-423`.
- The MCP transport caps a frame at 4 MiB and applies a server-level rate limit in `src/mcp/mcp_client.cpp:18,350-381,498-500`.
- The existing no-outbound test is `SecurityTest.LocalCoreCreatesNoOutboundEndpoints` in `tests/security_test.cpp:160-213`. It exercises the local core, not WebView2 browsing traffic.

## Test intent

The tests added with this report are deliberately red characterization/contract tests. Each names a required control and fails against the audited baseline. Remediation pull requests should make the relevant test green by implementing the control; tests must not be weakened or deleted to obtain a green run.
