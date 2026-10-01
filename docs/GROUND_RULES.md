# Ground Rules

1. Do not copy or derive proprietary Perplexity, Comet, Chromium-fork assets, CRX files, minified bundles, logos, trademarks, or private source code.
2. Do not inspect or depend on `D:\Models\unknown`; treat it only as external evidence.
3. Build a hybrid browser shell, not a browser engine; use Microsoft WebView2 on Windows.
4. Use C++20 for native code; Assembly is reserved for the optional P29 CPU probe.
5. No telemetry, analytics, crash upload, remote update, or cloud sync by default.
6. Agent and MCP features are local-first, consent-based, allowlisted, and restricted by default.
7. Keep dependencies minimal: Windows SDK, WebView2 SDK, SQLite3, GoogleTest.
8. Every prompt must produce observable progress: buildable code, tests, docs, or packaging artifacts.
9. Never claim a test passed unless it was executed; otherwise mark it `NOT_RUN` and give the exact command.
10. Maintain `docs/STATE.md` as the single project memory file.
