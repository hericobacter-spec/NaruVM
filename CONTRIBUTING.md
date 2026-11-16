# Contributing

This is an experimental Android source project. Start with [README.md](README.md)
and [docs/building.md](docs/building.md). Contributions should make a concrete
behavior or a reproducible check better; broader device support remains experimental.

## Before changing code

Describe the trigger, observed result and intended result. Identify the exact
model/SoC, Android version, page size and graphics backend when relevant. Use
sanitized logs. Do not upload guest images, account data, screenshots containing
private information, device serial numbers or signing material.

Keep CPU, GPU and app-lifecycle changes independently reviewable. Preserve CPU
feature/MMU checks, explicit backend choices and normal disk flush/stop behavior.
Do not turn a short fixture, a heartbeat or a microbenchmark into a stability or
macOS speed claim. New device rows must distinguish specification research from
actual execution.

## Checks

Run from the source root with Python3.12+, a C compiler and JDK21:

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
```

An exported checkout records exact source hashes. After intentional edits,
refresh that inventory and review its diff before rerunning the checks:

```sh
python3 tools/export_android_source.py --refresh-manifest
```

Native or graphics changes need the relevant [build/test scope](docs/building.md)
as well. Do not claim another physical device passed without a report from that
device. CI's source checks do not test Android, macOS, GPU drivers or endurance.

## Pull requests and rights

Explain the final behavior, why it helps, tests actually run and material limits.
Keep existing copyright/license notices. Mark modified upstream files with the
change date and update the source/patch/notice inventories when those inputs
change. New code must be yours to contribute under the applicable project/file
license; identify any third-party source and its grant. A new restrictive license
cannot replace Henge's or a dependency's existing rights.

See [docs/licensing.md](docs/licensing.md) before adding a binary dependency.
Do not add APKs or downloaded drivers to a source PR. Report security issues
according to [SECURITY.md](SECURITY.md).
