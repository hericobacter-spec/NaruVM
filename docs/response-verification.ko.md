# 입력·화면 개선: v0.1.3-preview

[English](response-verification.md) · [다운로드·서명·소스](apk-release.ko.md)

## APK에 포함된 변경과 범위

버전 코드 **4** APK에는 [입력·화면 소스 변경](https://github.com/hericobacter-spec/NaruVM/commit/c90b946d6bac0341bdfc6381774e23a4dc12f241)의 입력 대기열 제한, 과부하 시 명시적 키·버튼 해제, 작업 스레드 FramePump와 NEON BGRA 변환이 포함됩니다. 인접한 상대 이동은 총 이동량과 이벤트 경계를 보존해 합칩니다. 과부하에서는 오래된 입력을 버리고 눌린 상태를 해제합니다. 종료는 대기 입력을 우회하지만 이미 커널에서 막힌 파이프 쓰기를 선점하지 못합니다. UI 전달은 하나만 대기하며 최종 Bitmap 복사와 GPU 읽기는 남아 있습니다. zero-copy·네이티브급 속도 달성을 주장하지 않습니다.

## 정확한 APK 검증 — observed, 2026-11-17

파일은 `NaruVM-v0.1.3-preview-arm64-v8a.apk`, **85,778,737바이트**, SHA-256은 다음과 같습니다.

```text
004572445072ca760d2ac65f6d8f26123809b606249c3be15c210dda11b4f91c
```

[배포 기록](../android/publication/apk-release.json)에 구성·인증서·네이티브 해시가 있습니다. 별도 Android35 ARM64 에뮬레이터 한 대(2 vCPU/2048 MiB, SwiftShader, 1280×800, 4K 실행 페이지)에서 시험했습니다. Apple 이미지·게스트 디스크를 쓰지 않는 자체 작성 검사입니다.

| 검사 | 관측 결과 |
| --- | --- |
| 서명·포장 | 기존 배포 인증서와 같은 v3 서명, 16K ZIP 정렬, 디버깅·백업 비활성 |
| 공개 v0.1.2 → v0.1.3 업데이트 | install -r로 앱 UID·자체 데이터 표식 보존, 삭제·초기화 없음 |
| 자동 그래픽 | CPU 렌더링 선택, 4096픽셀 검사 5회 통과 |
| 명시적 시스템 Vulkan | 기능 부족을 eligible=0/status4로 거부, abort 없음 |
| 공유 표면·읽기 | 30프레임씩 5회, 총150프레임 통과 |
| CPU·MMU·종료 | 5종 검사 통과 |
| 자체 화면 | 150개 시퀀스 중143개 표시, 6모드·최대 너비1280, 실패0 |
| 터치·도구 | 긴 터치·짧은 탭·트랙패드 대체·도구 클릭·정상 종료 통과, 네이티브 누름 간격118/119ms |
| 서비스 수명 | Activity 두 번 연결 시 같은 작업자 유지, 종료 후 포그라운드·웨이크락 해제 |
| 충돌 버퍼 | 검사 후0바이트 |
| 소스·포장 대응 | 네이티브54개 매핑(고정 패키지27/로컬 출력27), strip 재현, classes.dex·리소스·Manifest 정확히 재컴파일 |

배포 APK와 작업자는 일반 앱 UID로 실행했습니다. 디버깅을 끈 APK의 비공개 검사 보고서를 읽기 위해 **일회용 에뮬레이터에서만** root를 사용했습니다. 작업자를 root로 실행하거나 배포 앱 디버깅을 켠 것은 아닙니다. 공개 Python 기기 검사 도구는 run-as를 사용하므로 별도로 빌드한 디버그 APK가 필요합니다. 배포 APK 수령자는 앱의 **로그** 화면에서 검사 결과를 확인할 수 있습니다. 터치 검사는 예전 태블릿의 고정 해상도·한국어 버튼 가정을 없애 실제 화면 크기와 한국어·영어 UI를 읽습니다.

두 소스 묶음이 APK와 함께 제공됩니다. 의존성 입력94개·원본 Git 스냅샷4개를 검증했습니다. 외부 패키지의 과거 생산 커밋 보증이나 Termux/LLVM 전체 독립 소스 재빌드는 주장하지 않습니다. [소스 범위와 한계](apk-release.ko.md)를 유지합니다. 과거 캐시 시간 비교는 [v0.1.2 검사](graphics-verification.ko.md)이며 이번 APK의 새로운 성능 측정이 아닙니다.

## 게스트 없이 재현하기

게스트·설치 작업이 실행 중이지 않은 별도 ARM64 시험 기기에서 [설치·해시·서명 안내](apk-release.ko.md)를 따르세요. 아래 SERIAL을 실제 대상 값으로 바꿔 한 번에 하나씩 실행하고, 완료 후 앱의 **로그**를 엽니다.

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_compatibility_test true --es boot_gpu auto
```

시험 에뮬레이터에서는 `COMPATIBILITY_RESULT eligible=1 backend=software`, `REIMS_FRAME_RESULT failures=0`을 기대합니다. 다른 하드웨어는 다른 백엔드를 선택하거나 검사를 거부할 수 있습니다. 반복 읽기·CPU 검사 명령은 [기존 디스크 없는 재현 절차](graphics-verification.ko.md)를 따르되 APK는 이번 버전을 사용합니다. 큰 화면 검사는 다음과 같습니다.

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_frame_test true
```

`FRAME_TRANSPORT_BITMAP_RESULT failures=0`과 `max_width=1280`을 확인합니다. 호스트에서는 JDK를 준비하고 `python3 android/check_response.py`를 실행합니다. 진단용 제한 시간은 일반 게스트 실행의 시간 제한이 아닙니다.

## 미검증 — unknown

최초 배포 검사 당시 실기기·macOS 게스트는 시험하지 못했습니다. 이후 2026-11-17 동일 공개 APK로 TB373FU 한 대의 기존 macOS 15를 실행해 [실제 녹화와 화면](device-capture.ko.md)을 추가했습니다. 새 자동 설치·장기 안정성 검사는 아닙니다. 실제 게스트 속도, 장시간 안정성, 외부 USB·Bluetooth 입력, 과거 태블릿 재부팅 원인, 16K 실행 페이지, 다른 기기·GPU, 새 자동 설치 전체 과정, macOS26/27은 미검증입니다. 자체 작성 에뮬레이터 검사 통과로 실사용 수준을 보증하지 않습니다.
