# CX Build UI Design Language

## Direction

CX Build uses **Najdi utility minimalism**: compact, high-contrast Windows UI that feels local, deliberate, and engineered rather than decorative.

The design is original to CX Build. It must not copy Comet, Perplexity, Chromium-fork assets, logos, proprietary bundles, or private UI source.

## Palette

The central design tokens are defined in `src/ui/design_tokens.h`. `src/ui/najdi_theme.{h,cpp}` consumes those tokens for native Win32 painting and behavior.

| Token | RGB | Hex | Role |
| --- | --- | --- | --- |
| Background | 30, 28, 25 | `#1E1C19` | warm charcoal base |
| Surface | 40, 37, 31 | `#28251F` | controls and inactive tabs |
| Surface Raised | 49, 45, 37 | `#312D25` | pressed/active surfaces |
| Input | 24, 23, 20 | `#181714` | address and list fields |
| Border | 73, 66, 55 | `#494237` | quiet structure |
| Text | 242, 238, 228 | `#F2EEE4` | primary high-contrast text |
| Muted Text | 183, 173, 154 | `#B7AD9A` | secondary labels |
| Sand | 199, 169, 107 | `#C7A96B` | active/focus accent |
| Olive | 127, 133, 87 | `#7F8557` | reserved secondary accent |

## Token system

`src/ui/design_tokens.h` is the single source for:
- color: warm charcoal surfaces, high-contrast text, sand/olive accents
- spacing: 2/4/6/8/12/16/24 px compact scale
- typography: Unicode Segoe UI body/semibold metrics
- radius: control 3 px, surface 4 px, hard maximum 4 px
- border: 1 px standard border and explicit focus treatment
- focus: sand focus and olive primary-action accent
- density: compact control, tab, toolbar, input and settings metrics
- window/settings layout metrics used by the main browser and Settings UI

## Rules

- No gradients.
- No glassmorphism or translucent decorative panels.
- No decorative noise or imitation-browser chrome.
- Compact control density with clear hierarchy.
- Use native Windows behavior where it improves reliability and accessibility.
- Keep WebView2 content visually separate from CX-owned chrome.
- Unicode Win32 controls and Segoe UI are used for Arabic-friendly rendering. `theme::ApplyLayoutDirection` provides an exercised Win32 RTL/LTR mirroring hook; translated Arabic copy and a user-facing language switch are separate work and are not claimed yet.
- Agent and MCP consent state must remain obvious and must never be hidden by styling.
- Privacy/security controls must favor clarity over visual novelty.

## Current implementation

The first implemented slice themes the main browser chrome and Settings & Privacy window with:
- dark native title bars where supported by Windows DWM
- warm-charcoal window and tab surfaces
- sand active-tab/focus accents
- compact owner-drawn browser and settings push buttons
- high-contrast input/list backgrounds with tokenized 1 px borders
- rounded controls capped at 4 px radius
- sand focus indication and olive primary-action border treatment
- shared Unicode UI font handling and an RTL/LTR layout-direction hook
- tokenized compact spacing/density in browser chrome and Settings layout

The native menu bar remains a Windows system surface rather than a custom imitation menu.

## Product boundary

CX is a WebView2-based Windows browser shell with local SQLite state, deny-by-default Agent/MCP permissions, reproducible build/test/coverage paths, and no CX telemetry/cloud sync/remote-update client by default.

Offline/local ALLaM retrieval is a product direction, but **no ALLaM integration exists in the current repository at this revision**. It must not be presented as shipped until an implementation and verification path are added.
