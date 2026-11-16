# Android architecture

This is an Android host adaptation of Henge at the revision recorded in
[the source-input inventory](../android/publication/dependencies.json).
The original design is documented in
[upstream Henge](https://github.com/Leviidev/Henge/blob/e98920ce3b108d17f98acaff87f74e5a477ec4b2/docs/architecture.md).
It uses same-ISA translation, not an Android hardware hypervisor or QEMU engine.

```text
Android Views / VmService / serialized input
                 |
NDK/Bionic host + separate RW/RX JIT mappings
                 |
Henge ARM64 translator + software MMU + virtual devices
                 |
Reims virtual GPU -> metal2vulkan -> selected Vulkan driver
```

## CPU and memory

`core/` retains guest state, instruction translation, software TLB/MMU and
exception/device exits. Most compatible ARM64 instructions can be copied;
memory, branches, reserved registers and system operations require translation.
`host/platform.h`, `host/jit_region.c` and `host/enter.S` adapt clocks, waits,
memory aliases, cache synchronization and the Android platform register.

The guest advertises a fixed M1-era instruction set. Before boot/restore,
`android/native/platform_compat.c` checks host HWCAP/HWCAP2 requirements, 4K/16K
page conditions and a small JIT mapping. It refuses missing features instead of
emulating them. The 16K policy/build contract has no physical runtime evidence.
Affinity considers the caller's allowed CPUs and exposed capacity; it changes
neither clock settings nor thermal/security policies.

## Graphics and sessions

`android/native/reims_gpu.c` connects guest virtual-GPU devices, shared guest RAM,
IRQ and frame transport to Reims. Reims and metal2vulkan are pinned external
projects with recorded Android patches. Their exact shared-memory requirement
is stronger than a Vulkan version check.

`GpuConfig` defines candidate order; `VmService` runs disk-free CPU and actual
shared-memory pixel preflight before opening a guest. New models can fall back
to CPU rendering during that preflight. Explicit choices are not substituted,
and there is no renderer switch after guest startup. The observed TB373FU
default remains its specific PanVK path. An app-private optional Turnip adapter
does not install a system driver or prove Adreno compatibility.

The foreground service owns a normal VM process, serialized controls and wake
lock. Activity recreation does not own the VM lifetime. Normal sessions have no
automatic deadline/restart; the toolbar requests normal stop and disk flush.
Authored diagnostics and restore experiments have their own bounds. Frames are
shared with the native Android view; touch/keyboard events pass through the
serialized service writer.

## Source and test layout

- `core/`, `devices/`, `host/`: Henge engine, virtual board and host adapters.
- `android/native/`, `android/src/`: NDK entry points, diagnostics and Android UI.
- `android/gpu/`, `android/reims/`, `android/panvk/`: authored tests, source pins
  and external-project patches.
- `third_party/`: vendored restore/crypto/library sources and original notices.
- `tests/`, `android/tests/`: authored CPU/MMU/resource/backend/packaging checks.
- `tools/`: toolchain bootstrap, host/source checks and isolated publication staging.

Implemented paths are distinct from observed device behavior. Read
[the device matrix](../android/device-support.json), [build scopes](building.md)
and [license boundaries](licensing.md) before extending capability claims.
