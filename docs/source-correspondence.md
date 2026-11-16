# Current APK source correspondence

Technical observations on 2026-11-16, one local NaruVM v0.1 research APK. Audience: contributors
with Python 3.12+, Git/C compiler, JDK21 and the prepared Apple Silicon Android
build environment described in [building](building.md). This report audits source
and binary identity; it does not approve binary distribution or macOS use.

APK SHA256: `55d78d6f8077bce71dba7e6ad5add2d870b9b4632905b110bdddf5ecf345ee37`.
The [per-file report](../android/publication/source-correspondence.json) records
all 53 native members, hashes, package origins and build recipes.

| Observed check | Result and limit |
| --- | --- |
| Native payload origin | 53/53 mapped: 27 exact raw-deb files and 26 exact local outputs |
| Used external packages | 20 distinct versions; 29 lock entries include duplicate and unshipped inputs |
| Project/recipe inputs | 927 project source/build files and 147 recipe files match recorded hashes |
| CMake native rebuild | All 18 CMake targets clean-rebuilt and byte-identical; same paths and prepared tools |
| Java, manifest and resources | Independently compiled `classes.dex`, binary Android manifest and packaged resources byte-identical |
| NDK libc++ packaging | Official pinned NDK runtime + source-built cleaner 3.0.1/API24 + NDK strip exactly reproduce the entire APK library |
| Graphics and curl | Metal/Reims source pins/patch/file hashes and PanVK pin/patch/output checked; stripped source outputs match; 4515 curl source files match its archive |

The decoder is pinned Apple LZFSE source under BSD-3-Clause, verified by
`android/installer_sources.py` and built as `libnaruvm_rom.so`. A real small
Apple ROM-range decode matched its expected final SHA-256; this does not
verify a full automatic installation or 26/27 boot.

The final APK's Reims builder used its verified default cache worktree, rather
than the earlier isolated fresh worktree. Those sources were checked against the
same pin/patch. This audit does not repeat a complete independent Rust/PanVK build.
The libc++ result reproduces **packaging**, not libc++ compilation from source.
The unrelated older NDK prebuilt release-tree download is excluded from source
counts and is not used as evidence for the current runtime.

## Repeat the offline checks

The GitHub source preview does not contain APKs, build outputs, package caches or
toolchains. Supply your own research APK and matching prepared build tree. Do not
download or run an APK merely because this document names its hash. The commands
below install nothing and never start a driver, device or guest. Source collection
is a separate [procedure](publication.md#selected-graphics-and-restore-sources).

Replace `/path/to/...` arguments with actual paths. The prepared build tree is
the source root containing `android/build/native/bin` and its CMake link records;
the package cache root contains `android-net/debs` and `android-lavapipe/debs`.
The source checkout passed to `--source-tree` must have `SOURCE-MANIFEST.json`,
as produced by the source-preview export/staging procedure. Use new output paths;
existing reports are preserved. A missing cache/build input is an unavailable
check, not evidence of correspondence.

```sh
python3 android/tests/test_source_correspondence.py
python3 android/audit_source_correspondence.py --apk /path/to/research.apk --build-tree /path/to/prepared-build --source-tree . --cache /path/to/package-cache --inputs-cache /path/to/publication-sources --output android/artifacts/origin-audit.json
python3 android/reproduce_source_payloads.py --apk /path/to/research.apk --source-tree . --sdk /path/to/android-sdk --jdk /path/to/jdk-home --inputs-cache /path/to/publication-sources --libcxx --output android/artifacts/payload-reproduction.json
```

The six negative boundary tests reject changed/unknown/duplicate native payloads
and malformed packages. Expected audit marker for the recorded APK:
`APK_ORIGIN_AUDIT_RESULT failures=0 native=53 exact_deb=27 local_outputs=26 complete_source_correspondence=0`.
Payload reproduction reports `failures=0` only when all requested exact comparisons
pass. Compiling the packaging tool requires CMake and a C++20 host compiler; its
checksum-pinned archive must be collected first. Only temporary copies of the
NDK library are changed. No package recipe or downloaded shell script is run.

Adding `--require-complete-source` to the origin audit deliberately exits nonzero
for this report's unresolved source gates, even though all origin matches pass.
An exit-zero origin check must not be presented as complete source correspondence.

## Remaining source gates

The inspected deb controls do not attest the binary-producing recipe commits
for the 20 used package versions. Version-matched recipes/source archives are
preserved candidates; raw-deb equality does not prove those recipes built them.
To establish a new distribution's source-to-build chain, rebuild required package
libraries from retained source/patch/configuration, record the exact producing
inputs and verify that new APK; do not invent an attestation for the old binaries.

The NDK libc++ source build and its complete build/runtime inputs still require
reproduction. Release-specific modified-library replacement/relink/install
acceptance remains untested. Therefore `complete_source_correspondence=false`
and `binary_release_ready=false` remain explicit. Guest endurance, physical HID,
other-device support and performance are outside this source audit.
