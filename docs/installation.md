# Install and try NaruVM

[Korean guide](installation.ko.md) · [Project overview](../README.md) · [Developer build reference](building.md)

Audience: developers comfortable with a terminal who want to build the Android
app and try it on their own device. This guide describes the checked-in source
as of 2026-11-17. Commands are run on an Apple Silicon Mac unless stated otherwise.

## 1. Understand what is available

The [v0.1.2-preview release](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.2-preview)
provides a signed ARM64 APK and both source archives. Follow the
[APK verification/install guide](apk-release.md) to install without building,
then continue at [app/resource setup](#6-check-the-app-and-choose-resources).
The original v0.1 remains source-only. The steps below also explain local builds.
An emulator install is observed; physical/guest retesting of this candidate is unverified.

There are two installations: installing the **NaruVM Android app**, then using
its experimental installer to create a **macOS guest**. Installing the APK alone
does not install macOS. A complete run of the new automatic installer has not
been verified on a physical device. Earlier macOS 15 desktop observations came
from a separate research installation.

| Build profile | What it includes | Suitable next step |
| --- | --- | --- |
| Diagnostic | CPU/MMU tests and restore/network research tools | Check the Android host; no graphical Mac session |
| Graphics + software | Reims, Metal-to-Vulkan translation and a CPU renderer | Experimental installation/graphical boot; software rendering is very slow |
| Graphics + PanVK | Above plus the source-built private Mali driver | Reproduce the observed TB373FU path; not general Mali support |

All profiles write the same APK filename and use the same local signing key.
The **last successful build** determines which profile that file contains.

See the [APK release evidence and limits](apk-release.md).

## 2. Check prerequisites

| Item | Requirement or observed limit |
| --- | --- |
| Build computer | Apple Silicon Mac; full graphics builds on Windows, Linux and Intel Mac are unverified |
| Host tools | Python 3.12+, C compiler, Git, Make and CMake 3.22+ |
| Graphics tools | Full Xcode with the Metal compiler, plus `glslangValidator` on PATH; the recorded research environment used Xcode 27 |
| Android app | `arm64-v8a`; manifest minimum Android 9 / API 28 is a packaging floor, not tested support |
| Guest CPU | Must pass the app's fixed-ISA/JIT checks; ARM64 branding alone is insufficient |
| Guest installation memory | Installer requires Android-reported total memory of at least **4 GiB** and no low-memory condition; nominal “4 GB” devices can report less |
| Storage | Internal app storage must meet the estimate below; downloads and incomplete guest disks remain there |
| Network | Android Internet connection for Apple downloads and restore requests |

Only **one TB373FU, Android 16, 4K pages** has bounded guest observations.
Y700/Snapdragon Galaxy/other ARM64 devices, Adreno acceleration and 16K runtime
remain untested. See the [device matrix](../android/device-support.json).
The app does not require a root workflow or replace system GPU drivers. It also
does not bypass CPU, JIT, storage or Apple signing failures.

Guest images are not included. Read [license boundaries](licensing.md) before
guest installation; the project license does not grant rights to Apple software.

## 3. Get the source and build tools

Clone `main` for the current documentation and source:

```sh
git clone https://github.com/hericobacter-spec/NaruVM.git
cd NaruVM
python3 --version
cc --version
git --version
make --version
cmake --version
```

Install missing prerequisites first. Xcode Command Line Tools supply the basic
Mac compiler; graphics needs full Xcode's Metal tools as well. If Homebrew is
already your package manager, `brew install cmake glslang` supplies those two
additional command-line tools ([CMake](https://formulae.brew.sh/formula/cmake),
[glslang](https://formulae.brew.sh/formula/glslang)). Check graphics tools before the graphics profile:

```sh
xcodebuild -version
xcrun -sdk macosx --find metal
glslangValidator --version
```

Bootstrap the pinned SDK, NDK, build tools and ARM64 Mac JDK, then restore inputs:

```sh
python3 tools/bootstrap_android.py
export JAVA_HOME="$PWD/.tools/jdk-21/Contents/Home"
export ANDROID_SDK_ROOT="$PWD/.tools/android-sdk"
python3 android/fetch_restore_deps.py
```

Downloads, sources and build outputs can use many GB on the Mac; no measured
peak-space or build-time guarantee is available. The scripts verify pinned
inputs rather than selecting current versions. Existing mismatched checkouts
are preserved and refused. `JAVA_HOME` and `ANDROID_SDK_ROOT` above select this
checkout's tools; reuse them in a new terminal. Android Studio/Gradle are not
used by this build path.

Optionally run the device-free host checks now. They need the JDK above and a
C compiler, but do not install an app or boot a guest:

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
```

Expected markers: `PORTABILITY_HOST_RESULT failures=0` and
`PUBLICATION_SOURCE_RESULT failures=0`.

## 4. Build one profile

### A. Diagnostic app

```sh
python3 android/build.py
```

Use this for **Run CPU tests**. The graphics compatibility/installer controls
are disabled because this profile does not contain the graphical Mac runner.
To install/boot a graphical guest, use B or C instead.

### B. Graphics with software rendering

Keep the same environment from step 3. Set these paths **before both fetch and
build**, and keep them for subsequent graphics builds:

```sh
export HENGE_REIMS_SOURCE="$PWD/.tools/reims-android"
export HENGE_REIMS_TARGET="$PWD/android/build/reims-target"
export CARGO_BUILD_JOBS=4
python3 android/fetch_gpu_deps.py --reims
python3 android/build.py --reims --software
```

This downloads pinned Reims/metal2vulkan source, prepares local Rust tools and
bundles CPU Vulkan rendering. It does not bundle PanVK or Turnip. In the app,
choose **CPU rendering** for this profile if an automatic candidate is missing
or fails. In particular, **TB373FU Auto requires PanVK and does not fall back**;
use CPU rendering explicitly here or build profile C. A software-renderer pass
does not establish acceptable speed.

### C. TB373FU PanVK research profile

Complete the setup/fetch steps for B. Install/start Docker separately with an
existing Linux/arm64 context. Inspect its name, then replace `YOUR_CONTEXT`:

```sh
docker context ls
python3 android/build_panvk_source.py --bootstrap --docker-context YOUR_CONTEXT --jobs 2
python3 android/build.py --reims --software --panvk-userptr-probe
```

The first command after `docker context ls` builds the pinned private driver;
the last packages it. The historical `--panvk-userptr-probe` flag name remains,
but the packaged ICD also serves this guest backend, not only diagnostics.
In NaruVM use **Auto** on TB373FU or explicitly select
**Mali PanVK**. Other Mali models are untested. The helper does not start Docker,
alter a system driver or flash firmware. It leaves a project container for
incremental builds; use the generated report and [build reference](building.md#panvk-and-optional-turnip)
to identify it. Missing pinned packages must be investigated rather than replaced
silently. Adreno users need their own validated [Turnip bundle](../android/turnip_bundle.py)
for that optional backend; Snapdragon/Y700 names alone do not establish support.

## 5. Install the Android app over USB

Every successful profile produces:

```text
android/artifacts/NaruVM-arm64-debug.apk
```

It is a debuggable, locally signed app with package ID `dev.henge.research`.
The build verifies APK signatures and 16K ZIP alignment. Preserve the local
`.tools/research-debug.keystore` privately for future updates; do not publish it.
The retained package ID and internal `henge` filenames preserve project history
and compatibility; the display name is NaruVM.

On the Android device enable Developer options and USB debugging, connect a data
cable, unlock it and accept that computer's debugging prompt. Follow
[Android's device setup](https://developer.android.com/studio/run/device) if the
prompt or device is missing. On the Mac, from the repository root:

```sh
export ADB="$ANDROID_SDK_ROOT/platform-tools/adb"
"$ADB" devices -l
```

Choose the exact serial from that list; replace `DEVICE_SERIAL` below. Its state
must be `device`, not `unauthorized` or `offline`:

```sh
export NARU_DEVICE='DEVICE_SERIAL'
"$ADB" -s "$NARU_DEVICE" install -r android/artifacts/NaruVM-arm64-debug.apk
"$ADB" -s "$NARU_DEVICE" shell am start -n dev.henge.research/.MainActivity
```

Expected: ADB prints `Success`, then NaruVM opens. `-s` selects one device and
`-r` updates while retaining data **only when Android accepts the signature**;
see the [official ADB reference](https://developer.android.com/tools/adb).
On `INSTALL_FAILED_UPDATE_INCOMPATIBLE`, stop and recover the original local
signing key. **Do not uninstall, clear storage or overwrite guest files to fix
the mismatch.** App-private guest data is not included in the source release.

## 6. Check the app and choose resources

The home has **Launch**, **Diagnostics/Tests**, and **Logs** navigation; labels
vary slightly with screen width. The interface follows the device language and
includes English and Korean. On Android versions with per-app languages, those
system settings can select either language.

1. In Launch choose resources and the backend appropriate to your build profile
   for the **next graphical boot**. In particular, profile B on TB373FU must use
   CPU rendering before running the compatibility check:

   | Setting | Starting value | Available control |
   | --- | --- | --- |
   | Guest CPU | 2 vCPU | 1 / 2 / 3 / 4 |
   | Guest RAM | 2048 MiB | 2048 / 3072 / 4096 MiB |
   | Graphics | Auto with a matching bundled backend; CPU rendering for profile B on TB373FU | Auto / CPU / system Vulkan / Mali PanVK / Adreno Turnip |
   | External mouse gain | 1× | 0.5×–3× |
   | JIT cache | 384 MiB | Current runtime default; not a home-screen allocation control |

2. Keep Android memory headroom. Larger CPU/RAM allocations are not a measured
   speed improvement. Restore itself currently uses **1 vCPU / 2 GiB**; the saved
   settings apply to graphical boot after installation.
3. With a graphics build, run **Check device compatibility** in Diagnostics.
   This checks CPU/JIT/page conditions and an authored 4096-pixel shared-memory
   result without opening a guest disk. Record the selected backend/result.

Auto tries optional bundled Turnip, system Vulkan, then software on other models.
TB373FU Auto retains only its observed PanVK candidate. Explicit selections fail
without substitution; the renderer does not switch after guest startup. A failed
preflight should be addressed before a guest experiment; a pass is not endurance
or full macOS compatibility certification.

## 7. Download and install a fresh macOS guest

In **Launch**, choose **macOS version** and **Download, install & start**.
If a guest already exists, expand **Install another Mac**. No external ROM/IPSW
import is required by this flow; advanced firmware/restore buttons are separate
research tools and are not prerequisites.

The following are **checked-in catalog values**, not promises of current Apple
signing availability or Android support. Clean-install space is calculated as
remaining image download + configured restore headroom + 2 GiB:

| Catalog version | Image download | Free app storage on an empty cache, approximately | Device evidence |
| --- | ---: | ---: | --- |
| 15.6.1 / 24G90 | 15.66 GiB | 49.7 GiB | Earlier tablet desktop observed; new automatic install unverified |
| 26.6.2 / 25G83 | 18.41 GiB | 60.4 GiB | Installation and boot untested |
| 27.2 / 26B123 | 23.84 GiB | 73.8 GiB | Installation and boot untested |

Use the value shown by the app: cached verified downloads/partial data affect
it, and real restore writes can exceed estimates. “64 GB device” is total
capacity, not free app storage. An external SD card does not automatically add
space to these internal paths. Failed restore disks also consume space; retries
create separate disks, so repeated retries can require more free space.

Expected phases, as implemented:

1. Prepare and verify the boot ROM from Apple.
2. Download the selected Apple restore image; `.part` downloads can resume.
3. Verify exact length and SHA-256 before restoration.
4. Create a new `machines/BUILD-UUID/` identity and sparse 64 GiB virtual disk.
   Logical disk size is distinct from currently allocated host storage.
5. Restore into that new disk, finish writes and record success. An existing
   guest stays separate and selected until successful completion.
6. Automatically start the new guest when the app is foreground; otherwise
   return to the app. Complete language, region and local-account setup in macOS.

Keep the app visible and the device powered adequately. There is no published
installation-time estimate and **no normal VM/automatic-installer duration
deadline**. Do not infer success from a download reaching 100%; restore and
first boot are separate phases. Apple signing/network/virtual-device failures
can stop the flow even with a verified image.

**Stop installation** requests cancellation and completion of pending writes.
Download retry can resume saved partial data; the full image is verified before
restoration. An interrupted restore is
preserved as incomplete and retry uses another fresh disk. No automatic retry
loop runs. The app does not currently offer a general guest import/export or
incomplete-machine cleanup manager. Do not use ad-hoc deletion of private files
as a troubleshooting step.

## 8. First boot and normal use

- Finish Setup Assistant in the guest. A local Mac account is separate from an
  Apple ID; use your own account choices. No account or password is provided by
  this repository.
- The virtual network is a **wired NIC with Android-side NAT/DNS**, not Wi-Fi
  radio passthrough. An empty macOS Wi-Fi list does not by itself mean the host
  is offline. After reaching the desktop, test a web page to check guest networking.
- Click/tap the Mac display to give it focus. Direct touch and trackpad modes
  are available; the latter moves the pointer by swiping. The tools include Tab,
  Enter and **Switch input source (⌃Space)**. Select ABC/another Latin input
  source inside macOS when entering Latin text; Android's IME alone does not
  change the guest input source.
- Connect USB/Bluetooth input devices to Android normally. Use **Lock external
  mouse · Esc to release** for relative movement. Try 1× first, then adjust mouse
  gain if travel is too small. Meta/Windows maps to Command, Alt to Option and
  Ctrl to Control. Android can intercept system shortcuts. Physical HID-to-guest
  acceptance remains untested; these are implemented handlers, not a peripheral
  compatibility promise or general USB passthrough.
- End a session with **Stop Mac**, allowing disk writes to finish before closing
  the app. Later choose **Start Mac** to use the selected installed disk. There
  is no normal automatic restart/deadline.

Slow interaction and prior GPU faults/tablet reboots remain open. Choose a small
test workload and change one resource/backend setting at a time. Source checks
and a desktop observation do not establish practical daily use or native speed.

## Troubleshooting

| Symptom | Check / next action |
| --- | --- |
| No APK in Releases | Expected for v0.1; build A, B or C above. Source archives are not installable APKs. |
| `Missing ... bootstrap_android.py` | Run step 3; check this shell's `JAVA_HOME`/`ANDROID_SDK_ROOT` and the pinned tool files. |
| `metal` or `glslangValidator` missing | Install/select full Xcode with Metal tools and provide glslang before graphics builds. Command Line Tools alone are insufficient. |
| Source differs from pin / checksum mismatch | Keep the conflicting checkout/cache and inspect the exact error. Do not skip verification or silently change a pin. |
| ADB shows no device / `unauthorized` | Use a data cable, unlock Android, enable USB debugging and accept the computer; consult the linked Android setup guide. |
| Signature mismatch on update | Recover `.tools/research-debug.keystore` used for the installed app. Do not uninstall or clear its data. |
| Installer/compatibility button disabled | Confirm the APK was built with `--reims`, and no VM/install/diagnostic is active. Basic profile A has no graphics runner. |
| CPU feature or JIT refusal | Save the exact reason. This Android branch does not emulate missing guest ISA features. |
| TB373FU Auto says PanVK is missing | Use profile C, or explicitly choose CPU rendering with profile B; Auto does not silently switch on this model. |
| Vulkan/shared-memory pixel preflight fails | Device GPU/Vulkan labels are insufficient. Record the actual driver/backend result; CPU rendering is a separate slow experiment. |
| Low-memory / insufficient-space refusal | Check reported RAM and free internal storage against step 7. End other workloads; do not erase an existing guest to bypass the gate. |
| Background download paused | Return to NaruVM and retry; keep it visible. Android can pause background dataSync work. |
| Setup screen goes black / app exits | Record the last phase, backend, duration and whether Android itself rebooted. Stop repeated guest retries and collect logs; no generic fix is established. |
| No Wi-Fi list / Latin typing wrong | Check the virtual wired network and macOS input source as described in step 8. |

## Collect a useful report

Use **Logs → Save session log** for VM/diagnostic output. Review it for personal
information before attaching it to an issue. For a stopped/completed installer,
its separate log can be copied from this debuggable app using the same ADB/device
variables as step 5:

```sh
"$ADB" -s "$NARU_DEVICE" exec-out run-as dev.henge.research cat files/last-install.txt > naruvm-install.txt
```

That file may not exist before an installer run finishes; the session-log button
does not export the installer's separate log. Include app commit/build profile,
exact model/SoC, Android version, host page size, CPU/RAM/backend, guest version,
last phase and elapsed time. Distinguish a guest exit, app exit and whole-device
reboot. Do not post serial numbers, account/password data, signing keys, guest
disks or Apple images. Use the [issue forms](https://github.com/hericobacter-spec/NaruVM/issues/new/choose)
and [contribution guide](../CONTRIBUTING.md).

## Verification boundary

Observed evidence remains the bounded 2026-11-16 tablet research and prepared
build checks described in [building.md](building.md). This documentation update
does not add a physical device test, full automatic installation, physical HID
acceptance, fresh-machine reproduction, endurance result or APK release.
