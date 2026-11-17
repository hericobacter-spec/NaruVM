# 그래픽 사전 검사 수정: v0.1.2-preview

[English guide](graphics-verification.md) · [APK 설치·검증](apk-release.ko.md)

## 관찰한 원인과 수정

2026-11-17, Android 35 ARM64 전용 에뮬레이터에서 공개된 v0.1.1-preview의
두 실패를 재현했습니다(observed). SwiftShader, 2 vCPU, RAM 3 GiB, 호스트 페이지
4 KiB 환경이며 macOS 디스크는 열지 않았습니다.

| 경로 | 관찰한 원인 | 구현한 수정 |
| --- | --- | --- |
| 시스템 Vulkan, 종료 코드 134 | SwiftShader에 `VK_EXT_external_memory_host`가 없어 기기 생성 후 게스트 RAM을 공유하는 과정에서 중단됐습니다. | Reims가 기기를 채택하기 전에 활성 확장 기능을 확인하고, 없으면 no-device/status 4로 정상 거부합니다. 기능이 있는 기기도 실제 메모리·픽셀 검사를 통과해야 합니다. |
| 소프트웨어, 종료 코드 132 | 에뮬레이터가 기본 SVE/SME 없이 파생 기능을 보고했고, LLVM이 실행 불가능한 SVE 명령 `index z3.s, #0, #1` (`0x04a14003`)을 생성했습니다. | 소프트웨어 자식 프로세스에만 적용하는 `getauxval` 보정 라이브러리가 모순된 파생 기능 비트만 제외합니다. 기능을 추가하지 않으며 일관된 보고는 유지합니다. |
| 반복 소프트웨어 시작 | Mesa가 접근할 수 없는 `/data/data/com.termux`에 캐시를 만들려다 디스크 캐시를 껐습니다. | 앱 전용 `mesa-shader-cache`를 사용하고 `MESA_SHADER_CACHE_MAX_SIZE=256M`으로 제한합니다. 사전 검사와 게스트 실행에 같은 설정을 연결했습니다. |

Android 시스템이나 다른 앱의 CPU 설정은 바꾸지 않습니다. 게스트 RAM을 별도 복사하는
대체 경로도 추가하지 않았습니다. 필요한 Vulkan
[호스트 메모리 가져오기 확장](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_external_memory_host.html)은
GPU 이름이나 Vulkan 버전만으로 판단할 수 없습니다. 명시적으로 선택한 백엔드의 실패 시
다른 백엔드로 바꾸지 않으며, 자동 모드는 게스트 시작 전 기존 모델별 순서로 검사합니다.
TB373FU의 자동/PanVK 정책은 유지합니다.

## 정확한 후보의 검사 결과 — observed, 에뮬레이터 1대

검사한 APK는 `NaruVM-v0.1.2-preview-arm64-v8a.apk`, 버전 코드 3이며 SHA-256은
`af5a2dd38fd4d16c637eae422b43d62feaa05348ee225189186ecc13fc73b17e`입니다.
[배포 기록](../android/publication/apk-release.json)에 구성·네이티브 해시·측정 원자료를 담았고,
[v0.1.1 기록](../android/publication/apk-release-v0.1.1.json)은 별도로 보존했습니다.

| 검사 | 관찰 결과 |
| --- | --- |
| 시스템 Vulkan 직접 선택 | 충돌 없이 no-device, 종료 코드 1 |
| 자동 호환성 검사 5회 | 소프트웨어 경로로 5회 통과, 매번 작성한 4,096개 픽셀 비교 |
| 공유 표면 가져오기·매핑·해제와 읽기 5회 | 총 150프레임, 크기 32/64/96, 초기 0값과 행 사이 여백 검증 |
| CPU·MMU·종료 | 네이티브 검사 5종 모두 통과 |
| 앱 실행 11회 중 Android 충돌 로그 | 없음 |
| 네이티브 출처·패키징 | 54개 전부 대응: 변경하지 않은 고정 패키지 27개 + 로컬 산출물 27개, strip 변환 재현 |
| Java·리소스·Manifest 독립 재컴파일 | APK 내용 일치, NDK libc++ 패키징도 일치; libc++ 소스 재빌드는 아님 |

이 제한된 자체 작성 검사는 macOS 로그인·바탕화면이나 장시간 사용 검증이 아닙니다.
실물 태블릿은 연결할 수 없었습니다. 실제 기기의 GPU 오류·재부팅·입력 반응,
16K 페이지 실행, 다른 GPU, 전체 자동 설치, macOS 26/27은 이 후보에서 미검증입니다.

## 캐시 효과 측정 — observed

후보 APK의 정확한 네이티브 파일과 포함된 CPU 보정 라이브러리를 사용했습니다.
매번 새 프로세스로 동일한 64×64 EFI 화면을 그리고 4,096개 픽셀을 비교했습니다.
예열 1회를 제외한 뒤 캐시 끔 5회와 예열된 캐시 사용 5회를 섞어 실행했습니다.
호스트 경과 시간에는 ADB 통신 비용도 포함됩니다.

| 모드 | 중앙값 |
| --- | ---: |
| `MESA_SHADER_CACHE_DISABLE=true` | 143.69ms |
| 예열된 디스크 캐시 사용 | 69.99ms |

관찰된 시간 감소는 51.29%, 캐시 크기는 약 2.3 MiB입니다. 작은 반복 그래픽 검사의
시작·렌더 작업을 측정한 것으로, macOS 부팅·프레임률·마우스 지연·앱 전체 속도나
네이티브에 가까운 성능을 뜻하지 않습니다. 게스트에도 같은 캐시 설정을 연결했지만
이번 검증에서 실제 게스트를 실행하지는 않았습니다.

## 디스크를 열지 않는 앱 검사 재현

중요한 게스트 데이터가 없는 별도 ARM64 Android 에뮬레이터·기기와 서명된 APK를
사용하세요. 필요한 ARM64 명령 지원 조건은 그대로 적용됩니다. `SERIAL`을 실제
기기 시리얼로 바꾸세요. 아래 명령은 검사 사이에 이 앱만 종료합니다.
게스트나 설치 작업이 실행 중일 때 사용하지 마세요. Apple 다운로드, macOS 설치,
앱 데이터 초기화나 게스트 디스크 열기는 하지 않습니다.

```sh
adb -s SERIAL install NaruVM-v0.1.2-preview-arm64-v8a.apk
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_compatibility_test true --es boot_gpu auto
```

같은 배포 서명의 기존 앱 업데이트는 `install -r`을 사용하세요. 서명이 다른 연구 앱을
강제 업데이트하려고 삭제하지 마세요. [서명·데이터 안내](apk-release.ko.md)를 참고하세요.
완료 후 **로그**에서 확인합니다. 검사한 에뮬레이터의 예상 결과는
`COMPATIBILITY_RESULT eligible=1 backend=software guest=none disk=none macos_tested=0`,
`REIMS_FRAME_RESULT failures=0`입니다. 실제 기기는 다른 백엔드로 통과하거나
기능 부족으로 정상 거부될 수 있습니다. 각 결과를 기다리며 5회 반복합니다.

일반 앱 UID에서 공유 표면과 화면 크기를 바꾸는 30회 읽기 검사를 실행하려면:

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_reims_test true --ez reims_software true --ez reims_surface true --ez reims_readback_stress true
```

`REIMS_SURFACE_RESULT failures=0`, `READBACK_C_API_RESULT failures=0 rounds=30`,
`REIMS_RESULT failures=0`을 확인하며 5회 반복합니다. CPU 검사 5종은:

```sh
adb -s SERIAL shell am force-stop dev.henge.research
adb -s SERIAL shell am start -n dev.henge.research/.MainActivity --ez run_tests true
```

`SUITES=5 FAILED=0`이 예상 결과입니다. 진단 작업에는 별도 실행 제한이 있지만,
일반 게스트 세션은 자동 시간 제한 없이 **Mac 중지**로 종료합니다.
기존 로그를 삭제하지 않고 충돌을 확인하려면:

```sh
adb -s SERIAL logcat -b crash -d
```

호스트 소스 검사는 `python3 tools/check_android_portability.py`로 실행합니다.
CPU 기능 검사는 실제 모순 보고, 유효한 기본 기능, 무관한 기능의 보존 및 기능을
추가하지 않는 조건을 검사합니다. Java 검사는 소프트웨어 프로세스에만 환경 설정이
적용되는지와 전용 캐시를 확인합니다. 실제 앱의 픽셀 검사와 함께 해석해야 합니다.
