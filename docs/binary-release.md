# APK distribution status and release preparation

[Korean guide](binary-release.ko.md) · [Installation](installation.md) · [Source audit](source-correspondence.md)

## Signed experimental preview

[v0.1.3-preview](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.3-preview) provides the signed ARM64 APK, both source archives and checksums. Follow
the [APK guide](apk-release.md) for verified scope, installation and modifications.
The original v0.1 remains source-only.

## Historical research-APK assessment

The following records the earlier `55d78d6f…ee37` research artifact, not the new signed
candidate. Its conservative audit flags and unresolved producer attestations are
preserved; current artifact-level checks are in the linked guide.

## Download status before the signed preview

As observed on **2026-11-17**, [v0.1 Releases](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1)
contains a project source archive and checksums. **At that assessment, no APK was published.**
The existing research APK is a local test artifact, not a cleared release asset.
To try the app now, use the [source build and installation guide](installation.md).
An APK distribution is a project goal; source-only is not a permanent product restriction.

## What has been checked

The inspected research APK has SHA-256
`55d78d6f8077bce71dba7e6ad5add2d870b9b4632905b110bdddf5ecf345ee37`.
The 2026-11-17 offline audit matched all **53 native members**: 27 exact files
from pinned binary packages and 26 local build outputs. Its recorded scope is
`complete_source_correspondence=false` and `binary_release_ready=false`.
See the [per-file inventory](../android/publication/source-correspondence.json).

Source previews already retain project source, dependency pins, patches,
notices and build helpers. Separately collected dependency source inputs exist
locally; they are not included in the current GitHub release's project archive.
The [source collection procedure](publication.md#dependency-source-input-collection)
explains how contributors can fetch and verify the pinned inputs.

## Remaining work for an APK release

These are concrete project release checks, not a legal determination that an
experimental APK can or cannot be distributed. Calling a file a preview does
not replace the applicable source and notice obligations. The
[license review](licensing.md) and the actual license texts govern those terms.

| Item | Current evidence | Work to finish |
| --- | --- | --- |
| External binary packages | Versions, original binary hashes and candidate source/recipe inputs recorded | Establish the actual source/patch/configuration for shipped libraries, or rebuild them from retained inputs and record the new chain |
| NDK C++ runtime | Exact official runtime origin and packaging reproduced | Close the recorded runtime source/build-information gap; do not present packaging as a source build |
| Complete source delivery | Project archive is public; selected external inputs are separately collectable | Publish the applicable matching dependency/build material alongside the exact APK, with durable links and checksums |
| Modified-library rebuild | Source and build helpers present | Demonstrate rebuilding/recombining with a changed LGPL component, signing with a recipient's key and installing on an isolated test target |
| Release signing | Local research signing exists | Select a dedicated private release key; publish only its certificate fingerprint, never the key; document update/signature behavior |
| User acceptance | Historical single-tablet observations | Test the exact candidate's first launch, notices, input and normal stop; disclose the unverified full installer and device matrix |

The used external package versions are recorded by the
[source audit](source-correspondence.md#remaining-source-gates): glib,
libandroid-shmem, libandroid-support, libc++, libdrm, libffi, libiconv, libllvm,
libslirp, libwayland, libx11, libxau, libxcb, libxdmcp, libxml2, libxshmfence,
Mesa software Vulkan, pcre2, zlib and zstd. Twenty package versions are not twenty
proven license violations: this is the audit's unresolved build-provenance set.
Assess applicable obligations per component and the selected combination.

Hash equality is a useful integrity/reproduction check; it is not a substitute
for source completeness. Conversely, this project's conservative source-build
checks must not be described as a universal legal requirement for byte-identical
binaries. See the [GNU FAQ on binary hashes](https://www.gnu.org/licenses/gpl-faq.html#MustSourceBuildToMatchExactHashOfBinary),
[GPLv3 sections 1 and 6](https://spdx.org/licenses/GPL-3.0-only.html) and
[LGPLv3 section 4](https://spdx.org/licenses/LGPL-3.0-only.html).

## Proposed release contents

After those checks, a separate experimental binary release should identify:

- The exact signed `arm64-v8a` APK, commit, version and build profile. State whether
  software rendering and PanVK are bundled; do not imply Turnip is included.
- `SHA256SUMS.txt`, a signing-certificate SHA-256 fingerprint and verified install
  instructions. Explain that a different signing key cannot update an existing
  research installation; never suggest erasing its data as the default fix.
- The project source plus applicable matching dependency sources, patches,
  configuration/build/install scripts, notices and a per-library inventory.
- Honest device/test results, known crashes/slowness, and untested versions.

Apple images, ROMs, guest disks, local accounts and publisher signing keys do
not belong in the APK or source attachments. Preserve the original v0.1 tag
and source assets; publish a new binary release rather than replacing history.

## Repeat the current assessment

Prepared local inputs are required; the public source checkout does not contain
an APK or caches. Use the [auditor instructions](source-correspondence.md#repeat-the-offline-checks).
The normal origin check can pass while `--require-complete-source` returns exit
code **2** for the unresolved completeness status. An origin match or a green
source-only CI badge is not an APK release approval.
