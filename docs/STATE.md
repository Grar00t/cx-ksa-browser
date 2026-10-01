# Project State

## Current Status
Phase: P06 implemented on branch `prompt-P06`.
CX now has logical tabs over one WebView2 surface, immediate SQLite session
persistence, local history search, local bookmarks, and browser navigation UI.

## Browser
The window provides a tab strip, address bar, Back, Forward, Reload, Go,
Bookmark, New Tab, Close Tab, History, and Bookmarks.
Tabs are lightweight logical tabs; switching navigates the shared WebView2
surface to that tab's persisted URL.

## Persistence
SQLite migration v3 adds `bookmarks`.
Tab creation, close, activation, URL, and title changes are committed when
they occur. The active tab ID is stored in `settings`, so restore does not
depend on a clean application shutdown.

History is local in SQLite and searchable by URL or title.
Bookmarks are local in SQLite and unique by URL.

## Verification On 2026-10-01
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: CTest passed.
- PASS: direct GoogleTest run: 36 tests, 36 passed.
- PASS: 13 simultaneous logical tabs persisted and closed correctly.
- PASS: crash helper exited with code 77 and the next process restored 3 tabs
  plus the previously active tab.
- PASS: measured maximum logical-tab creation time over 12 creates:
  `2.9042 ms` (<100 ms).
- PASS: 25 cycles of 12 create/close operations did not leak process handles
  beyond the test tolerance.
- PASS: local history search matches title and URL.
- PASS: bookmark add/update/delete is local and persistent.
- PASS: javascript-style explicit schemes are rejected by address parsing.

## Privacy Boundary
CX passes WebView2 flags disabling sync, component updates, and background
networking and does not implement any application-level sync or telemetry.
The browser must permit user-requested HTTP/HTTPS traffic for P06 navigation.
The separate Evergreen WebView2 runtime is third-party runtime code; absence
of all vendor-runtime telemetry is not claimed without independent runtime
verification.

## Next Prompt
P07.
