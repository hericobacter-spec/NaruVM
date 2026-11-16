# License review for the Android source preview

Technical review, 2026-11-16. This records observed source grants and concrete
release conditions; it is not a legal opinion or a declaration that an APK is
cleared for distribution.

## Source license and original authors

Henge is GPL-2.0-or-later. Its original [LICENSE](../LICENSE) is retained verbatim;
[NOTICE](../NOTICE) preserves the later-version option and identifies this
independent Android adaptation. Original copyright and per-file license notices
remain in their files. Android-port material follows that grant unless a file
states otherwise. Do not replace this with MIT/Apache or claim ownership of the
upstream work.

[Modified upstream files](modifications.json) record the pinned Henge baseline,
original hashes and change dates. Modified C/header/assembly files also carry
prominent dated notices. New Android files, patches and build tools are separated
from vendored upstream libraries. Source publication does not transfer anyone's
copyright or trademark rights.

## Observed component terms

| Component | Observed grant and retained evidence |
| --- | --- |
| Henge/Android port | GPL-2.0-or-later; root LICENSE/NOTICE |
| usbmuxd | Source permits GPL version 2 or 3; both license texts retained |
| idevicerestore | Source headers say LGPL-2.1-or-later; COPYING contains LGPLv3 |
| libimobiledevice/libplist | LGPL library, GPL tools and nested third-party notices; individual files govern |
| glue/libirecovery/libtatsu/libusbmuxd/libusb header | LGPL-2.1-or-later; original copying text and full LGPL text retained |
| libzip | BSD-3-Clause; copyright, conditions and disclaimer retained |
| MbedTLS | Apache-2.0 OR GPL-2.0-or-later; original dual-license text retained |
| Fetched curl implementation | curl permission notice; pinned release/hash in fetch script |
| Apple LZFSE ROM decoder | BSD-3-Clause; verbatim LICENSE and checksum-pinned source in third_party/lzfse |
| Reims/metal2vulkan patches | Upstream GPL-2.0-or-later OR LGPL-3.0-or-later; pins and patches retained |
| PanVK patch | MIT and upstream file-specific notices; pinned Mesa fork and MIT notice retained |

The machine-readable [component inventory](../android/publication/licenses.json)
records exact notice paths/hashes and vendored revisions. Nested libimobiledevice
third-party and libcnary copying files remain included. Henge's curl shim header
is separate from the real curl implementation used by Android. SPDX summaries
are not substitutes for the actual files.

## Selected combination for the research APK

Observed source grants, reviewed 2026-11-16: use the **GPLv3 option** for a combined
research APK. Henge permits later GPL versions; usbmuxd permits version 2 or 3;
the Reims/metal2vulkan dual grant permits GPLv3; MbedTLS permits the GPL later-version
path. For idevicerestore, select LGPLv3 under its LGPL-2.1-or-later headers,
consistent with the included COPYING. The later-version permission in
[LGPL 2.1 section 13](https://spdx.org/licenses/LGPL-2.1-or-later.html) supports
that choice. The two texts are not, by themselves, an unresolved license conflict.
This records a distribution choice, preserves every original grant, and does not
relicense upstream files. See the [machine-readable selection](../android/publication/license-selection.json).

The [GPLv3 text](https://spdx.org/licenses/GPL-3.0-or-later.html) defines the source
and conveyance conditions. The [Apache Software Foundation's compatibility
guidance](https://apache.org/licenses/GPL-compatibility.html) distinguishes GPLv3
from GPLv2 compatibility. Preserve the original grants and review the selected
combination, rather than assuming every permissive or dual license is interchangeable.

An eventual APK needs the exact Henge/Android source, patches, target/feature
configuration and all required external dependency source/build/install inputs.
For the 29 downloaded Termux package entries, a pinned official recipe revision
now matches all 27 unique full versions. All recipe files and 30 source archives
have checksum records in the [source-input inventory](../android/publication/termux-sources.json).
The source collector and archive verifier are described in the
[publication procedure](publication.md#dependency-source-input-collection).
Observed matching versions and source hashes are narrower than proof that these
recipes produced the downloaded binaries: the deb controls contain no build-commit
attestation. libc++ copies an NDK prebuilt, which is explicitly not counted as source.
Its retained NDK notice and provenance record do not replace matching source.

Recipe license summaries describe whole packages, not necessarily the linked
library. For example, the collected XZ 5.8.4 COPYING identifies liblzma as 0BSD;
zstd 1.5.7 retains a BSD option in LICENSE alongside GPLv2 in COPYING. Preserve
the actual file-level grants and all necessary notices. Do not infer a GPLv2-only
library from those recipe summaries.

The APK packager includes the NDK toolchain notice, Rust standard-library
copyright/license collection and target-reachable Rust dependency notices.
The offline home-screen license viewer exposes packaged texts and source/replacement
instructions. Its existence does not establish complete source coverage or
release-specific replacement/installation acceptance. A public source-download
destination is still unset for this local research build. Selected Cargo, Mesa,
Rust library and original graphics Git source inputs are now separately collected
and hash-verified; see the publication procedure for counts and limits.

For recipients modifying an LGPL component, provide the selected source and the
same build configuration, rebuild the combined APK with that component replaced,
and sign/install the result with their own key. The build commands in
[building](building.md) are the starting procedure, not evidence that the exact
external binary closure has been rebuilt. No publisher key or device unlock is
required by this app. Android will reject replacement of an installed app signed
with a different key; use a separate test device/profile rather than erase data.
Record any installation-information obligation for the actual distribution.

Before a binary release, complete the inventory and distribute its matching
source/build information alongside the APK under the selected grants; verify
the applicable installation/relink conditions and visible legal notices.
An emulator/device smoke test and embedded license files alone do not close
those obligations. No binary-release approval is implied by the source checker.

## Apple material and use restrictions

Apple software, Boot ROM, restore images and guest disks are outside these
open-source grants. The [macOS Sequoia software license](https://www.apple.com/legal/sla/docs/macOSSequoia.pdf)
states Apple-branded-system restrictions, including its virtualization terms.
Owning the tablet or obtaining an image does not itself establish permission to
use macOS on Android. Source availability is not a legal clearance for that use.
Review the applicable agreement and any separate rights before deployment or
commercial distribution.

Do not publish Apple images, guest accounts, private captures or signing keys.
This preview also excludes compiled APK/IPA/driver artifacts. See the
[publication procedure](publication.md) for file-level checks and remaining
binary gates. No affiliation with Apple, Lenovo, Samsung, Qualcomm, Arm or the
upstream contributors is claimed.

The [source-correspondence audit](source-correspondence.md) maps every native
payload and distinguishes exact package/packaging identity from source builds.
A reproduced libc++ packaging transformation does not close its source-build gate.
