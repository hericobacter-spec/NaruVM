# Current physical-tablet screenshots

Observed 2026-11-17 on one TB373FU with the exact public v0.1.3-preview APK.
The existing macOS 15 guest and saved 4 vCPU / 4096 MiB / Mali PanVK allocation
were retained. These are unedited 2944 × 1840 Android screenshots, reviewed for
visible private data and metadata.

| File | Visible screen |
| --- | --- |
| [tb373fu-2026-11-17-home.png](tb373fu-2026-11-17-home.png) | Current mint NaruVM home and saved resource controls |
| [tb373fu-2026-11-17-desktop.png](tb373fu-2026-11-17-desktop.png) | Existing macOS desktop after local login; personal windows hidden |
| [tb373fu-2026-11-17-safari.png](tb373fu-2026-11-17-safari.png) | Safari displaying example.com in the same session as the actual recording |

[Device/build/input conditions and limits](../device-capture.md) ·
[Korean capture guide](../device-capture.ko.md) · [Actual video](../videos/README.md).
No complete new installation, measured speed gain or sustained stability is claimed.

# Historical Android research screenshots

These are existing captures from the TB373FU macOS 15 research sessions in
November 2026, before the NaruVM v0.1 release. They show guest screens, not the
current mint/teal Android home. They were inspected for visible private data
and copied without editing for this documentation update on 2026-11-17.
Guest UI language is Korean; the Android app also has an English interface.

| File | What is visible | What it does not prove |
| --- | --- | --- |
| [macos-setup.png](macos-setup.png) | Setup Assistant language/input-source screen | A complete run of the new Android automatic installer |
| [macos-safari.png](macos-safari.png) | Safari displaying example.com | Reliable networking or sustained guest stability |
| [macos-vscode.png](macos-vscode.png) | VS Code with a small authored Python file, in Restricted Mode | A complete development environment or native-like performance |

The six named PNGs and two [reviewed videos](../videos/README.md)
are allowed by `PUBLIC_MEDIA` in
[the exporter](../../tools/export_android_source.py). Other captures, research
logs, guest images, accounts and signing material remain outside publication.
`SOURCE-MANIFEST.json` records each included file's bytes and SHA-256.

Third-party software UI, artwork and marks in the captures retain their
respective rights; the project's source license does not relicense them.
