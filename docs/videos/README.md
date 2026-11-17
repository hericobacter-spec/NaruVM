# Actual tablet recording

[Korean guide](README.ko.md) · [Project overview](../../README.md#video)

**[Watch / download the actual 95-second MP4](https://raw.githubusercontent.com/hericobacter-spec/NaruVM/main/docs/videos/tb373fu-2026-11-17-live.mp4)**

Observed 2026-11-17 on a physical TB373FU running the exact public
v0.1.3-preview APK. Existing macOS 15 guest, 4 vCPU / 4096 MiB / Mali PanVK.
ADB-driven app inputs; silent and continuous, no speed changes. See
[device/build verification, privacy review and limits](../device-capture.md).

## Live recording transcript

Approximate MP4 timestamps; pauses include both operator waiting and guest response.
This is not a latency benchmark.

| Time | Actual screen / action |
| --- | --- |
| 00:00–00:18 | Existing macOS desktop; restored personal windows already hidden |
| 00:19–01:06 | Safari opens from the Dock and displays its start page |
| 01:07–01:26 | Address bar focused; app tools enter only the public example.com URL |
| 01:27–01:29 | Tools close and Safari begins displaying the result |
| 01:30–01:35 | example.com web page visible |

File: [tb373fu-2026-11-17-live.mp4](tb373fu-2026-11-17-live.mp4).
H.264 / yuv420p, 1280 × 800, variable rate, **95.309578 seconds**, **466 stored
frames**, one video stream, no audio; **2,416,912 bytes**. SHA-256:
`c8df35d6c61ba5a5fff7a10f6a13d9233e88acfe758771e5b16b6e0811d6ab77`.
All encoded video packet hashes/timestamps match Android's recording original;
only the container was remuxed for fast-start playback and incidental tag removal.
The stored frame rate is not guest FPS. Recorder limit was 120 seconds; the
resulting file is shorter. No cuts, acceleration or synthetic frames were added.

Open the MP4 link in a browser/player; GitHub clients may download it. Current
capture hashes are in the [machine-readable record](../../android/publication/device-capture-2026-11-17.json).

## Historical still-image slideshow

[Korean guide](README.ko.md) · [Project overview](../../README.md#video)

**[Watch / download MP4](https://raw.githubusercontent.com/hericobacter-spec/NaruVM/main/docs/videos/historical-captures.mp4)**

This is a **still-image slideshow, not a live screen recording**. When this older slideshow was prepared, no relevant
execution-recording original was found in the reviewed local research folders.
The video was assembled on 2026-11-17 from the three previously published,
privacy-reviewed [TB373FU macOS 15 screenshots](../images/README.md).
They predate v0.1 and do not show the current Android home.

## Scenes and text transcript

| Time | Scene | Meaning |
| --- | --- | --- |
| 00:00–00:03 | NaruVM / Historical capture walkthrough | Independent experimental Android source preview; existing TB373FU macOS 15 research captures, November 2026, before v0.1 |
| 00:03–00:08 | Setup Assistant | Historical language and input-source screen |
| 00:08–00:14 | Safari | example.com displayed in one historical research run |
| 00:14–00:20 | VS Code | Editor open in Restricted Mode; no execution benchmark |

Every scene says **STILL CAPTURES / NOT A LIVE RECORDING**. There is no audio,
input animation, generated guest content or simulated pointer movement.
The scenes are separate captures; their order is editorial, not evidence of
one continuous install or session. They do not establish interaction speed,
network reliability, installation completion or sustained stability.

## File and provenance

- File: [historical-captures.mp4](historical-captures.mp4)
- Format: H.264 / AVC, yuv420p, 1280 × 900, 24 fps, 20 seconds, 480 frames;
  fast-start MP4, one video stream, no audio.
- Size: 299,732 bytes.
- SHA-256: `c24bc33fdd461ede8e8931e30e4af2fbcf6d8431da835952ed6cbf36c74bd7c4`.
- Source images, in order: `../images/macos-setup.png`,
  `../images/macos-safari.png`, `../images/macos-vscode.png`.
- Editing: fit the complete screenshots to a captioned canvas; add an
  introductory card and fixed scene durations. Original PNGs are unchanged.
- Reviewed: all four scene compositions, full-file decoding and container tags.
  Container metadata contains standard codec/container fields, no account,
  location, device identifier or local path. Captured third-party UI and marks
  retain their respective rights; source licensing does not relicense them.

Click the README preview or the watch link to open the MP4. If your browser
downloads the file, open it in a local video player. The README uses an ordinary
linked image, not a claim of inline video playback on every GitHub client.
Only the two named, reviewed MP4s are included by the source exporter; arbitrary
recordings remain excluded. No APK or guest image is included.
