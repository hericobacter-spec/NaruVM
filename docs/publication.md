# Prepare a source-only GitHub repository

Audience: a developer with Python 3.12+, a C compiler, JDK 21 and Git, reading
English or Korean. This procedure produces a local, reviewable source preview.
It creates no GitHub repository, commit, remote, tag, upload or release.

## 1. Check the source

Run from the source root:

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
python3 android/tests/test_publication.py
python3 android/tests/test_publication_sources.py
```

Expect `PORTABILITY_HOST_RESULT failures=0`,
`PUBLICATION_SOURCE_RESULT failures=0` and `OK` from the boundary tests.
An exported tree has `SOURCE-MANIFEST.json`. After intentional source edits,
refresh it with `python3 tools/export_android_source.py --refresh-manifest`,
review the changed hashes, then repeat the checks. Do not refresh blindly to
hide an unexpected change. This file is an integrity inventory, not a signature.

## 2. Create a separate local staging repository

Choose a new, unused output path. The following default creates
`android/artifacts/github-source` and refuses to overwrite it if it exists:

```sh
python3 tools/prepare_github.py
```

For a later rehearsal, choose another fresh path with `--output`. The tool uses
an allowlist, rejects selected symlinks/binaries/private-data patterns, preserves
license texts and patches, writes a source manifest, initializes `main` and stages
the files. It checks whitespace in authored source/docs, preserving vendored
license/source formatting and patch context. Expected final marker:
`GITHUB_PREPARE_RESULT failures=0 scope=source-only staged=1 commit=none remote=none published=0`.

Only the new directory is staged. Existing history and remotes are excluded.
In particular, an upstream Henge remote in your research checkout must never be
used as the publication destination.

Inspect the result from the original source root:

```sh
git -C android/artifacts/github-source status --short
git -C android/artifacts/github-source remote -v
git -C android/artifacts/github-source diff --cached --stat
python3 android/artifacts/github-source/tools/check_publication.py --staged
```

The remote command must print nothing. There is no commit yet. Review the full
staged diff and attribution before making any public action. In exported Git
checkouts, the checker also rejects tracked files outside the inspected selection.
Automated screening
has finite patterns; it cannot prove the absence of every secret, personal detail
or copyright issue. Only three individually reviewed PNGs and one explicitly labeled still-capture
MP4 in `PUBLIC_MEDIA` are included as documentation media; see
[capture context](images/README.md) and [video context](videos/README.md). Private research
logs, other captures, guest data, tool caches,
signing keys, compiled artifacts, the old iOS release workflow and Git history
are outside the export. Do not replace this isolated tree with a blanket add of
the research checkout.

## 3. Run the final offline source gate

Keep the C compiler, Git, Python 3.12+ and JDK21 available. Set `JAVA_HOME` to
your actual JDK21 home if `java`/`javac` are not already on PATH. From the
original source root, run the gate inside the newly staged tree:

```sh
python3 android/artifacts/github-source/tools/verify_github_ready.py --output android/artifacts/source-gate.json
```

If you are already inside an exported source tree or a GitHub clone, run from
that tree's root instead:

```sh
python3 tools/verify_github_ready.py --output android/artifacts/source-gate.json
```

Use a new JSON output name on later runs. Expected final marker:
`GITHUB_SOURCE_GATE_RESULT failures=0 scope=source-only` with
`binary_release_ready=0 published=0`. The gate runs CPU/resource/backend/input
policy checks, 16 publication-boundary tests, seven dependency-integrity tests,
six origin tests and eight archive tests, Git object integrity, and two
byte-identical source archive builds. It checks the actual Git index blobs
against the reviewed worktree and manifest before and after execution. It
performs no network, device, signing, commit or upload operation.

The staged-byte comparison is necessary: restoring a worktree file does not
restore a different version already staged in Git. After intentional changes
in an exported checkout, review the changes, refresh its manifest and stage
the changed files before repeating this final gate. Do not bypass a mismatch
by blindly refreshing or staging files you have not reviewed.

Observed 2026-11-16: this gate passed locally on Apple Silicon macOS and an
isolated Ubuntu24.04/AArch64 container with its network disconnected. A basic
diagnostic APK was separately rebuilt from selected source with prepared pinned
tools and copied dependency caches; signing and 16K ZIP-alignment checks passed.
The optional full graphics build was observed earlier with prepared tools.
These are bounded local observations; actual GitHub-hosted CI, a wholly fresh
machine and complete APK source correspondence are not established by them.

## 4. Produce and verify a deterministic source archive

From the original source root (or an exported checkout):

```sh
python3 tools/export_android_source.py --output android/artifacts/source-preview.tar.gz
python3 tools/export_android_source.py --verify android/artifacts/source-preview.tar.gz
```

Expected marker: `SOURCE_VERIFY_RESULT failures=0`, followed on creation by
`SOURCE_SHA256`. The same selected file bytes produce the same archive. The
archive has no outer directory: extract only into a new empty directory.
It contains a manifest, not a complete corresponding-source bundle for an APK.
Verification rejects noncanonical/traversing paths, duplicate members/manifest
paths, wrong sizes/hashes, promoted binary-release scope and recognized binary
payloads even when their manifest hash matches. Integrity screening is bounded;
it does not authenticate a publisher or replace the full source gate above.

## 5. GitHub settings to review when publishing

After the local result is accepted, create a repository under the intended owner
with a description such as “NaruVM: experimental ARM64 macOS runtime for Android; source preview.”
Choose the visibility intentionally. Do not add a generated license that replaces
the retained GPL grant. Review the first commit and destination before uploading.
These are future publication steps, not actions performed by this tool.

Enable Issues if desired; the included bug/device forms request sanitized
evidence and distinguish untested hardware. Enable private vulnerability
reporting before advertising it as available. Review the security contact policy.
Protect `main` and require the `host-source` check after its first successful
GitHub run. The workflow has a read-only token and a commit-pinned checkout; it
does not publish artifacts or run privileged pull-request code. GitHub-hosted CI
execution itself remains unverified until the repository exists and a run passes.

All selected files are also checked against a conservative 100MiB maximum policy;
the observed largest source file is about 1.45MiB. Keep the large dependency
source-input archive separate. GitHub's [regular Git file limits](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)
are not a reason to add binaries or private caches to the source repository.

No owner, support address, project funding, CI badge or binary download link is
invented in this preview. Add those only when the actual destination and policy
exist. The community files follow
[GitHub's community-file conventions](https://docs.github.com/en/communities/setting-up-your-project-for-healthy-contributions/creating-a-default-community-health-file);
workflow controls follow
[GitHub's secure-use guidance](https://docs.github.com/en/actions/reference/security/secure-use).

## Source preview versus binary release

The source preview includes upstream attribution, dated modification notices,
20 component records, retained license texts, external pins/patches and explicit
limitations. The checker verifies those files and their recorded hashes, not
the legal sufficiency of a distribution.

Before publishing an APK, close all of the following separately:

- Preserve the documented GPLv3 combination choice and all upstream grants;
  review every component in the exact release configuration.
- Verify correspondence between the 29 downloaded package entries and the
  version-matched recipes/source inputs now recorded; absent build-commit
  attestations must remain explicit. Validate the collected NDK libc++, Rust/Cargo and Mesa source inputs
  against that exact build, plus release-specific install/relink information.
- Retain every source/subproject and host-tool input for the selected graphics
  build. A fresh source/output rebuild with prepared pinned toolchains has been
  observed; a wholly fresh machine and deterministic APK rebuild remain unverified.
- Verify the exact release's embedded notices and offline license viewer,
  source-download destination and any applicable installation requirements. Never include Apple software, guest data or signing keys.
- Publish only the tested device/status claims and a release-specific source
  manifest. Unresolved reboots/faults and untested devices must remain explicit.

See [licensing](licensing.md), [build scope](building.md) and the
[dependency inventory](../android/publication/dependencies.json). Source-preview
preparation is not a binary-release approval or a macOS-use license.

## Dependency source-input collection

The [Termux source lock](../android/publication/termux-sources.json) records one
official recipe commit matching 29 binary entries / 27 unique package versions,
all recipe-file hashes, and source URLs/checksums. The binaries do not attest
which commit built them; `exact_recipe_commit` deliberately remains null in the
binary inventory. Version-matched source inputs are a candidate for further
correspondence checks, not a completed APK source offer.

From the source root, explicitly allow public downloads once:

```sh
python3 android/fetch_publication_sources.py --download
python3 android/fetch_publication_sources.py
python3 android/package_publication_sources.py --output android/artifacts/termux-source-inputs.tar.gz
python3 android/package_publication_sources.py --verify android/artifacts/termux-source-inputs.tar.gz
```

To also collect the pinned official LLVM source and Android toolchain build/patch
scripts identified in the NDK provenance record, use the optional source flag:

```sh
python3 android/fetch_publication_sources.py --download --ndk-source
python3 android/package_publication_sources.py --ndk-source --output android/artifacts/dependency-source-inputs.tar.gz
python3 android/package_publication_sources.py --ndk-source --verify android/artifacts/dependency-source-inputs.tar.gz
```

This adds two actual source archives (32 in total), not the NDK binary ZIP or
prebuilt release tree. These source downloads have been hash-verified locally;
the Termux libc++ library has not been rebuilt from them.

The collector never executes downloaded recipes. It records results under ignored
`.tools/publication-sources/status.json` and refuses existing checksum mismatches.
Default mode validates offline. If the recipe archive is absent, `--download`
fetches only the pinned Git commit and archives it; `--recipe-tree` can use an
existing object store containing that commit. Archive verification checks the
uncompressed pinned recipe content, so gzip implementation differences do not
change the accepted source tree.

Expect `PUBLICATION_DEP_SOURCE_RESULT inputs=30 failures=0 binary_release_ready=0`
and `TERMUX_SOURCE_VERIFY_RESULT failures=0 source_inputs=30 git_sources=1 complete_apk_source=0`.
The optional source-input archive contains the complete recipe tree, source lock
and 30 unchanged source archives, including ncurses terminfo inputs. It omits
the prebuilt NDK ZIP, APKs, guest files, caches and keys. It is separate from the
small GitHub project-source preview and is not added to Git. Choose a new output
name for repeated packaging; existing outputs are preserved.

libc++ is copied from an NDK prebuilt by its recipe. The
[NDK provenance record](../android/publication/ndk-runtime.json) distinguishes that
binary origin from source. Neither a matching build ID nor a prebuilt ZIP closes
matching-source requirements. The additional selected source collection below
reduces missing-input risk; it does not attest the shipped binary closure.


### Selected graphics and restore sources

Observed on 2026-11-16: the [graphics source inventory](../android/publication/graphics-sources.json)
records the Android ARM64 Metal/default and Reims/Vulkan/no-default-features Cargo
graphs. It collects 54 registry crate archives by Cargo.lock checksum, five Mesa
wrap source/patch inputs matching the fresh PanVK build, official Rust library
source, statically linked curl source, supporting ELF packaging-tool source, and original Git snapshots at the three pinned graphics commits. Local
workspace sources come from this project plus those snapshots and Android patches.

```sh
python3 android/fetch_publication_sources.py --download --ndk-source --graphics
python3 android/fetch_publication_sources.py --ndk-source --graphics
python3 android/package_publication_sources.py --ndk-source --graphics --output android/artifacts/selected-source-inputs.tar.gz
python3 android/package_publication_sources.py --ndk-source --graphics --verify android/artifacts/selected-source-inputs.tar.gz
```

Expected: `PUBLICATION_DEP_SOURCE_RESULT inputs=94 failures=0 binary_release_ready=0`
and `DEPENDENCY_SOURCE_VERIFY_RESULT failures=0 source_inputs=94 git_sources=4 complete_apk_source=0`.
The four Git trees are Termux recipes, metal2vulkan, Reims and Mali/PanVK; archives
read the pinned commit, never local working-tree edits. Verification checks exact
members and hashes, including uncompressed Git tar hashes. Gzip implementation
variation does not change the accepted source content. Existing outputs are never
overwritten. Keep this large dependency bundle separate from the small project
source repository; the archive itself is not added to Git.

This is an input collection, **not an offline full-build installer or a complete
APK source offer**. Existing builders still obtain pinned Git objects and use
prepared SDK/NDK/Rust tools and package binaries. Source snapshots do not contain
Git history. Follow [building](building.md) for source checkout/patch steps; retain
the selected graph, toolchain and host-tool inputs for each exact release.
Termux binary/recipe correspondence, libc++ rebuild correspondence, replacement
installation and exact-build device endurance remain separate acceptance gates.


For exact APK per-file origin/rebuild evidence and unresolved source gates, see
[source correspondence](source-correspondence.md). An origin audit pass is not a
complete corresponding-source or binary-release approval.

## Separate signed APK release

The source gate above deliberately retains `binary_release_ready=0`; it does not
execute Android or approve binaries. The v0.1.1-preview release adds separately
recorded signing, source delivery, native origin/packaging, emulator first-launch
and actual recipient-modified LGPL installation checks. See the [APK guide](apk-release.md)
and [artifact record](../android/publication/apk-release.json). Both source archives
are supplied alongside the APK; a project-only source export is not that set.
Historical external producing commits and whole dependency rebuilds remain
unattested. Preserve the original source-only v0.1 tag/assets.
