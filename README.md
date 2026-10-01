# CX Build - Made with love in KSA

CX Build is an independent, local-first hybrid browser shell for Windows.
It is not affiliated with Perplexity or Comet.

## Direction
- Windows-native shell using Microsoft WebView2 for web rendering.
- C++20 for native code.
- Local-first storage and agent behavior.
- Minimal dependencies and explicit privacy defaults.
- No telemetry, analytics, crash upload, remote update, or cloud sync by default.

## Status
P04 agent foundation is implemented: native WebView2 shell, local SQLite
storage, deny-by-default agent permissions, fixed action allowlisting,
local AppData logging, consent dialogs, UI revocation, and GoogleTest coverage.

See `docs/STATE.md` for verified build/test state and known limitations.
