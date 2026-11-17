# Build and verification scopes

For the complete source → APK → USB install → guest setup sequence, start with
[the installation guide](installation.md) / [Korean installation guide](installation.ko.md).
This page is the developer reference for build variants and verification scopes.

Audience: developers who can use a shell, Python, C and Git. English and Korean
READMEs describe the same experimental source preview. This guide distinguishes
host-only checks, a diagnostic APK, graphics builds and guest provisioning.

Observed build environment, 2026-11-16: Apple Silicon macOS, Python 3.14.6, CMake,
JDK 21 and the pinned Android SDK/NDK. An extracted source tree built a basic APK
using prepared tool and dependency caches. A second isolated source/output tree
also rebuilt the Reims/metal2vulkan and software paths from fresh pinned source,
and PanVK with newly built host compilers in a newly created Docker container.
Prepared pinned SDK/NDK/JDK/Rust tools and binary download caches were reused;
this is not a wholly fresh-machine or bit-identical-build test. Linux CI runs host checks only. Windows,
Intel Mac and Linux whole-APK builds have not been verified.

## Host checks: no device or downloads

Install Python 3.12+, a C compiler and JDK 21 through your normal package manager.
Set `JAVA_HOME` to that JDK if it is not already on PATH. From the source root:

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
python3 android/tests/test_publication.py
python3 android/tests/test_publication_sources.py
```

Expected final markers are `PORTABILITY_HOST_RESULT failures=0`,
`PUBLICATION_SOURCE_RESULT failures=0` and `OK` from the publication-boundary
tests. A missing compiler/JDK is a prerequisite failure. These checks exercise
authored CPU/resource/backend policy, source boundaries and driver-manifest
packaging; they do not boot macOS or prove actual driver compatibility.

## Basic diagnostic APK: Apple Silicon Mac

Prerequisites: command-line developer tools (`cc`, `git`, `make`), Python 3.12+,
CMake 3.22+ on PATH, network access and space for several GB of tools. Downloads
are pinned in [the toolchain lock](../tools/android-toolchain.lock.json) and
[dependency inputs](../android/publication/dependencies.json). Review the
applicable SDK and dependency licenses before use.

```sh
python3 tools/bootstrap_android.py
python3 android/fetch_restore_deps.py
python3 android/build.py
```

Output: `android/artifacts/NaruVM-arm64-debug.apk`. This is a locally signed
research APK for `arm64-v8a`, package `dev.henge.research`, manifest minimum API 28.
That manifest minimum is not a tested OS/device compatibility guarantee.
The helper creates a local debug key under ignored `.tools/`; it is not a release
identity. Do not upload that key. Network/software dependencies contain pinned
binary inputs; see the separate binary distribution gaps below.

To install on your own explicitly selected test device, enable USB debugging
and use the SDK's ADB (or an installed `adb`):

```sh
adb install -r android/artifacts/NaruVM-arm64-debug.apk
```

Launch NaruVM and use its disk-free CPU diagnostics. The basic APK lacks the
Reims graphics runner, so it cannot run the macOS graphical session. Installing
over an existing local app requires the same signing key; do not uninstall or
erase an existing research installation merely to resolve a signature mismatch.
Nothing in these scripts grants root access or changes system GPU drivers.

## Optional graphics and software renderer

In addition to the basic prerequisites, install full Xcode with its Metal tools
and `glslangValidator` on PATH. The bootstrap installs its pinned Rust toolchain
locally and fetches pinned upstream source. It refuses unexpected revisions or
changes rather than overwriting them.

```sh
python3 android/fetch_gpu_deps.py --reims
python3 android/build.py --reims --software
```

This bundles the experimental Reims path and CPU Vulkan renderer. On TB373FU,
explicitly select CPU rendering unless you also build the PanVK profile below:
Auto on that model requires PanVK and intentionally does not fall back. It requires
external downloads and, by default, a Reims worktree under `~/.cache/henge-reims-android`.
To keep source and Rust output inside a separate checkout, set these before
both the fetch and build commands (same values for each):

```sh
export HENGE_REIMS_SOURCE="$PWD/.tools/reims-android"
export HENGE_REIMS_TARGET="$PWD/android/build/reims-target"
export CARGO_BUILD_JOBS=4
```
Read [Reims inputs](../android/reims/dependencies.json),
[metal2vulkan inputs](../android/gpu/dependencies.json) and the retained patches.
The full sequence has not been reproduced on an empty machine. CPU rendering
is a compatibility experiment, not a usable-speed claim.

## PanVK and optional Turnip

The TB373FU research driver uses [pinned PanVK inputs](../android/panvk/dependencies.json)
and [the host-pointer patch](../android/panvk/userptr-driver.patch). On an Apple
Silicon Mac with Docker and an already running Linux/arm64 context, run:

```sh
python3 android/build_panvk_source.py --bootstrap --docker-context YOUR_CONTEXT --jobs 2
python3 android/build.py --reims --software --panvk-userptr-probe
```

Replace `YOUR_CONTEXT` with the existing context shown by `docker context ls`.
The helper creates a project-named container, fetches the pinned source, verifies
and applies the patch, prepares the Mac Python tools with hash-locked wheels,
builds the Linux generators, then cross-compiles the Android driver. It refuses
an unexpected source patch or a container belonging to another build. It does
not start/configure Docker, change system drivers, install the APK, or start a VM.
An existing manually prepared checkout can omit `--bootstrap`; that expert path
defaults to container `henge-panvk-build`. Pass `--host-container` to select another.

The [host-tools lock](../android/panvk/host-tools.lock.json) pins the base image
digest, 24 top-level apt package versions, source and patch; the
[Dockerfile](../android/panvk/linux-build.Dockerfile) and hash-locked Python files
are included. Transitive apt versions are recorded in
`android/build/panvk-host-environment.json`, not independently pinned. The
[sanitized observed environment](../android/publication/panvk-host-observed.json)
retains installed package versions from the 2026-11-16 bootstrap. Repository
retention can affect future installation of these exact versions; the helper
fails rather than silently selecting newer versions. Save the environment report
and external subproject sources when assembling a release-specific source offer.
The named project container remains running for incremental compiler invocations;
after finishing you can stop that exact container from the recorded report.
No compiled PanVK driver is in the source preview. A successful build is not
evidence of a corrected tablet reboot or driver stability.

An optional Turnip package requires the exact source pin, binary SHA256,
license and build recipe accepted by
[the bundle validator](../android/turnip_bundle.py). No Turnip binary is bundled
or downloaded automatically. Capability checks on an actual Adreno device are
still required; a Snapdragon model name does not establish support.

## Download, install and first boot

No Apple ROM, IPSW, guest disk or prepared account is bundled. Read the
[license review](licensing.md) before guest work. Device ownership by itself
does not establish permission under Apple's macOS license.

Build a graphics configuration above, install it with your local key and open
NaruVM. On an empty installation, choose a **macOS version** and press
**Download, install & start**. With an existing
Mac, expand **Install another Mac**. Installation uses a fresh
app-private `machines/BUILD-UUID/` directory and a separate machine identity;
it does not restore over the existing guest. Use **Stop installation** to stop.
Downloads can resume from `.part`; an interrupted restore remains a separate
incomplete disk, and retry creates a new disk. There is no automatic retry loop.

The pinned catalog in `android/src/dev/henge/research/MacRelease.java` includes
15.6.1 (24G90), 26.6.2 (25G83), and 27.2 (26B123). Direct Apple CDN size/digest
metadata was checked on 2026-11-16. **26 and 27 are Android untested**: neither
installation nor boot is established. Prior macOS 15.6.1 desktop runs do not verify
this new complete installation flow. The 35 host HTTP/cache/integrity fixture checks,
11 native disk-guard scenarios and an actual small Apple ROM decode passed;
no full restore-image download or automatic guest install was used in those checks.

Keep the app visible during installation. A foreground service owns the download
and restore across ordinary screen recreation. Android can pause a background
`dataSync` service; return to the app and retry to resume the saved download.
The installer checks estimated remaining download + restore headroom + 2 GiB,
and uses 2 GiB guest RAM for restoration. Estimates are not measured peak usage.
After successful restore exit/status and disk flush, the app selects the new
machine. If the app is foreground, it automatically starts the first graphical
boot with the saved CPU/RAM/GPU settings; otherwise return to the app to start (or use its notification when
notification permission is granted). Finish setup/account choices in macOS.

Run **Check device compatibility** before experiments.
A basic diagnostic APK lacks the graphics runner and disables installation.
Start with 2 vCPU / 2048 MiB guest RAM / 384 MiB JIT. Normal VM and UI restore sessions
have no duration deadline; disk-free diagnostics retain separate bounds. Apple
signing availability, networking, virtual-device compatibility and storage errors
can still prevent installation. The app does not bypass them.

The flow follows upstream Henge's `Installer.swift`, `BootROM.swift` and
`RangedDownload.swift` structure, adapted to Android foreground services. CPU
instruction/JIT/page conditions are capability-gated; GPU selection follows
actual disk-free pixel checks. CPU/RAM controls apply to the first graphical
boot. Restoration currently uses 1 vCPU / 2 GiB; it does not modify the Apple image
for each SoC. No all-device or macOS 26/27 compatibility follows from this wiring.

## External input verification

The Mac display must own focus. Use the tools button **Lock external mouse · Esc to release**
for captured relative mouse movement; press Escape to release capture. Focus loss
releases held guest keys/buttons. Android may intercept system shortcuts before
they reach the app. Meta/Windows = Command, Alt = Option, Ctrl = Control; input
source selection belongs to macOS. The source implements common key/mouse input,
not general USB passthrough, extra gaming buttons or all keyboard layouts.

Host input-state checks run as part of `tools/check_android_portability.py`.
To test real Android View dispatch with **authored** events, first build the target
APK above and install it using your local research key. Only test while the VM
is off: instrumentation restarts the target app process. Use an authorized test
device and replace `DEVICE_SERIAL` with its `adb devices` identifier.

```sh
python3 android/build_input_test.py
adb -s DEVICE_SERIAL install -t -r android/artifacts/NaruVM-input-test.apk
adb -s DEVICE_SERIAL shell am instrument -w dev.henge.research.inputtests/dev.henge.research.HardwareInputInstrumentation
```

The target and test APK must have the same signing key. Do not uninstall an
existing app or erase its data to resolve a signature mismatch. Expected result:
`HARDWARE_INPUT_ANDROID checks=30 failures=0 source=authored no_vm=1 physical_hid=0`
and `INSTRUMENTATION_CODE: -1`. This does not use an actual mouse/keyboard or guest.
Physical USB/Bluetooth devices, pointer-capture acceptance and macOS end-to-end
behavior require separate device testing; they are not proven by this marker.

The **Licenses and source** button in Logs opens offline packaged notices,
license texts, combination selection and source/replacement instructions.
A public source-download destination is not configured in this local build.

## Distribution is a separate gate

Successful local builds do not establish permission to redistribute their APKs.
The [license inventory](../android/publication/licenses.json) and
[external-input inventory](../android/publication/dependencies.json) record
remaining matching-source, recipe and license-choice work. Follow the
[publication procedure](publication.md) for source-only preparation. No binary
release is cleared by these commands.


## Authored installation-state UI check

The same companion APK also provides a network-free, VM-free installation UI
fixture. Run only while both services are stopped and no first boot is pending:

```sh
adb -s DEVICE_SERIAL shell am instrument -w dev.henge.research.inputtests/dev.henge.research.InstallerUiInstrumentation
```

It injects an explicitly authored working state to check visible Stop controls
outside scrolling/disclosure regions, navigation and cancellation dispatch. It
does not verify an actual foreground download, restore or automatic first boot.

## Explicit release signing and recipient modifications

The default commands above build a research APK. For the exact published
v0.1.1-preview configuration, private signing inputs, path remapping and
recipient re-signing instructions, follow [the APK guide](apk-release.md).
The published profile is `--gpu --reims --software --panvk-userptr-probe`.
The publisher key is neither required to rebuild nor included in source assets.
