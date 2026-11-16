# NaruVM

[한국어 안내](README.ko.md)

An independent, experimental Android port of [Henge](https://github.com/Leviidev/Henge).
It retains Henge's ARM64-to-ARM64 JIT, software MMU and virtual devices, with
Android NDK/Bionic host adapters and an experimental Reims → metal2vulkan →
Vulkan graphics path. It is not an official Android release from upstream Henge.

**Source preview, not a production release.** GPU faults, tablet reboots and slow
interaction remain unresolved. No near-native performance or everyday stability
claim is made. The publication set contains source, patches and notices, not an
APK, Apple image, guest disk, account data or signing key.

## Status and devices

Observed on 2026-11-16: one physical Lenovo TB373FU, Android 16, 4K host pages.
Desktop and local development-tool execution were observed in bounded research
runs. The latest portability smoke test reached the local login screen, accepted
input and stopped normally. These observations do not establish sustained use.

| Target | Current evidence |
| --- | --- |
| TB373FU / Dimensity 8300 / Mali | Bounded actual guest runs and disk-free pixel checks |
| Y700/Legion Tab, Snapdragon Galaxy tablets/phones | Capability/selection code and specification research; actual execution untested |
| Other ARM64 Android devices | Experimental capability-gated path; no all-device support claim |
| 16K host pages | Build alignment/policy checks only; runtime untested |

The [device matrix](android/device-support.json) names exact models and primary
specification sources. Regional/generation names are not a support allowlist.
Older CPUs may lack required instructions; this port does not emulate those
missing features. The 32-bit Android ABI has no build path.

## Start with host checks

From the source root, with Python 3.12+, a C compiler and JDK 21:

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
```

Expected markers: `PORTABILITY_HOST_RESULT failures=0` and
`PUBLICATION_SOURCE_RESULT failures=0`. These checks use no Android device,
guest image or network. They test CPU/backend/resource policy, driver packaging
boundaries and the source/notice/link inventory, not macOS execution.

An exported checkout records exact file hashes. After intentional source edits,
refresh and review that manifest before rerunning the publication check:

```sh
python3 tools/export_android_source.py --refresh-manifest
```

## Build and experiment

The complete build helpers currently target an **Apple Silicon Mac**. A minimal
diagnostic APK and the optional graphics build are separate configurations.
See [build prerequisites and commands](docs/building.md), including optional
PanVK bootstrap steps. A basic APK does not include the macOS graphics runner.
The graphics build includes an experimental download → fresh install → first-boot
action; the complete automatic installation still needs real-device verification.

The conservative starting allocation is **2 vCPU / 2048 MiB guest RAM / 384 MiB JIT**.
The app lets users choose CPU, RAM and graphics for the next launch. Leave host
memory headroom, especially on phones. Normal guest sessions stop manually with
**Mac 종료**; they have no automatic duration limit or restart. Disk-free
diagnostics have separate limits.

With a graphics build, run **기기 호환성 검사 · 디스크 사용 안 함** before opening
a guest. It checks host CPU/JIT conditions and an authored shared-memory 4096-pixel
result. A pass is a prerequisite, not macOS or driver-stability certification.

Automatic graphics selection preserves the observed PanVK path on TB373FU. Other
models try an explicitly bundled Turnip, system Vulkan, then CPU rendering, using
the actual preflight result. Explicit selections fail without substitution, and
there is no fallback after a guest starts. A Turnip driver is not bundled by
default. CPU rendering is very slow. GPU model/Vulkan version alone does not
prove the exact shared-memory support this renderer needs.

## macOS versions and automatic installation

The graphics build has a version selector and **Download, install & start**.
This is a wired experimental flow, not a verified new-user installation claim.
Downloads are direct from Apple, checked against pinned length and SHA-256,
and kept in app-private storage. Incomplete downloads resume on retry; an
interrupted restore is preserved separately and retry creates another fresh disk.
The existing Mac stays selected until a new restore succeeds. Account setup
is completed inside macOS after first boot. English and Korean UI are included.

| Version / build | Apple download | Android evidence |
| --- | --- | --- |
| 15.6.1 / 24G90 | 15.66 GiB | Prior tablet desktop observed; new complete automatic install untested |
| 26.6.2 / 25G83 | 18.42 GiB | Experimental selection/download/install path; installation and boot untested |
| 27.2 / 26B123 | 23.84 GiB | Experimental selection/download/install path; installation and boot untested |

Catalog metadata was observed from Apple CDN on 2026-11-16. A selectable version
or available file does not establish Android compatibility. The app estimates
storage headroom before starting; larger images/restore writes can require more.
Keep the app open during installation. If Android pauses a background download,
return and retry. There is no automatic VM duration deadline. Read
[installation details](docs/building.md#download-install-and-first-boot) and
[Apple license constraints](docs/licensing.md) before use.

## External mouse and keyboard

The Mac display forwards Android-recognized USB/Bluetooth keyboard and mouse
input while focused. Click the display first. Movement, left/right/middle buttons,
dragging and horizontal/vertical wheels have handlers. Hardware movement no longer
divides travel by display zoom; a 0.5×–3× speed selector defaults to 1×. The tools button
**외부 마우스 잠금 · Esc로 해제** (English: **Capture external mouse · Esc to release**) enables relative pointer capture; Escape releases it.
Meta/Windows maps to Command, Alt to Option and Ctrl to Control. macOS chooses
the input source; Android-reserved system shortcuts may be intercepted.

Observed: authored host checks and Android framework event-dispatch tests on
TB373FU pass. **Physical USB/Bluetooth-to-macOS interaction remains untested.**
This forwards input events, not arbitrary USB devices. See
[input verification](docs/building.md#external-input-verification).

## Licenses and attribution

Henge and original Android-port changes retain **GPL-2.0-or-later**. Third-party
and per-file terms remain in effect. Read [NOTICE](NOTICE), the
[license review](docs/licensing.md) and [component inventory](android/publication/licenses.json).
The combined research APK selects the GPLv3 option while retaining upstream
grants. Version-matched Termux recipes and checksum-pinned source inputs are
recorded; complete binary/source correspondence is still a separate release
check. Do not call a combined APK GPLv2-only or treat this source preview as a
complete APK source offer.

The project license grants no rights to Apple software. The
[macOS Sequoia license](https://www.apple.com/legal/sla/docs/macOSSequoia.pdf)
includes Apple-branded-system restrictions. Owning a device does not itself
establish a macOS license for Android. No Apple software or private guest files
belong in a source contribution or release.

## Contribute and prepare a release

[Contributing](CONTRIBUTING.md) · [Security/reporting](SECURITY.md) ·
[Architecture](docs/architecture.md) · [Changelog](CHANGELOG.md) ·
[Publication procedure](docs/publication.md)

The GitHub workflow performs read-only host/source checks. It does not build or
publish an APK, access a device or certify legal compliance. The local
publication tool creates a separate Git staging tree with no commit or remote;
it does not upload anything. Review that tree before any public action.

The [final source gate](docs/publication.md#3-run-the-final-offline-source-gate)
checks actual staged Git bytes, host policies, negative boundary tests and
deterministic source archives. It is also the workflow's check command. A local
source-gate pass covers source publication preparation; the APK has separate
unresolved source and runtime release conditions.

See the [current APK source correspondence audit](docs/source-correspondence.md)
for per-file origins, exact payload/rebuild checks and remaining source gates.
