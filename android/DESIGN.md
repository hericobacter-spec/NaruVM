---
name: NaruVM
description: "Observed native Android mint and teal workspace, 2026-11-16."
# Observed source values. Android dimensions remain dp and sp, not CSS pixels.
colors:
  accent: "#006B5F"
  on-accent: "#FFFFFF"
  background: "#F4F8F7"
  surface: "#FFFFFF"
  ink: "#142E2D"
  muted: "#526764"
  outline: "#D4E3DF"
  tonal: "#DBEEE7"
  dark-accent: "#8AD5C2"
  dark-on-accent: "#00382E"
  dark-background: "#101B19"
  dark-surface: "#182724"
  dark-ink: "#E8F2EF"
  dark-muted: "#A8BFB8"
  dark-outline: "#354A43"
  dark-tonal: "#243D35"
typography:
  headline:
    fontFamily: "sans-serif"
    fontSize: "28sp"
    fontWeight: 700
  headline-session:
    fontFamily: "sans-serif"
    fontSize: "26sp"
    fontWeight: 700
  brand:
    fontFamily: "sans-serif"
    fontSize: "24sp"
    fontWeight: 700
  title:
    fontFamily: "sans-serif"
    fontSize: "22sp"
  body:
    fontFamily: "sans-serif"
    fontSize: "16sp"
  action:
    fontFamily: "sans-serif"
    fontSize: "15sp"
  label:
    fontFamily: "sans-serif"
    fontSize: "14sp"
  note:
    fontFamily: "sans-serif"
    fontSize: "13sp"
  log:
    fontFamily: "monospace"
    fontSize: "13sp"
rounded:
  panel: "16dp"
spacing:
  label-gap: "4dp"
  text-gap: "6dp"
  small: "8dp"
  action-gap: "10dp"
  medium: "12dp"
  large: "16dp"
  section-gap: "20dp"
  outer: "24dp"
components:
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.on-accent}"
    typography: "{typography.action}"
    rounded: "{rounded.panel}"
    padding: "8dp 16dp"
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    typography: "{typography.action}"
    rounded: "{rounded.panel}"
    padding: "8dp 16dp"
  navigation-selected:
    backgroundColor: "{colors.tonal}"
    textColor: "{colors.accent}"
    typography: "{typography.action}"
    rounded: "{rounded.panel}"
    padding: "8dp 16dp"
  navigation-idle:
    backgroundColor: "{colors.background}"
    textColor: "{colors.muted}"
    typography: "{typography.action}"
    rounded: "{rounded.panel}"
    padding: "8dp 16dp"
  panel-surface:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.panel}"
    padding: "16dp"
  panel-tonal:
    backgroundColor: "{colors.tonal}"
    rounded: "{rounded.panel}"
    padding: "24dp"
  spinner-selected-row:
    textColor: "{colors.ink}"
    typography: "{typography.body}"
    padding: "8dp 12dp"
  spinner-dropdown-row:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    typography: "{typography.body}"
    padding: "12dp 16dp"
  install-status:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    typography: "{typography.label}"
    rounded: "{rounded.panel}"
    padding: "12dp 16dp"
  log-panel:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    typography: "{typography.log}"
    rounded: "{rounded.panel}"
    padding: "20dp"
---
# Design System: NaruVM

## Overview

**Creative North Star: "Korean Stationery"**

NaruVM uses a mint ground, teal actions and white work surfaces in a native Android Views interface. The approved Korean stationery direction lands through quiet tint, legible labels, generous control groups and restrained borders.

The same semantic roles have explicit dark counterparts. Android system sans-serif, resource-backed English and Korean copy, native dropdowns and dialogs, and scalable text keep the visual system close to the host platform. The authored interface uses text, flat shapes and the bundled vector identity mark.

Evidence status: **observed on 2026-11-16**. This label applies to the implementation statements and extracted tokens below. Scope is one native implementation: `src/dev/henge/research/MainActivity.java`, `MacInputView.java`, `res/values/styles.xml`, `res/values-night/styles.xml`, English/Korean strings and `res/drawable/naruvm_icon.xml`. Sampled rendered evidence is `.impeccable/review/tablet-en.png` and `phone-large-dark.png`; current source governs changes made after captures. The surface brief records the approved direction; this document records the built system rather than promising universal device behavior.

**Key Characteristics:**

- Mint and teal with a complete paired dark palette.
- Native Android text and controls, with dp geometry and sp type.
- Flat bordered panels, wrapping labels and clear action hierarchy.

## Colors

The frontmatter records the native palette verbatim; `dark-` keys are the paired nighttime values, selected from Android's UI mode.

### Primary

- **Teal Action / Mint Action** (`accent` / `dark-accent`): primary buttons, the NaruVM wordmark and selected navigation text.
- **On Action** (`on-accent` / `dark-on-accent`): text inside primary buttons.
- **Mint Paper / Deep Mint Paper** (`tonal` / `dark-tonal`): the readiness surface and selected navigation background.

### Neutral

- **Workspace Ground** (`background` / `dark-background`): root canvas and system bars.
- **Work Surface** (`surface` / `dark-surface`): settings, secondary buttons, logs and installation status.
- **Reading Ink** (`ink` / `dark-ink`): headings, body copy and selected control values.
- **Quiet Ink** (`muted` / `dark-muted`): labels, notes, idle navigation and spinner tint.
- **Fine Outline** (`outline` / `dark-outline`): the shared panel and button stroke.

**The Paired Palette Rule.** Resolve every semantic color role through the current light or dark palette. Accent marks selection and primary actions; ink carries reading text.

## Typography

**Body and interface font:** Android `sans-serif`, inherited from the native Material NoActionBar theme. No separate decorative display role is defined. **Log font:** Android `monospace`, applied to selectable session output.

The hierarchy is a set of observed roles, not a geometric scale. Destination and readiness headings and the wordmark explicitly use bold; other weights remain the native theme defaults. The `text()` helper adds line spacing (3dp) with a multiplier of one; buttons and spinner rows retain their native text metrics.

- **Headline:** destination heading.
- **Headline session:** readiness or preparation heading.
- **Brand:** the NaruVM wordmark.
- **Title:** launch settings heading. The diagnostic subsection also uses a smaller local heading (20sp).
- **Body:** explanatory copy and spinner values.
- **Action:** sentence-case button and navigation labels.
- **Label:** field labels, session status and progress.
- **Note:** secondary explanations and device metadata.
- **Log:** selectable machine output; monospace conveys actual data.

The role sizes and explicit weights are normative in the frontmatter. Resource strings supply both English and Korean; authored controls do not uppercase their labels.

**The Native Scale Rule.** Keep text in sp and geometry in dp. Use the observed role sizes and allow Android font scaling and wrapping to change the occupied height.

## Layout

The inspected home has two reflow thresholds. At expanded width (1000dp or more), a left rail (164dp) sits beside the content; readiness and allocation share a row with weights (0.85 / 1.15) and a gap (24dp). Below that threshold, the existing implementation uses a top row of three destination buttons and stacks the two work areas. This describes the current Views composition; it does not establish a replacement for platform navigation on unrelated surfaces.

CPU and memory fields share a row at medium width (600dp or more), with a gap (16dp); below that threshold they stack with a gap (12dp). Graphics and mouse fields remain full-width below them. The main content uses outer horizontal padding from the spacing scale. Readiness uses the outer padding token; settings use the large padding token. Tight label/value relationships use the label-gap token.

Each destination owns a vertical ScrollView with bottom padding (16dp). The destination heading, summary and active installation work bar live outside those scroll areas. Text and panels wrap to content height. Spinner rows can show up to three lines and have a minimum touch height (48dp); general buttons have a minimum height (52dp).

**The Insets Rule.** Apply system window insets at the shell and immersive-tools boundaries before the internal spacing.

## Elevation & Depth

Authored panels use flat fills and a fine outline (1dp). There is no authored panel-shadow scale. The shared button helper removes the framework state-list animator. Native spinner popup and AlertDialog presentation still belong to the Android framework; they are not evidence of a custom shadow token.

**The Bordered Surface Rule.** Separate authored panels with their shared outline and tonal fill. Preserve framework behavior for dropdowns and dialogs rather than inventing an elevation vocabulary.

## Shapes

The shared panel drawable supplies gently rounded corners via the panel radius token and the fine outline. It is reused by the readiness surface, settings, secondary and primary buttons, navigation states, logs and installation status. These are rounded rectangles, not pills. Native spinner chrome and dialogs keep their framework shapes.

The bundled app identity is the simple stroked N vector in `naruvm_icon.xml`; the inspected home uses a text wordmark. This establishes an existing identity asset, not a new icon library or illustration style.

## Components

### Buttons

Native Button controls share sentence-case action text, the panel shape, a minimum height (52dp), and padding from the button component tokens. Primary buttons use the action pair; secondary buttons use work surface and reading ink. Their RippleDrawable uses the authored translucent teal feedback color (`#22006B5F` in Android ARGB notation). No web hover state or custom focus-ring token exists in this implementation. Input focus and accessibility stay with the native controls.

The launch button summary reflects the selected CPU and RAM. It is hidden when a Mac is not ready. Launch and several firmware actions explicitly reduce alpha (0.45) while disabled; other controls use native enabled state. Do not treat that selective alpha as a universal disabled-opacity token.

### Navigation

Three native buttons select Launch, Diagnostics/Tests and Logs. Selected navigation uses the tonal fill and action text; idle navigation uses workspace ground and quiet ink. Each state has the shared outline. Selection swaps the button background to the panel drawable, so it does not inherit the helper's RippleDrawable. Expanded and compact positioning are recorded in Layout.

### Resource fields

Each field is a quiet label associated with a native dropdown Spinner. The selected row has multiline body text, a minimum height (48dp) and its frontmatter padding; dropdown rows use the work surface and their own observed padding. The spinner chrome is tinted with quiet ink. CPU, RAM, graphics, mouse speed and installation version reuse this pattern. Selection is persisted and next-run resource changes update the launch summary without navigating away.

### Work surfaces and logs

The tonal readiness panel emphasizes the current machine state. The work-surface panel groups resource controls with tighter internal padding. The log surface uses its own observed padding and selectable monospace output; logs remain in their own destination. The two panel variants share shape and border rather than inventing separate elevation.

### Installation state

An unprepared machine shows preparation copy, version selection and the primary Download, install & start action; an existing machine retains launch and a collapsed Install another Mac area. Active work disables resource changes and installer version selection. The persistent work bar uses the installation-status component tokens, selectable status text limited to four lines with end ellipsis, and the native stop button. Stopping text replaces the status after a stop request; the stop button disables once cancellation is requested. Success hides this work bar.

**The Visible Work Rule.** Keep installation status and its Stop installation action outside destination scrolling and disclosure. Disable the installer disclosure while installation is active.

### Guest display and tools

The active guest uses a dark display canvas, current-session frames fitted within the available area, and native tools that can be disclosed over the display. The tool affordance and bottom panel account for system insets. Keyboard entry uses a native EditText inside an AlertDialog. Its field chrome and dialog buttons are framework-owned, so no bespoke text-field tokens are extracted.

## Do's and Don'ts

### Do:

- Do pair the semantic light and dark roles when adding a native surface.
- Do use Android text scaling, multiline labels, generated control IDs and labelFor associations for resource fields.
- Do derive launch visibility and readiness wording from the current machine state, and update the launch resource summary with the selection.
- Do keep active installation progress and the stop action visible while the destination content scrolls.

### Don't:

- Don't force the expanded rail and two-column layout into a compact window.
- Don't substitute a captured desktop for frames from the current guest session.
- Don't turn an example resource selection, device name or version result into a design token.
