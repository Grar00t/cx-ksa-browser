# CX Build User Guide

## 1. Starting CX

Installed mode launches CX Build from the Start Menu or cx.exe. Portable mode must be started through run_portable.bat so APPDATA and LOCALAPPDATA are redirected into the portable folder.

The browser opens with a tab strip, address field, navigation controls, bookmark control, and Browser and Agent menus.

## 2. Navigation and tabs

Use New Tab to create a tab. CX persists tab state immediately in local SQLite when session restore is enabled. The address field accepts HTTP and HTTPS destinations and normalizes normal host names to HTTPS. Unsafe schemes such as javascript are rejected by the navigation controller.

Back, Forward, Reload, New Tab, Close Tab, History, and Bookmarks operate on local browser state. More than ten logical tabs and abrupt-session restoration are covered by automated tests.

## 3. History and bookmarks

History and bookmarks are local database records. History saving is disabled by default unless the user enables it. Bookmarks are unique by URL and persist locally.

Use Browser > History or Browser > Bookmarks to inspect the local collections.

## 4. Settings and Privacy

Open Browser > Settings & Privacy.

The window contains four pages:
- Privacy & Security
- Agent Permissions
- MCP Allowlist
- Data & Storage

Privacy settings are persisted immediately. The privacy dashboard shows enabled agent permissions, active MCP state, MCP allowlist count, and local-data size.

## 5. Agent permissions

The local agent starts denied. Each capability is represented by its own permission. Granting agent.run does not automatically grant page reading, navigation, tab management, clipboard writing, native messaging, or MCP access.

Revoke a capability from Settings or use Revoke All Permissions. Revoking agent.run stops the agent and MCP; revoking mcp.connect stops MCP.

## 6. MCP allowlist

CX MCP support is local stdio only. A server must be represented by an allowlisted absolute executable path. Shell lookup, remote URL commands, and unknown server IDs are rejected.

Adding a server to the allowlist does not start it. The agent must be running, mcp.connect must be granted, and the user must explicitly connect.

Requests are rate-limited per server. CX local logs record metadata such as server ID and byte count, not JSON request bodies.

## 7. Local data

Installed mode stores CX application data under the current Windows profile, primarily under APPDATA\CX Build.

Important local files include:
- data.db for SQLite state
- config.json for P08 configuration
- config\mcp_allowlist.json for local MCP servers
- logs for local agent and MCP logs

The Data & Storage page can clear local history and bookmarks and show the data location.

## 8. Configuration import, export, backup, and restore

P08 configuration import and export accepts local absolute filesystem paths only. UNC and remote-drive paths are rejected.

Database backup and restore uses SQLite snapshot and integrity verification. Restore accepts only a valid CX schema and preserves the current database for rollback until the replacement has reopened and verified successfully.

## 9. Portable mode

Keep cx.exe and run_portable.bat together. The launcher redirects application data into a data folder beside the executable.

Portable behavior has been verified from a relocated writable folder. Physical USB media was not present during the P09 verification, so USB hardware execution remains a separately testable deployment condition.

## 10. Networking and privacy

Normal browsing contacts the destination selected by the user. CX does not implement application telemetry upload, cloud synchronization, or an application auto-updater.

CX uses the Microsoft WebView2 Runtime. The runtime is a separate trust boundary and can perform Microsoft or Windows network activity. CX does not claim that the complete renderer process tree is network silent.

For formal product privacy terms, see PRIVACY_POLICY.md.
