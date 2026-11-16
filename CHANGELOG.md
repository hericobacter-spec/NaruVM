# Changelog

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
