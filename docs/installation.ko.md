# NaruVM 설치와 첫 실행

[English](installation.md) · [프로젝트 소개](../README.ko.md) · [개발자용 빌드 참고](building.md)

터미널을 사용할 수 있고 직접 Android 앱을 빌드해 자신의 기기에서 시험하려는
개발자를 위한 안내입니다. 2026-11-17 저장소 소스를 기준으로 작성했습니다.
별도 표시가 없는 명령은 **Apple Silicon Mac의 저장소 루트**에서 실행합니다.

## 1. 현재 배포 형태부터 확인하기

[v0.1.2-preview 릴리스](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.2-preview)에서
서명한 ARM64 APK와 두 소스 묶음을 제공합니다. 빌드 없이 설치하려면
[APK 확인·설치 안내](apk-release.ko.md)를 따른 뒤 아래 앱·자원 설정으로 진행하세요.
기존 v0.1은 소스 전용입니다. 아래에는 직접 빌드하는 절차도 설명합니다.
이번 후보는 에뮬레이터 설치를 관측했으며 실기기·게스트 재검증은 미실시입니다.

설치는 두 단계입니다. 먼저 **NaruVM Android 앱**을 설치하고, 그 안에서
**macOS 게스트**를 새로 설치합니다. APK 설치만으로 macOS가 설치되지는 않습니다.
새 자동 설치 기능의 처음부터 끝까지의 실기기 검증은 아직 하지 않았습니다.
이전 macOS 15 바탕화면 관측은 별도의 연구 설치에서 얻은 결과입니다.

| 빌드 구성 | 포함 기능 | 다음에 할 일 |
| --- | --- | --- |
| 기본 진단 | CPU·MMU 테스트, 복구·네트워크 연구 도구 | Android 호스트 검사. 그래픽 Mac 실행은 불가 |
| 그래픽 + 소프트웨어 | Reims·Metal→Vulkan 변환·CPU 렌더러 | 실험용 설치·그래픽 부팅. CPU 렌더링은 매우 느림 |
| 그래픽 + PanVK | 위 구성 + 소스에서 빌드한 앱 전용 Mali 드라이버 | 관측된 TB373FU 경로 재현. 모든 Mali 지원을 뜻하지 않음 |

모든 구성은 **같은 APK 파일명과 같은 로컬 서명키**를 사용합니다.
따라서 그 파일의 구성은 **마지막으로 성공한 빌드**에 따라 달라집니다.

공개 APK의 검증 범위와 서명키 주의 사항은 [APK 설치 안내](apk-release.ko.md)를 확인하세요.

## 2. 준비물과 기기 조건

| 항목 | 조건 또는 관측 한계 |
| --- | --- |
| 빌드 컴퓨터 | Apple Silicon Mac. Windows·Linux·Intel Mac 전체 APK 빌드는 미검증 |
| 기본 도구 | Python 3.12+, C 컴파일러, Git, Make, CMake 3.22+ |
| 그래픽 빌드 도구 | Metal 컴파일러가 있는 전체 Xcode, PATH의 `glslangValidator`. 연구 환경은 Xcode 27 |
| Android 앱 | `arm64-v8a`. 최소 Android 9 / API 28은 Manifest 조건이며 실기기 호환 보장은 아님 |
| CPU | 앱의 고정 게스트 ISA·JIT 검사를 통과해야 함. ARM64라는 이름만으로 판단 불가 |
| macOS 설치 메모리 | Android가 보고하는 전체 메모리 **4 GiB 이상**, low-memory 상태가 아니어야 함. 제품 표기의 ‘4 GB’보다 실제 보고량이 적을 수 있음 |
| 저장 공간 | 아래 설치 추정치 이상의 내부 앱 저장 공간. 다운로드·미완료 디스크도 공간을 사용 |
| 네트워크 | Apple 다운로드·복구 요청을 위한 Android 인터넷 연결 |

실제 게스트 관측은 **Lenovo TB373FU 한 대 / Android 16 / 4K 페이지**에 한정됩니다.
Y700·스냅드래곤 갤럭시·다른 ARM64 기기, Adreno 가속, 16K 실기기 실행은 미검증입니다.
정확한 기종은 [기기 조사표](../android/device-support.json)를 확인하세요.
이 절차는 루팅이나 시스템 GPU 드라이버 교체를 요구하지 않으며, CPU·JIT·용량·Apple
서명 실패를 우회하지 않습니다.

Apple ROM·IPSW·게스트 디스크는 포함하지 않습니다. 게스트 설치 전에
[라이선스 안내](licensing.md)를 읽어 주세요. 프로젝트의 오픈소스 라이선스가
Apple 소프트웨어 사용권을 부여하지는 않습니다.

## 3. 소스와 도구 준비

최신 문서와 소스는 `main`을 복제해 확인합니다.

```sh
git clone https://github.com/hericobacter-spec/NaruVM.git
cd NaruVM
python3 --version
cc --version
git --version
make --version
cmake --version
```

없는 기본 도구를 먼저 설치하세요. 기본 Mac 컴파일러는 Xcode Command Line Tools로
준비할 수 있지만, 그래픽 구성에는 Metal 도구가 있는 전체 Xcode가 필요합니다.
이미 Homebrew를 사용한다면 `brew install cmake glslang`으로 두 명령행 도구를 준비할
수 있습니다([CMake](https://formulae.brew.sh/formula/cmake),
[glslang](https://formulae.brew.sh/formula/glslang)). 그래픽 구성 전에는 다음 도구도
확인합니다.

```sh
xcodebuild -version
xcrun -sdk macosx --find metal
glslangValidator --version
```

저장소에 고정된 SDK·NDK·빌드 도구·ARM64 Mac JDK를 받고 복구 의존성을 준비합니다.

```sh
python3 tools/bootstrap_android.py
export JAVA_HOME="$PWD/.tools/jdk-21/Contents/Home"
export ANDROID_SDK_ROOT="$PWD/.tools/android-sdk"
python3 android/fetch_restore_deps.py
```

Mac에도 도구·소스·출력물을 위한 여러 GB의 여유가 필요합니다. 전체 최대 용량과
빌드 시간은 측정하지 않았습니다. 스크립트는 최신 버전을 임의로 선택하지 않고
고정 버전·체크섬을 검사합니다. 다른 리비전이나 수정된 체크아웃은 보존하고
거절합니다. 새 터미널에서도 위 `JAVA_HOME`·`ANDROID_SDK_ROOT`를 같은 값으로
설정하세요. 이 빌드 경로는 Android Studio·Gradle을 사용하지 않습니다.

선택적으로 호스트 검사를 실행합니다. 위 JDK와 C 컴파일러를 사용하며 앱 설치나
게스트 부팅은 하지 않습니다.

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
```

성공 표시는 `PORTABILITY_HOST_RESULT failures=0`,
`PUBLICATION_SOURCE_RESULT failures=0`입니다.

## 4. 빌드 구성 선택

### A. 기본 진단 앱

```sh
python3 android/build.py
```

앱의 **CPU 테스트 실행**을 확인할 수 있습니다. 그래픽 Mac 실행기가 없으므로
그래픽 호환성 검사와 설치 기능은 비활성화됩니다. 그래픽 게스트 설치·부팅을
시험하려면 B 또는 C 구성이 필요합니다.

### B. 그래픽 + CPU 렌더링

3단계 환경을 유지합니다. 아래 경로는 **fetch와 build 전에 모두 설정**하고
이후 그래픽 빌드에서도 같은 값을 유지합니다.

```sh
export HENGE_REIMS_SOURCE="$PWD/.tools/reims-android"
export HENGE_REIMS_TARGET="$PWD/android/build/reims-target"
export CARGO_BUILD_JOBS=4
python3 android/fetch_gpu_deps.py --reims
python3 android/build.py --reims --software
```

고정된 Reims·metal2vulkan 소스와 로컬 Rust 도구를 준비하고 CPU Vulkan 렌더러를
포함합니다. PanVK·Turnip은 포함하지 않습니다. 자동 후보가 없거나 검사에 실패하면
앱에서 **CPU 렌더링**을 명시적으로 선택하세요. 특히 **TB373FU의 자동 선택은
PanVK만 요구하며 자동으로 CPU로 바뀌지 않습니다.** 이 구성에서는 CPU 렌더링을
선택하거나 C를 빌드해야 합니다. CPU 렌더링 검사 통과가 실용적인 속도를 뜻하지는
않습니다.

### C. TB373FU PanVK 연구 구성

B의 준비·fetch 단계까지 완료합니다. Docker는 별도로 설치·실행하고 기존
Linux/arm64 컨텍스트를 준비하세요. 목록을 확인한 뒤 `YOUR_CONTEXT`를 실제 이름으로
바꿉니다.

```sh
docker context ls
python3 android/build_panvk_source.py --bootstrap --docker-context YOUR_CONTEXT --jobs 2
python3 android/build.py --reims --software --panvk-userptr-probe
```

첫 Python 명령이 고정 소스의 앱 전용 드라이버를 빌드하고, 마지막 명령이 APK에
넣습니다. 기존 `--panvk-userptr-probe`라는 플래그 이름을 유지하지만 포함된 ICD는
진단뿐 아니라 이 게스트 백엔드에도 사용됩니다.
TB373FU에서는 앱에서 **자동** 또는 **Mali PanVK**를 사용합니다. 다른
Mali 기기는 미검증입니다. 도구는 Docker를 대신 실행하거나 시스템 드라이버·펌웨어를
변경하지 않습니다. 증분 빌드용 컨테이너가 남으므로 생성 보고서와
[개발자 안내](building.md#panvk-and-optional-turnip)에서 정확한 컨테이너를 확인하세요.
고정 패키지가 없다는 오류를 임의 버전 교체로 넘기지 마세요.
Adreno의 선택적 경로에는 별도로 검증한 [Turnip 묶음](../android/turnip_bundle.py)이
필요하며 스냅드래곤·Y700이라는 이름만으로 지원이 확정되지는 않습니다.

## 5. Android 앱을 USB로 설치

어느 구성이든 성공하면 다음 파일이 생깁니다.

```text
android/artifacts/NaruVM-arm64-debug.apk
```

패키지 ID는 `dev.henge.research`이며 디버그 가능한 로컬 서명 앱입니다. 빌드가
APK 서명과 16K ZIP 정렬을 검사합니다. 업데이트를 위해
`.tools/research-debug.keystore`를 개인적으로 보관하고 공개하지 마세요.
표시 이름은 NaruVM이며 내부 패키지·`henge` 파일명은 기존 호환성과 이력을 위해
유지합니다.

Android에서 개발자 옵션·USB 디버깅을 켜고 데이터 케이블로 연결합니다. 잠금을
풀고 이 컴퓨터의 USB 디버깅을 허용하세요. 인식되지 않으면
[Android 공식 기기 연결 안내](https://developer.android.com/studio/run/device)를
확인합니다. Mac의 저장소 루트에서 실행합니다.

```sh
export ADB="$ANDROID_SDK_ROOT/platform-tools/adb"
"$ADB" devices -l
```

목록의 정확한 시리얼로 `DEVICE_SERIAL`을 바꿉니다. 상태가 `unauthorized`나
`offline`이 아니라 `device`여야 합니다.

```sh
export NARU_DEVICE='DEVICE_SERIAL'
"$ADB" -s "$NARU_DEVICE" install -r android/artifacts/NaruVM-arm64-debug.apk
"$ADB" -s "$NARU_DEVICE" shell am start -n dev.henge.research/.MainActivity
```

성공하면 `Success`가 표시되고 NaruVM이 열립니다. `-s`는 대상 기기 하나를 고르며,
`-r`은 **Android가 서명을 허용하는 경우** 데이터를 유지해 업데이트합니다.
[ADB 공식 문서](https://developer.android.com/tools/adb)를 참고하세요.
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`이면 원래 설치에 사용한 로컬 서명키를 찾아야
합니다. **서명 문제 때문에 앱 제거·저장 공간 초기화·게스트 파일 덮어쓰기를 하지
마세요.** 소스 릴리스에는 개인 게스트 데이터가 없습니다.

## 6. 호환성 검사와 자원 설정

홈에는 **실행**, **진단/검사**, **기록** 영역이 있습니다. 화면 폭에 따라 이름이나
배치가 조금 다릅니다. 앱은 기기 언어를 따르며 한국어·영어를 제공합니다.
앱별 언어를 지원하는 Android에서는 해당 시스템 설정으로 선택할 수 있습니다.

1. 실행 영역에서 빌드 구성에 맞는 그래픽과 **다음 그래픽 부팅**에 사용할 자원을
   선택합니다. 특히 TB373FU에서 B 구성을 쓴다면 검사 전에 CPU 렌더링을 고릅니다.

   | 설정 | 시작값 | 선택 범위 |
   | --- | --- | --- |
   | 게스트 CPU | 2코어 | 1 / 2 / 3 / 4 |
   | 게스트 RAM | 2048 MiB | 2048 / 3072 / 4096 MiB |
   | 그래픽 | 해당 드라이버가 포함된 자동. TB373FU에서 B 구성은 CPU 렌더링 명시 선택 | 자동 / CPU / 시스템 Vulkan / Mali PanVK / Adreno Turnip |
   | 외부 마우스 감도 | 1× | 0.5×–3× |
   | JIT 캐시 | 384 MiB | 현재 런타임 기본값. 홈에서 조절하는 할당 항목은 아님 |

2. Android에 남길 메모리를 확보하세요. 코어·RAM을 늘리면 빨라진다고 검증한 것은
   아닙니다. 복구 설치 자체는 현재 **1코어 / 2 GiB**로 실행하며 저장한 값은 설치
   뒤 그래픽 부팅에 사용합니다.
3. 그래픽 빌드의 진단 영역에서 **기기 호환성 검사**를 실행합니다. 게스트 디스크를
   열지 않고 CPU·JIT·페이지 조건과 작성된 공유 메모리 4096픽셀 결과를 검사합니다.
   선택된 백엔드와 결과를 기록하세요.

다른 모델의 자동 선택은 별도 포함한 Turnip → 시스템 Vulkan → 소프트웨어 순서로
검사합니다. TB373FU는 관측된 PanVK 후보만 유지합니다. 명시 선택은 실패 시 다른
방식으로 대체하지 않으며 게스트 시작 뒤에도 렌더러를 바꾸지 않습니다. 실패한
사전 검사를 먼저 해결하세요. 통과도 장기 안정성·macOS 전체 호환 보장은 아닙니다.

## 7. macOS 다운로드와 새 설치

**실행 → macOS 버전 → 다운로드·설치 후 실행**을 선택합니다. 기존 게스트가 있다면
**다른 Mac 설치**를 펼칩니다. 이 흐름에는 별도의 ROM·IPSW 수동 가져오기가 필요
없습니다. 고급 펌웨어·복구 버튼은 별도 연구 도구이며 필수 설치 순서가 아닙니다.

아래는 **소스에 고정된 카탈로그 값**입니다. 현재 Apple 서명 가능성이나 Android
지원을 보장하지 않습니다. 빈 캐시의 예상 여유 공간은 남은 이미지 다운로드 +
설치 여유분 + 2 GiB로 계산합니다.

| 버전 | 이미지 다운로드 | 빈 캐시에서 필요한 내부 여유 공간 약 | 실기기 근거 |
| --- | ---: | ---: | --- |
| 15.6.1 / 24G90 | 15.66 GiB | 49.7 GiB | 이전 태블릿 바탕화면 관측. 새 자동 설치 미검증 |
| 26.6.2 / 25G83 | 18.41 GiB | 60.4 GiB | 설치·부팅 미검증 |
| 27.2 / 26B123 | 23.84 GiB | 73.8 GiB | 설치·부팅 미검증 |

실제로는 앱이 표시하는 값을 확인하세요. 받은 파일·부분 다운로드에 따라 달라지고
실제 복구 쓰기가 추정치를 넘을 수 있습니다. ‘64 GB 기기’는 전체 용량이며 빈 공간이
아닙니다. 외장 SD 카드가 이 내부 경로의 공간을 자동으로 늘려 주지도 않습니다.
미완료 설치 디스크도 공간을 사용하고 재시도는 다른 디스크를 만들기 때문에 반복
실패 후에는 더 많은 여유 공간이 필요할 수 있습니다.

구현된 진행 단계는 다음과 같습니다.

1. Apple에서 부팅 ROM을 받아 검증합니다.
2. 선택한 Apple 복구 이미지를 받습니다. `.part` 파일은 재시도 시 이어받습니다.
3. 전체 크기와 SHA-256이 맞아야 복구 설치로 넘어갑니다.
4. 새 `machines/BUILD-UUID/` 식별 정보와 논리 크기 64 GiB의 sparse 디스크를 만듭니다.
   논리 크기와 Android에서 현재 실제 점유한 용량은 다릅니다.
5. 새 디스크로 복구 설치하고 쓰기 완료·성공을 기록합니다. 기존 게스트는 별도로
   유지하고 새 설치가 성공할 때까지 기존 선택을 유지합니다.
6. 앱이 화면 앞에 있으면 새 게스트를 자동 실행합니다. 아니면 앱으로 돌아옵니다.
   macOS 안에서 언어·국가·로컬 계정 설정을 마칩니다.

설치 중 앱을 열어 두고 전원을 충분히 확보하세요. 설치 소요 시간은 측정하지
않았으며 **일반 VM·자동 설치에는 실행 시간 제한이 없습니다.** 다운로드 100%는
설치 성공이 아닙니다. 복구 설치와 첫 부팅이 별도 단계입니다. 파일 검증 뒤에도
Apple 서명·네트워크·가상 장치 문제로 중단될 수 있습니다.

**설치 중단**은 취소를 요청하고 진행 중인 디스크 쓰기를 마칩니다. 다운로드는
저장된 부분을 이어받고 전체 파일을 검증합니다. 복구 설치 도중 중단한 디스크는
미완료 상태로 보존하며 재시도는 새 디스크를 만듭니다. 자동 재시도 루프는 없습니다.
현재 일반적인 게스트 가져오기·내보내기나 미완료 디스크 정리 관리자는 제공하지
않습니다. 오류를 해결하려고 앱 내부 파일을 임의로 지우지 마세요.

## 8. 첫 부팅 뒤 사용

- 게스트의 설정 도우미를 마칩니다. 로컬 Mac 계정과 Apple ID는 별개이며 본인이
  계정을 정합니다. 저장소에 준비된 계정·암호는 없습니다.
- 네트워크는 **Android 쪽 NAT·DNS를 사용하는 가상 유선 NIC**입니다. 실제 Wi-Fi
  무선 장치를 넘기는 구조가 아니므로 macOS Wi-Fi 목록이 비어 있다는 이유만으로
  Android 연결 장애로 판단하지 마세요. 바탕화면에 도달하면 웹 페이지로 게스트
  통신을 확인합니다.
- Mac 화면을 눌러 포커스를 줍니다. 직접 터치·트랙패드 모드가 있고 트랙패드
  모드는 밀어서 포인터를 움직입니다. 도구의 Tab·Return과 **입력 소스 전환(⌃Space)**을
  사용할 수 있습니다. 영문 입력에는 macOS에서 ABC 등 영문 입력 소스를 고릅니다.
  Android 키보드의 한/영 변경만으로 게스트 입력 소스가 바뀌지는 않습니다.
- USB·Bluetooth 키보드와 마우스는 Android에 연결합니다. **외부 마우스 잠금 ·
  Esc로 해제**로 상대 이동을 사용하고 1×에서 시작해 이동 거리가 짧으면 감도를
  조절하세요. Meta/Windows→Command, Alt→Option, Ctrl→Control입니다. Android가
  시스템 단축키를 먼저 가져갈 수 있습니다. 물리 HID→게스트 전체 검증은 아직
  하지 않았으며, 이 설명은 구현된 입력 처리 경로입니다. 일반 USB 전달은 아닙니다.
- 종료는 **Mac 종료**를 눌러 쓰기가 끝나기를 기다린 뒤 앱을 닫습니다. 다음에는
  **Mac 실행**으로 선택된 설치 디스크를 엽니다. 일반 세션의 자동 재시작·시간 제한은
  없습니다.

느린 반응, 기존 GPU 오류와 태블릿 재부팅 문제는 남아 있습니다. 작은 시험 작업에서
자원·백엔드 하나씩 바꾸어 비교하세요. 호스트 검사 통과·바탕화면 관측만으로
일상 사용 안정성이나 네이티브 속도를 입증하지는 않습니다.

## 문제 해결

| 증상 | 확인할 내용 |
| --- | --- |
| Releases에 APK가 없음 | v0.1의 현재 배포 형태입니다. 위 A/B/C 중 하나를 빌드합니다. 소스 압축 파일은 설치 APK가 아닙니다. |
| `Missing ... bootstrap_android.py` | 3단계와 현재 셸의 `JAVA_HOME`·`ANDROID_SDK_ROOT`, 고정 도구 파일을 확인합니다. |
| `metal`·`glslangValidator` 없음 | 그래픽 빌드 전에 전체 Xcode의 Metal 도구와 glslang을 준비합니다. Command Line Tools만으로는 부족합니다. |
| 소스 핀 불일치·체크섬 오류 | 충돌한 체크아웃·캐시를 보존하고 원인을 확인합니다. 검증 생략·핀 임의 교체로 넘기지 않습니다. |
| ADB 장치 없음·`unauthorized` | 데이터 케이블, 잠금 해제, USB 디버깅과 컴퓨터 허용을 확인하고 공식 연결 안내를 봅니다. |
| 업데이트 서명 불일치 | 기존 `.tools/research-debug.keystore`를 찾습니다. 앱 제거·데이터 초기화로 해결하지 않습니다. |
| 설치·호환성 검사 버튼 비활성화 | `--reims`로 빌드했는지, VM·설치·진단이 이미 실행 중인지 확인합니다. A 구성에는 그래픽 실행기가 없습니다. |
| CPU 명령어·JIT 거절 | 정확한 이유를 기록합니다. Android 브랜치는 빠진 게스트 ISA 명령어를 에뮬레이션하지 않습니다. |
| TB373FU 자동에서 PanVK 없음 | C 구성 또는 B 구성의 명시적 CPU 렌더링을 사용합니다. 이 모델의 자동 선택은 CPU로 바뀌지 않습니다. |
| Vulkan·공유 메모리 픽셀 검사 실패 | GPU 이름·Vulkan 버전만으로 지원을 판단할 수 없습니다. 실제 백엔드·드라이버 결과를 기록합니다. CPU 렌더링은 별도의 느린 실험입니다. |
| 메모리·저장 공간 부족 | Android 보고 메모리와 7단계 내부 여유 공간을 확인합니다. 다른 작업을 끝내되 기존 게스트 삭제로 검사를 우회하지 않습니다. |
| 백그라운드 다운로드 중단 | 앱으로 돌아와 재시도하고 화면 앞에 유지합니다. Android가 백그라운드 dataSync 작업을 중단할 수 있습니다. |
| 검정 화면·앱 종료 | 마지막 단계·백엔드·경과 시간·Android 자체 재부팅 여부를 기록합니다. 반복 부팅을 멈추고 로그를 확보합니다. 공통 해결책이 확립된 상태가 아닙니다. |
| Wi-Fi 목록 없음·영문 입력 불일치 | 8단계의 가상 유선 네트워크와 macOS 입력 소스를 확인합니다. |

## 문제 보고용 기록 저장

VM·진단 기록은 **기록 → 실행 기록 저장**으로 저장하고 개인 정보가 없는지 확인한
뒤 이슈에 첨부합니다. 설치기의 로그는 별도입니다. 설치가 중단·완료된 뒤 5단계의
ADB·기기 변수를 사용해 로컬로 복사할 수 있습니다.

```sh
"$ADB" -s "$NARU_DEVICE" exec-out run-as dev.henge.research cat files/last-install.txt > naruvm-install.txt
```

설치 실행이 끝나기 전에는 이 파일이 없을 수 있습니다. **실행 기록 저장** 버튼이
이 설치 로그까지 내보내는 것은 아닙니다. 앱 커밋·빌드 구성, 정확한 모델·SoC,
Android 버전·호스트 페이지 크기, CPU/RAM/그래픽, macOS 버전, 마지막 단계·경과 시간을
함께 적으세요. 게스트 종료·앱 종료·기기 전체 재부팅을 구분합니다. 시리얼·계정·암호·
서명키·게스트 디스크·Apple 이미지는 올리지 마세요.
[이슈 양식](https://github.com/hericobacter-spec/NaruVM/issues/new/choose)과
[기여 안내](../CONTRIBUTING.md)를 이용할 수 있습니다.

## 이 안내의 검증 범위

근거는 [빌드 안내](building.md)에 명시한 2026-11-16의 제한된 태블릿 시험과 준비된
환경의 빌드 검사입니다. 이번 문서 보완으로 새 기기 실행, 전체 자동 설치, 물리
입력 장치, 완전히 새 컴퓨터의 빌드 재현, 장기 안정성 또는 APK 배포가 검증된 것은
아닙니다.
