# Changelog

## Unreleased — Input and display response — 2026-11-17

Observed source changes and authored-test scope:

- Bound pending input, combine adjacent relative motion without losing net travel,
  preserve key/button boundaries, and prioritize normal stop over queued input.
- Recover explicitly from input overload; atomic USB all-up and newest keyboard
  state retention prevent the reproduced dropped-modifier-release scenario.
- Prepare shared frames on a worker with one bounded UI handoff; use NEON BGRA
  conversion and retain only the final Bitmap copy/presentation on the UI thread.
- Correct the large-frame fixture selector and require a 1280-pixel-wide mode;
  support a fresh test install and PATH-only JDKs in the new host checks.
- Run `python3 android/check_response.py` for authored host regression checks.
  These changes do not establish physical macOS speed, historical reboot repair
  or long-run stability. Existing published APKs remain unchanged.

## v0.1.2-preview — Graphics preflight repair — 2026-11-17

- Reject Vulkan devices without required shared host-memory import before adoption,
  preserving explicit backend selection and the actual pixel capability gate.
- Prevent software shader JIT SIGILL on the observed inconsistent emulator CPU:
  a child-only capability shim removes derived SVE/SME flags whose base is absent.
- Use a bounded app-private Mesa shader cache instead of an inaccessible Termux path.
- On one Android35 ARM64 emulator: five Auto passes, 150 readback stress frames and
  five CPU/MMU/shutdown suites passed; no crash was logged. A small repeated graphics
  probe measured 143.7ms without cache versus 70.0ms with warm cache (five each).
  This is not macOS performance, physical-device stability or a new guest test.
- Preserve v0.1/v0.1.1-preview and their assets; same release signer, version code 3.
  See [reproduction and limits](docs/graphics-verification.md).

## v0.1.1-preview — Experimental APK — 2026-11-17

- Added explicit release signing, non-debuggable manifest, compiler-path remapping
  and native stripping; preserved research signing and the original v0.1 assets.
- Provided the ARM64 graphics/software/PanVK APK alongside project and dependency
  source archives, notices, checksums and the public signer fingerprint.
- Added a disk-free libplist behavior probe and recipient repackage/signing helper;
  changed LGPL source and independent-key installation observed on Android35 ARM64.
- Documented the different-key update restriction and existing-data preservation.
  Exact-candidate physical guest, sustained use and full automatic install remain
  unverified; this release makes no new speed or stability claim.

## Unreleased — Documentation

- Expanded English/Korean project overview and step-by-step source/APK/guest
  installation guides, with build-profile selection, TB373FU Auto/PanVK behavior,
  resource and storage estimates, USB updates, input/networking and troubleshooting.
- Included both guides in the source-publication allowlist. No runtime change,
  new device acceptance, APK release or change to the v0.1 tag.
- Corrected `--panvk-userptr-probe` help text to describe its existing use by
  the TB373FU Auto/Mali PanVK guest backend as well as diagnostics.

## v0.1 — Experimental source preview — 2026-11-16

- Independent NaruVM app branding with English/Korean native mint/teal UI,
  launch/resource settings, separate diagnostics/logs and system dark mode.
- External mouse travel independent of display scaling, preserved historical
  motion samples, user gain0.5–3× and immediate physical-event forwarding.
- Experimental Apple download/resume/size/SHA-256 checks, pinned BSD LZFSE ROM
  decoder, isolated fresh restore, successful-flush gate and automatic first boot.
  Versions15/26/27 selectable;26/27 and complete automatic install untested.
- Android NDK/Bionic host support, separate RW/RX JIT mappings, virtual-device,
  networking and restore adapters for the Henge ARM64 engine.
- Native Android session/input/frame UI, user-selectable CPU/RAM and graphics
  settings, recommended2CPU/2048MiB guest defaults and manual VM stop.
- Experimental Reims → metal2vulkan → Vulkan integration, observed Mali path,
  CPU rendering fallback, and an optional separately supplied Turnip loader.
- CPU feature/JIT checks and disk-free shared-memory/pixel preflight before
  starting a guest. Other-device execution and16K runtime remain untested.
- CPU metadata scheduling and scanout/readback research optimizations; no
  overall macOS speed or near-native responsiveness claim.
- Source-only export, local Git staging, license/provenance inventories,
  community files and read-only host/source CI preparation.

There is no public binary release in this preview. GPU faults, tablet reboots,
slow interaction, full fresh-machine graphics reconstruction and exact binary
corresponding-source coverage remain open. Historical observations are bounded
research results, not completed product acceptance.


## 2026-11-16 — External input and publication inputs

- Added focused external mouse movement, drag, wheel and held keyboard handling;
  release held input on focus/capture loss. Host and Android authored dispatch
  checks pass; physical USB/Bluetooth-to-macOS behavior remains untested.
- Added offline packaged license/source-information viewer.
- Collected selected Cargo/Mesa/Rust source inputs and original pinned graphics
  snapshots alongside Termux/LLVM inputs. Source and binary readiness stay separate.
- Stage source-built sampler workers consistently during graphics builds.
