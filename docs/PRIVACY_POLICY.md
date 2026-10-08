# CX Build Privacy Policy

Effective date: 2026-10-04

This policy describes the data behavior implemented by the CX Build application. It is written for the current release-candidate codebase. It does not claim that third-party websites, Windows, or the Microsoft WebView2 Runtime follow CX application-level privacy choices.

## 1. Data controller and accounts

CX Build does not require a CX cloud account and the current application contains no CX-operated cloud account service.

## 2. Information stored by CX

CX can store browser and application state locally on the Windows device, including settings, open tabs, bookmarks, optional history, agent permission grants, the MCP allowlist, configuration, and local operational logs.

Installed mode primarily uses APPDATA\CX Build for application state. Portable mode redirects APPDATA and LOCALAPPDATA to a data directory beside the portable executable.

## 3. Browsing data

Browsing history is disabled by default in the CX privacy settings. When enabled, history is stored locally. Bookmarks and tab state are local records.

Visiting a website necessarily sends network requests to that website and may expose information according to the website, network, DNS, operating-system, and renderer behavior. Those transfers are not CX telemetry.

## 4. Telemetry, analytics, sync, and advertising

The CX first-party application does not implement an analytics upload client, application telemetry upload, cloud synchronization service, advertising SDK, or advertising identifier.

The CX installer does not include advertising offers or bundled third-party applications.

## 5. Updates

CX does not implement an application auto-update mechanism in the current release candidate. Updates require an explicit new build or installation action.

This statement does not control updates performed by Windows or the Microsoft WebView2 Runtime.

## 6. Microsoft WebView2

CX uses Microsoft WebView2 as its web renderer. WebView2 is a separate Microsoft component with its own runtime behavior. It can perform network activity associated with websites and with Microsoft or Windows runtime infrastructure.

CX configures privacy-oriented renderer arguments, but CX does not claim that the WebView2 process tree is completely network silent.

Microsoft Defender SmartScreen reputation checking is disabled by CX by default. A user can explicitly opt in in Settings; the change applies after restart. When enabled, WebView2 may send page, download, and reputation information to Microsoft under Microsoft's privacy terms and the applicable Windows or Microsoft Edge SmartScreen settings. CX does not proxy or receive those reputation queries.

Password autosave and general form autofill are disabled. Developer Tools, default context-menu extras, web messages, and host objects are also disabled unless the user explicitly enables CX Developer Mode and restarts. Developer Mode does not enable CX telemetry, cloud sync, or an auto-update client.

## 7. Agent and MCP

The local agent is permission-gated. Capabilities are denied unless granted. MCP connectivity in the current design is local stdio to an explicitly allowlisted executable path.

Starting an allowlisted local program can cause whatever behavior that separate program implements. Users should allowlist only executables they trust.

## 8. Import, export, backup, and restore

CX configuration import and export and database backup and restore are designed for local filesystem paths. UNC and remote-drive paths are rejected by these P08 APIs.

## 9. Retention and deletion

Local data remains until the user removes it, clears supported data from CX, deletes the portable data directory, or otherwise removes the files from the device.

Uninstalling the installed application removes program files and installer integration but intentionally does not automatically destroy the user data under APPDATA\CX Build.

## 10. Security

CX uses local permission gating, MCP allowlisting, input validation, SQLite integrity checks for restore, and automated security tests. No software can guarantee absolute security. The current automated test scope and known boundaries are documented in SECURITY.md.

## 11. Third parties

Websites visited through CX and the Microsoft WebView2 Runtime are third parties with their own data practices. Their collection and processing are not governed by this CX policy.

## 12. Changes to this policy

Material application behavior changes should be accompanied by an update to this policy and the relevant architecture and security documents before a release is described as complete.

Security reporting instructions are maintained in the repository SECURITY.md.
