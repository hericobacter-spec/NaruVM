# Physical tablet capture: v0.1.3-preview

[Korean guide](device-capture.ko.md) · [Project overview](../README.md) · [Video and transcript](videos/README.md)

**Observed on 2026-11-17, one physical tablet (n=1).** The installed APK was
pulled back from the tablet and matched the public release byte for byte before
this session. This is a bounded existing-guest demonstration, not a benchmark
or a complete installation or stability test.

| Capture condition | Observed value |
| --- | --- |
| Tablet | Lenovo TB373FU; MediaTek MT8792; Android 16 / API 36 |
| APK | v0.1.3-preview, version code 4, non-debuggable ARM64 release |
| APK SHA-256 | `004572445072ca760d2ac65f6d8f26123809b606249c3be15c210dda11b4f91c` |
| Existing guest | macOS 15, Korean interface; existing local account and disk retained |
| Saved allocation | 4 vCPU / 4096 MiB; experimental Mali PanVK (`panvk-userptr`); mouse gain 1× |
| Guest display / screenshots | 1024 × 768 guest; complete 2944 × 1840 Android display captures |
| Recorded video | 1280 × 800 H.264, variable frame rate, 95.310 seconds, no audio |
| Input | ADB-driven Android touch/key events and the app's text tool; no physical external mouse/keyboard acceptance |

The saved allocation was left unchanged for this existing guest. It is **not** a
new recommended default; the conservative starting point remains 2 vCPU /
2048 MiB. Guest and Android clocks use different time zones; their displayed
clock times should not be compared as a timing measurement.

## What happened

The exact public APK opened the existing guest, reached local login, and
subsequently displayed the desktop. Personal restored Terminal windows were
hidden and other windows minimized before recording. The recording shows Safari
opening from the Dock, the public `https://example.com` address entered with the
app's tools, and the resulting web page. The screenshot of that page was taken
in the same session. The session was then stopped manually using **Stop Mac**.
No automatic deadline was applied to the VM.

Updating this particular maintainer-owned research installation required a
private Android signing-key rotation bridge before reinstalling the exact
public APK. Both private keys were already available; an isolated emulator
first retained an authored data marker through both updates. No physical app
uninstall, data reset or macOS reinstallation was used. **This is not a generic
migration tool for research APKs signed by somebody else.** Follow the
[signing/data-preservation guidance](apk-release.md) for other installations.
The bridge and signing material are not published, and public releases were
not replaced.


The exported release-app session log reports 758.8 seconds including boot,
`timeout=0`, `panic_events=0`, `flush_failed=0` and `EXIT_CODE=0`, followed by
foreground-service/wake-lock cleanup. The tablet boot identifier matched before
and after. These are observations from this session, not historical reboot
root-cause resolution or a long-duration acceptance test.

## Media and privacy

- [Home](images/tb373fu-2026-11-17-home.png),
  [desktop](images/tb373fu-2026-11-17-desktop.png) and
  [Safari](images/tb373fu-2026-11-17-safari.png): unedited original PNG captures.
- [Live MP4](https://raw.githubusercontent.com/hericobacter-spec/NaruVM/main/docs/videos/tb373fu-2026-11-17-live.mp4): Android `screenrecord`, with a 120-second recording limit.
  The resulting variable-rate file is 95.310 seconds long. Its original video
  timestamps were retained; no cause for the shorter-than-limit duration is claimed.
- The MP4 was remuxed only to put playback metadata first and remove incidental
  container tags. All 466 encoded video packet hashes and timestamps match the
  original. There are no cuts, speed changes, synthetic frames or audio.
  The stored frame count/rate is **not a guest FPS measurement**.
- Review: full-file decode, all 95 one-second visual samples, enlarged input/page
  scenes, original screenshots and metadata. This is not a claim that every
  encoded frame was manually inspected. No account/password, device serial,
  private terminal or local path was visible in the reviewed public media.
  Login and private setup were completed before recording.
- Exact sizes and hashes: [capture record](../android/publication/device-capture-2026-11-17.json).
  Only the named media files are allowlisted for export. Raw logs, guest disks,
  accounts, unreviewed footage and signing material remain excluded.

## Limits

Opening this page once does not establish network reliability. One bounded
session does not resolve historical GPU faults/tablet reboots or establish
sustained stability, everyday usability or near-native speed. No controlled
before/after speed comparison was made. External physical input, other devices
and GPUs, 16K runtime pages, a full new automatic installation and macOS 26/27
remain unverified. The earlier screenshots/slideshow are retained separately
as [historical material](images/README.md).

Third-party UI, artwork and marks retain their respective rights; the project
source license does not relicense them.
