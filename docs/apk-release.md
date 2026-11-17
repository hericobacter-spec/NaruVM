# Experimental APK: install, source and recipient modifications

[Korean guide](apk-release.ko.md) · [Full build reference](building.md) · [License review](licensing.md)

## Download and verify

The **v0.1.1-preview** prerelease provides the first installable `arm64-v8a` APK.
The original v0.1 tag and its source assets are preserved.

[Open the release and its exact files](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.1-preview):

- `NaruVM-v0.1.1-preview-arm64-v8a.apk`
- `NaruVM-v0.1.1-preview-source.tar.gz` — project source, patches and build/install tools.
- `NaruVM-v0.1.1-preview-dependency-sources.tar.gz` — upstream source archives,
  full pinned Termux recipes/patches, NDK runtime sources and selected Cargo/Mesa/Rust sources.
- `NaruVM-v0.1.1-preview-release.json` — exact artifact hashes, certificate, configuration and observed checks.
- `SHA256SUMS.txt`

Keep downloaded files together and run `shasum -a 256 -c SHA256SUMS.txt` on macOS,
or `sha256sum -c SHA256SUMS.txt` on Linux. Download all listed assets before
checking the complete checksum file. GitHub's automatic source ZIP is not the
dependency-source archive listed above.

With Android SDK build-tools 36.0.0:

```sh
apksigner verify --verbose --print-certs NaruVM-v0.1.1-preview-arm64-v8a.apk
```

Expected certificate SHA-256:
`d3f0ccbbc25d555d35a2867c2a0848fe7bfde9c0b8cefdd3e0ea3c71b63908be`.
The publisher's private keystore and password are not distribution assets.

## Install without building

Requirements: 64-bit ARM Android 9/API 28 or newer; CPU instruction and selected
GPU/backend capability checks still apply. A compatible ABI alone does not
establish macOS compatibility. PanVK requires its documented Android/GPU conditions.

Open the APK on Android and permit installation from the app that opened it,
or use USB debugging with Android platform-tools:

```sh
adb install NaruVM-v0.1.1-preview-arm64-v8a.apk
```

Open **NaruVM**. Start with **2 vCPU / 2048 MiB**, review the detected backend,
and run the disk-free preflight before attempting a guest. The APK contains
neither macOS nor a guest disk. Guest downloading/installation is a separate,
explicit action; [installation steps and storage estimates](installation.md#6-check-the-app-and-choose-resources)
apply. The new complete automatic installer and macOS 26/27 remain untested.

**Existing research installation:** its debug key differs from this release key.
Android will reject an update of `dev.henge.research` with a different signer.
Do not uninstall or clear your existing app to force an update: that could delete
its guest data. Use a separate device or disposable emulator for this preview.
Future APKs signed with this same release key and a higher version code can use
`adb install -r`; this is not a guest-data migration tool.

## What was checked and what remains untested

See the matching [release record](../android/publication/apk-release.json) and
the downloadable release JSON for dated, artifact-specific results. The test
scope is host source checks plus a disposable Android 35 ARM64 emulator:
installation, first launch, offline legal/source viewer, native disk-free probe,
recipient-modified LGPL library and independent-key installation. The release
manifest disables Android debugging and retains `allowBackup=false`.

No physical tablet was available for this candidate. Earlier TB373FU guest runs
are historical evidence, not a retest of this signed APK. No macOS guest was
installed or booted during release acceptance. GPU compatibility, sustained use,
physical keyboard/mouse input, 16K runtime pages, the complete new installer and
macOS 26/27 are unverified. Slow interaction and past GPU faults/reboots remain
open; this is an experimental preview, not a stability or speed improvement.

## Source provision versus rebuild attestations

Download **both** source archives from the same release as the APK. The project
archive has `SOURCE-MANIFEST.json`; extract it into a new empty directory. The
dependency archive contains inventories and `archives/` with the pinned inputs.
For example, after extracting the project archive:

```sh
mkdir -p .tools/publication-sources
tar -xzf /path/to/NaruVM-v0.1.1-preview-dependency-sources.tar.gz -C .tools/publication-sources
python3 android/package_publication_sources.py --verify /path/to/NaruVM-v0.1.1-preview-dependency-sources.tar.gz --ndk-source --graphics
```

The verifier reports 94 source inputs and four original Git snapshots. It checks
this input archive's membership/hashes; its legacy `complete_apk_source=0` marker
is not a runtime or legal verdict on the separate release set. Source-only
export manifests likewise describe their own scope, not a complete APK audit.

The native-origin audit separately maps all packaged native members and repeats
release strip transformations. Unchanged Termux library bytes map to checksum-
pinned official packages; the supplied source archives, complete version-matched
recipes and patches are provided alongside them. Their historical producing
commit is not attested, and a full independent Termux/LLVM rebuild is not claimed.
Those limits remain visible; the historical research audit's unconditional
`--require-complete-source` exit 2 is not changed into a successful attestation.
The new release record distinguishes those forensic limits from the actual
source provision, packaging and installation checks.

The GNU FAQ distinguishes [corresponding source from exact-hash reproducibility](https://www.gnu.org/licenses/gpl-faq.html#MustSourceBuildToMatchExactHashOfBinary).
The distribution review uses [GPLv3 sections 1 and 6](https://spdx.org/licenses/GPL-3.0-only.html)
and [LGPLv3 section 4](https://spdx.org/licenses/LGPL-3.0-only.html); it does not
claim that every permissive library or the compiler must be rebuilt to an
identical hash. Original file-level grants/notices remain, and this technical
review is not a categorical legal opinion.

## Build and sign your own APK

Use an Apple Silicon Mac and [the pinned SDK/NDK/JDK and graphics setup](building.md#optional-graphics-and-software-renderer).
The dependency sources above remain available even when rebuilding uses the
existing fetch/setup helpers. Tool downloads, Android system libraries and
Linux host build tools are separate prerequisites; this is not a full offline
or fresh-machine build claim.

Create a **private PKCS12 keystore with the same store/key password** and a
password text file; both must have owner-only permissions (`chmod 600`).
Keep them outside the source/release assets. Then build the recorded profile:

```sh
python3 android/build.py --gpu --reims --software --panvk-userptr-probe --release --version-name 0.1.1-preview --version-code 2 --keystore /private/path/recipient.p12 --ks-pass-file /private/path/password.txt --key-alias recipient --output /private/path/NaruVM-recipient.apk
```

The default research build still uses its separate debug key. Release builds
require explicit signing inputs, refuse an existing output, disable debugging,
remap compiler paths and strip local native debug/symbol data before packaging.
Public source instructions never require the publisher's private signing key.

## Replace an LGPL component and reinstall

Use a separate disposable Android emulator/device with no valued guest data.
Modify the library's source, rebuild the relevant native target using the same
configuration, then rebuild the complete APK with your own key as above. For a
bounded test, modify only `plist_new_string` in `third_party/libplist/src/plist.c`:
when its input is `NaruVM LGPL replacement probe`, substitute
`NaruVM recipient-modified LGPL library`. Leave other inputs unchanged.

After the normal graphics build has configured `android/build/native`, rebuilding
this statically linked component and its executable is:

```sh
cmake --build android/build/native --target henge_lab -j 2
```

For an existing APK, recombine only that rebuilt native payload and sign with
your own key (the file is an executable ELF with an Android packaging `.so` name):

```sh
python3 android/repackage_apk.py --apk /path/to/NaruVM-v0.1.1-preview-arm64-v8a.apk --replace-native libhenge_lab.so=android/build/native/bin/libhenge_lab.so --release --keystore /private/path/recipient.p12 --ks-pass-file /private/path/password.txt --key-alias recipient --output /private/path/NaruVM-modified.apk
adb install /private/path/NaruVM-modified.apk
```

The existing app/native members stay unchanged except the selected library and
signing metadata. The helper rejects new member names and non-ARM64 replacements,
re-aligns and verifies the newly signed result. Use `--sdk` when SDK tools are
outside this source tree; use `JAVA_HOME` for a separately installed JDK.

The worker's `license-probe` prints `LGPL_REPLACEMENT_VALUE` followed by the
library-returned value, so a source behavior change can be distinguished from
merely re-signing an unchanged binary. For an isolated emulator probe, extract
`lib/arm64-v8a/libhenge_lab.so` from **your modified APK**, push it to
`/data/local/tmp/naruvm-recipient-worker`, run `chmod 755` on that file and execute
it with `license-probe`. No Apple image, guest account, root or publisher key is
needed. This bounded probe is not a macOS boot/stability test.
