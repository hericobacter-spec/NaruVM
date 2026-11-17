# Graphics preflight repair: v0.1.2-preview

[Korean guide](graphics-verification.ko.md) · [Install and verify the APK](apk-release.md)

## Observed failures and repairs

On 2026-11-17, the published v0.1.1-preview APK reproduced two separate failures
on a disposable Android 35 ARM64 emulator using SwiftShader, 2 vCPU, 3 GiB RAM
and 4 KiB host pages. Neither test opened a macOS disk.

| Path | Observed cause | Implemented repair |
| --- | --- | --- |
| System Vulkan, worker exit 134 | SwiftShader lacked `VK_EXT_external_memory_host`; adopting the device succeeded but importing shared guest RAM aborted later. | Reims rejects devices missing the enabled extension before adoption, returning no-device/status 4. The real pixel/import test still gates eligible devices. |
| Software, worker exit 132 | The emulator reported SVE2/SME-derived CPU features without the required base features. LLVM emitted the SVE instruction `index z3.s, #0, #1` (`0x04a14003`), which the CPU could not execute. | A bundled child-process `getauxval` shim removes only inconsistent derived feature bits. It never adds features; consistent CPU reports remain unchanged. |
| Repeated software startup | Mesa tried an inaccessible `/data/data/com.termux` cache directory and disabled its disk cache. | Software workers use the app-private `mesa-shader-cache`, bounded by `MESA_SHADER_CACHE_MAX_SIZE=256M`. Preflight and guest launch share this configuration. |

The CPU shim affects only software-renderer child processes, not Android or
other apps. No shared guest-RAM copy fallback is introduced. The required Vulkan
[host-memory import extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_external_memory_host.html)
cannot be inferred from a GPU name or Vulkan version. Explicit backend selection
still fails without substitution; Auto tries its existing model-specific candidate
list before a guest starts. TB373FU Auto/PanVK policy is unchanged.

## Exact candidate evidence — observed, one emulator

The tested signed APK is `NaruVM-v0.1.2-preview-arm64-v8a.apk`, version code 3,
SHA-256 `af5a2dd38fd4d16c637eae422b43d62feaa05348ee225189186ecc13fc73b17e`.
The [artifact record](../android/publication/apk-release.json) contains the
configuration, native hashes and benchmark samples. The
[v0.1.1 record](../android/publication/apk-release-v0.1.1.json) is retained separately.

| Check | Observed result |
| --- | --- |
| Explicit system Vulkan | Clean no-device refusal, exit 1; no SIGABRT |
| Auto compatibility, five separate runs | Five passes via software; 4,096 authored pixels compared per run |
| Surface import/map/unmap and readback, five runs | 150 rendered/readback frames total, 32/64/96-pixel sizes; initial zero and row padding checked |
| CPU / MMU / shutdown | All five native suites passed |
| Android crash log during those 11 app runs | Empty |
| APK source/packaging | All 54 native files mapped: 27 unchanged pinned-package files and 27 local outputs; strip transformations reproduced |
| Independent Java/resource/manifest compilation | Exact APK payload match; NDK libc++ packaging also reproduced, not a libc++ source rebuild |

This bounded authored test verifies neither a macOS login/desktop session nor
sustained use. The physical tablet was unavailable. Physical-device GPU faults,
reboots, input responsiveness, 16K runtime pages, other GPUs, the full automatic
installer and macOS 26/27 remain unverified for this candidate.

## Targeted cache timing — observed

The candidate's exact native files and shipped CPU shim were tested in fresh
processes, rendering the same 64×64 authored EFI image with 4,096 pixel comparisons.
After one excluded warmup, five cache-disabled and five warm-cache samples were
interleaved. Host wall time includes ADB overhead.

| Mode | Median |
| --- | ---: |
| `MESA_SHADER_CACHE_DISABLE=true` | 143.69 ms |
| Warm disk cache enabled | 69.99 ms |

The observed reduction was 51.29%; the cache occupied approximately 2.3 MiB.
This measures repeated small-probe startup/render work. It is not macOS boot
speed, frame rate, mouse latency, an overall application speedup or near-native
performance. Guest launch is wired to the same cache configuration but was not
executed in this acceptance run.

## Reproduce the disk-free app checks

Use the signed APK and a separate disposable ARM64 Android emulator/device with
no valued guest data. Required ARM64 CPU instructions still apply. Use your actual
serial in place of `SERIAL`; commands below stop only this app between checks.
Do not run them while a guest or installer is active. They do not download Apple
images, install macOS, reset app data or open the guest disk.

```sh
adb -s SERIAL install NaruVM-v0.1.2-preview-arm64-v8a.apk
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_compatibility_test true --es boot_gpu auto
```

For an existing installation with this same release signer, use `install -r`.
Do not uninstall a differently signed research installation to force an update;
see the [signing and data guidance](apk-release.md#install-without-building).
Open **Logs** after completion. On the tested emulator, expect
`COMPATIBILITY_RESULT eligible=1 backend=software guest=none disk=none macos_tested=0`
and `REIMS_FRAME_RESULT failures=0`. Real hardware can select a different backend
or correctly refuse the capability check.

Repeat five times, waiting for each result. To exercise shared surfaces and the
30-round changing-size readback check in the ordinary app UID:

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_reims_test true --ez reims_software true --ez reims_surface true --ez reims_readback_stress true
```

Expect `REIMS_SURFACE_RESULT failures=0`, `READBACK_C_API_RESULT failures=0 rounds=30`
and `REIMS_RESULT failures=0`; repeat five times. To run the five CPU suites:

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_tests true
```

Expect `SUITES=5 FAILED=0`. These diagnostics have bounded worker deadlines;
normal guest sessions remain manually stopped with no automatic duration limit.
To inspect any new crash without deleting existing logs:

```sh
adb -s SERIAL logcat -b crash -d
```

For source-side policy checks, run `python3 tools/check_android_portability.py`.
The CPU-capability test covers the captured inconsistent masks, valid base features,
unrelated features and the invariant that filtering never adds a capability.
The Java helper test checks software-only environment setup and the private cache.
These host checks complement the actual app pixel tests; they do not replace them.
