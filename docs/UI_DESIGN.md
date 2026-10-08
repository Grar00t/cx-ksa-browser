# CX Build UI Design Language

## Direction

CX Build uses **Najdi utility minimalism** with an original deep-space and turquoise palette: compact, high-contrast Windows UI that feels local, deliberate, and engineered rather than decorative.

The design is original to CX Build. It must not copy Comet, Perplexity, Chromium-fork assets, logos, proprietary bundles, or private UI source.

## Palette

The central design tokens are defined in `src/ui/design_tokens.h`. `src/ui/najdi_theme.{h,cpp}` consumes those tokens for native Win32 painting and behavior. The palette and component treatment were created for CX Build; no third-party source, asset, icon, logo, string, proprietary bundle, or pixel-exact layout was imported or imitated.

| Token | Hex | Role |
| --- | --- | --- |
| Background | `#0B1020` | deep-space base |
| Surface | `#121A2E` | controls and inactive tabs |
| Surface Raised | `#1A2540` | active and pressed surfaces |
| Input | `#0A0F1C` | address bar and fields |
| Border | `#26344F` | quiet structure |
| Text | `#E8F1FF` | primary text |
| Muted Text | `#9FB0CC` | secondary text |
| Accent Turquoise | `#1FD1C6` | focus, active tab, primary action |
| Accent Turquoise Dim | `#138F89` | hover and secondary accent |
| Agent Active | `#7DE3FF` | agent-is-acting indicator |
| Danger | `#FF6B7A` | blocked, deny, and kill-switch state |

## WCAG contrast

Ratios use WCAG 2.x relative luminance from the exact sRGB token values. `DesignSystemTest.BodyTextMeetsWcagAaAcrossSurfaces` computes the same matrix and requires at least 4.5:1.

| Foreground | Background | Ratio |
| --- | --- | ---: |
| Text | Background | 16.64:1 |
| Text | Surface | 15.22:1 |
| Text | Surface Raised | 13.35:1 |
| Text | Input | 16.81:1 |
| Muted Text | Background | 8.62:1 |
| Muted Text | Surface | 7.88:1 |
| Muted Text | Surface Raised | 6.91:1 |
| Muted Text | Input | 8.70:1 |

Accent Turquoise, Agent Active, and Danger also remain at or above 4.5:1 on all four surfaces. Accent Turquoise Dim is a non-text hover/border token and must not be used for body text.

## Token system

`src/ui/design_tokens.h` is the single source for:
- color: deep-space surfaces, high-contrast text, turquoise accents, and explicit agent/danger state tokens
- spacing: 2/4/6/8/12/16/24 px compact scale
- typography: Unicode Segoe UI body/semibold metrics
- radius: control 3 px, surface 4 px, hard maximum 4 px
- border: 1 px standard border and 1 px turquoise focus treatment
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
- Unicode Win32 controls use Segoe UI with a local Tahoma Arabic fallback. P13 adds a central English/Arabic core string table and applies the exercised RTL/LTR mirroring hook to Settings & Privacy when Arabic is selected; WebView content is not mirrored. Complete translation of all secondary copy is separate work and is not claimed yet.
- Agent and MCP consent state must remain obvious and must never be hidden by styling.
- Privacy/security controls must favor clarity over visual novelty.

## Current implementation

The first implemented slice themes the main browser chrome and Settings & Privacy window with:
- dark native title bars where supported by Windows DWM
- deep-space window and tab surfaces
- turquoise active-tab/focus accents
- compact owner-drawn browser and settings push buttons
- high-contrast input/list backgrounds with tokenized 1 px borders
- rounded controls capped at 4 px radius
- turquoise focus indication and dim-turquoise primary-action border treatment
- shared Unicode UI font handling and an RTL/LTR layout-direction hook
- tokenized compact spacing/density in browser chrome and Settings layout

The native menu bar remains a Windows system surface rather than a custom imitation menu.

## Product boundary

CX is a WebView2-based Windows browser shell with local SQLite state, deny-by-default Agent/MCP permissions, reproducible build/test/coverage paths, and no CX telemetry/cloud sync/remote-update client by default.

Offline/local ALLaM retrieval is a product direction, but **no ALLaM integration exists in the current repository at this revision**. It must not be presented as shipped until an implementation and verification path are added.
