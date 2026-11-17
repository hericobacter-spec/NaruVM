# Input and frame update: v0.1.3-preview

[Korean guide](response-verification.ko.md) · [Download / signing / source](apk-release.md)

## Included changes and boundaries

The signed version-code **4** APK includes the bounded input queue, explicit
all-up overflow recovery, worker-side FramePump and NEON BGRA conversion from the
[input/frame source change](https://github.com/hericobacter-spec/NaruVM/commit/c90b946d6bac0341bdfc6381774e23a4dc12f241).
Adjacent relative motion is combined while preserving net travel and event
boundaries. Overflow deliberately discards stale input and releases held state.
Stop bypasses queued input; it cannot preempt a pipe write already blocked in the
kernel. One pending UI handoff bounds frame work; final Bitmap copying and GPU
readback still exist. This is not zero-copy or a near-native speed claim.

## Exact candidate acceptance — observed, 2026-11-17

APK: `NaruVM-v0.1.3-preview-arm64-v8a.apk`, **85,778,737 bytes**. SHA-256:

```text
004572445072ca760d2ac65f6d8f26123809b606249c3be15c210dda11b4f91c
```

The [artifact record](../android/publication/apk-release.json) records the profile,
certificate and native hashes. One dedicated Android35 ARM64 emulator used
SwiftShader, 2 vCPU, 2048 MiB, 1280×800 and 4K runtime pages. These are authored
fixtures without an Apple image or guest disk:

| Check | Observed result |
| --- | --- |
| Signature / packaging | v3 signature, same release certificate, 16K ZIP alignment; non-debuggable, backup disabled |
| Public v0.1.2 → v0.1.3 update | `install -r`, same app UID and authored private-data sentinel retained; no uninstall/reset |
| Auto graphics | Five passes; software selected; 4096 authored EFI pixels per probe |
| Explicit System Vulkan | Clean capability refusal, eligible=0/status4; no abort |
| Shared surfaces/readback | Five runs, 30 changing-size frames each, failures=0 |
| Native CPU/MMU/shutdown | Five suites passed |
| Authored display | 150 sequences, 143 displayed, six modes, width up to1280; failures=0 |
| Touch/tools | Long stationary touch, short tap, trackpad fallback, toolbar click and normal stop passed; native held intervals118/119ms |
| Service lifecycle | Two Activity attachments kept one worker; normal stop released foreground service and wake lock |
| Crash buffer | Zero bytes after acceptance |
| Source/payload | 54 native members mapped (27 pinned package bytes /27 local outputs); strip transforms repeated; classes.dex, resources and release Manifest recompiled exactly |

The release app and workers ran as the ordinary app UID. Because this APK is
non-debuggable, private report collection used root access **only on the
disposable emulator**; it did not run the workers as root or enable app debugging.
The public Python device harnesses use `run-as` and therefore require a separately
built debug APK. A signed-release recipient can use the app's **Logs** view.
The touch harness now reads actual display bounds and supports English/Korean
buttons instead of assuming one old tablet resolution and language.

Both source archives accompany the APK. Dependency input verification passed
94 source inputs and four original Git snapshots. Native mapping does not attest
the historical producer commit of external packages or a complete independent
Termux/LLVM source rebuild; [the source boundaries](apk-release.md#source-provision-versus-rebuild-attestations)
remain in force. The earlier cache timing belongs to [v0.1.2](graphics-verification.md),
not a new performance measurement of this APK.

## Repeat without a guest

On a separate disposable ARM64 Android target with no guest/installer running,
follow [APK verification and installation](apk-release.md), then run these one
at a time, replacing `SERIAL` with the selected target. Each command starts only
an authored diagnostic. Wait for completion and open **Logs** for results.

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_compatibility_test true --es boot_gpu auto
```

On the tested emulator, expect `COMPATIBILITY_RESULT eligible=1 backend=software`
and `REIMS_FRAME_RESULT failures=0`. Other hardware may choose another eligible
backend or refuse. Repeat graphics/readback/CPU checks using the
[complete disk-free recipe](graphics-verification.md#reproduce-the-disk-free-app-checks)
with this version's APK. For the large-frame fixture:

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_frame_test true
```

Expect `FRAME_TRANSPORT_BITMAP_RESULT failures=0` and `max_width=1280`.
For host regression checks, run `python3 android/check_response.py` with a JDK
available. Diagnostic deadlines do not impose a timeout on ordinary guest use.

## Unknown / unverified

No physical tablet or macOS guest was available for this candidate. Historical
TB373FU screenshots are not a retest. Actual guest speed, sustained stability,
physical USB/Bluetooth input, historical tablet reboot causes, 16K runtime pages,
other devices/GPUs, a complete new automatic install and macOS26/27 remain
unverified. Authored emulator passes do not establish everyday usability.
