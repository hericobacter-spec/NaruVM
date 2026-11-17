# 실험용 APK 설치·소스·수정 안내

[English](apk-release.md) · [전체 설치 안내](installation.ko.md) · [빌드 참고](building.md) · [라이선스 검토](licensing.md)

## 다운로드와 확인

[v0.1.3-preview 릴리스](https://github.com/hericobacter-spec/NaruVM/releases/tag/v0.1.3-preview)는 [입력·프레임 개선](response-verification.ko.md)과 기존 [그래픽 수정](graphics-verification.ko.md)을 포함한 서명된 `arm64-v8a` 설치용 APK입니다. 모든 기존 태그·배포 파일은 보존합니다.

다음 파일을 같은 폴더에 받으세요.

- `NaruVM-v0.1.3-preview-arm64-v8a.apk`
- `NaruVM-v0.1.3-preview-source.tar.gz`: 프로젝트 소스·패치·빌드·설치 도구
- `NaruVM-v0.1.3-preview-dependency-sources.tar.gz`: 외부 소스, 고정된 Termux 전체 레시피·패치, NDK 런타임 및 Cargo·Mesa·Rust 입력
- `NaruVM-v0.1.3-preview-release.json`: 정확한 해시·서명 인증서·구성과 관측 결과
- `SHA256SUMS.txt`

모든 파일을 받은 뒤 macOS에서는 `shasum -a 256 -c SHA256SUMS.txt`, Linux에서는 `sha256sum -c SHA256SUMS.txt`로 확인합니다. GitHub의 자동 생성 소스 ZIP은 위 의존성 소스 묶음을 포함하지 않습니다.

Android SDK build-tools 36.0.0의 도구로 서명을 확인할 수 있습니다.

```sh
apksigner verify --verbose --print-certs NaruVM-v0.1.3-preview-arm64-v8a.apk
```

예상 인증서 SHA-256:
`d3f0ccbbc25d555d35a2867c2a0848fe7bfde9c0b8cefdd3e0ea3c71b63908be`

개인 서명키와 암호 파일은 배포하지 않습니다.

## 빌드 없이 설치

64비트 ARM Android 9/API 28 이상이 Manifest 조건입니다. 실제 CPU 명령어·GPU·백엔드 사전 검사를 별도로 통과해야 하며 ABI만 같다고 macOS 호환성이 입증되지는 않습니다. PanVK에도 문서화된 Android·GPU 조건이 적용됩니다.

Android에서 APK를 열고 해당 설치 앱의 권한을 허용하거나, USB 디버깅과 platform-tools로 설치합니다.

```sh
adb install NaruVM-v0.1.3-preview-arm64-v8a.apk
```

**NaruVM**을 열고 권장 시작값 **2코어 / RAM 2048 MiB**와 감지된 그래픽 백엔드를 확인한 뒤 디스크 없는 호환성 검사를 실행하세요. APK에는 macOS나 게스트 디스크가 없습니다. 게스트 다운로드·설치는 별도 명시적 작업입니다. [전체 설치 안내](installation.ko.md)의 자원·저장 공간 조건을 확인하세요. 새 자동 설치 전체 과정과 macOS 26/27은 미검증입니다.

**기존 연구 앱 주의:** 새 배포키와 기존 디버그키가 다르므로 Android가 같은 패키지 `dev.henge.research`의 업데이트를 거절합니다. 기존 앱을 삭제하거나 데이터를 지워 강제 설치하지 마세요. 게스트 데이터가 없어질 수 있습니다. 이번 미리보기는 별도 기기·일회용 에뮬레이터에서 사용하세요. 앞으로 같은 배포키·더 높은 버전 코드의 APK는 `adb install -r`로 업데이트할 수 있지만 이는 게스트 데이터 이관 도구가 아닙니다.

## 관측한 검증과 미검증 범위

[검증 기록](../android/publication/apk-release.json)과 릴리스 JSON은 정확한 파일별 결과를 기록합니다. 이번 후보는 별도 Android 35 ARM64 에뮬레이터에서 설치·첫 실행, 자동 그래픽 검사 5회, 공유 표면·읽기 검사 150프레임, CPU·MMU·종료 검사 5종을 통과했습니다(observed). 수령자의 LGPL 소스 수정·독립 키 설치는 v0.1.1-preview의 별도 과거 증거이며 이번 후보에서 반복하지 않았습니다. 배포 Manifest는 디버깅을 끄며 `allowBackup=false`를 유지합니다.

최초 배포 검사 당시 실기기를 연결할 수 없었습니다. 이후 동일 공개 APK로 TB373FU 한 대의 기존 macOS 15에서 [2026-11-17 실제 실행 촬영](device-capture.ko.md)을 추가했습니다. 이전 TB373FU 실행은 역사적 자료이며 새 서명 APK의 실기기 시험이 아닙니다. 새 macOS 설치, 다른 기기의 실물 GPU 호환성, 장시간 사용, 외부 키보드·마우스, 16K 페이지 실기기, 전체 자동 설치와 26/27은 미검증입니다. [그래픽 수정·캐시 비교 측정과 재현 절차](graphics-verification.ko.md)는 제한된 에뮬레이터 결과이며 macOS 전체 속도·실사용 안정성을 입증하지 않습니다.

## 소스 제공 범위와 재빌드 입증의 구분

APK와 같은 릴리스에서 **두 소스 묶음을 모두** 받으세요. 프로젝트 묶음은 `SOURCE-MANIFEST.json`이 있으며 바깥 폴더가 없으므로 새 빈 폴더에만 풉니다. 해당 소스 루트에서 의존성 묶음을 배치·검사할 수 있습니다.

```sh
mkdir -p .tools/publication-sources
tar -xzf /path/to/NaruVM-v0.1.3-preview-dependency-sources.tar.gz -C .tools/publication-sources
python3 android/package_publication_sources.py --verify /path/to/NaruVM-v0.1.3-preview-dependency-sources.tar.gz --ndk-source --graphics
```

검사기는 소스 입력 94개와 원본 Git 스냅샷 4개의 구성·해시를 검사합니다. 기존 `complete_apk_source=0` 표시는 이 입력 묶음 자체의 범위이며 별도 릴리스 전체의 법적 판정이나 실행 결과가 아닙니다.

네이티브 출처 검사는 APK 파일 전부의 원본과 배포 strip 변환을 별도로 대조합니다. 변경하지 않은 Termux 라이브러리는 해시가 고정된 공식 패키지와 일치하며, 버전이 일치하는 소스·전체 빌드 레시피·패치를 함께 제공합니다. 역사적 바이너리 생산 커밋은 입증되지 않았고 Termux·LLVM 전체의 독립 소스 재빌드를 주장하지 않습니다. 기존 엄격한 소스 검사의 `--require-complete-source` 종료 코드 2를 성공으로 바꾸지 않습니다. 새 기록은 이러한 한계와 소스 제공·포장·수정 설치의 실제 관측을 구분합니다.

[GNU FAQ](https://www.gnu.org/licenses/gpl-faq.html#MustSourceBuildToMatchExactHashOfBinary)는 대응 소스와 완전히 같은 해시의 재현을 구분합니다. 기술 검토는 [GPLv3 1·6절](https://spdx.org/licenses/GPL-3.0-only.html), [LGPLv3 4절](https://spdx.org/licenses/LGPL-3.0-only.html)을 참고합니다. 원본 파일별 허가·고지는 유지하며 법률적 인증을 주장하지 않습니다.

## 직접 빌드·서명

Apple Silicon Mac과 [고정된 SDK·NDK·JDK 및 그래픽 빌드 준비](building.md#optional-graphics-and-software-renderer)가 필요합니다. 기존 빌드기는 도구 다운로드·준비된 캐시를 사용하므로 완전 오프라인·새 컴퓨터 전체 빌드 재현 주장은 아닙니다.

직접 만든 비공개 PKCS12 키 저장소와 암호 텍스트 파일을 준비하세요. 저장소·키 암호는 같아야 하고 두 파일은 소유자만 접근하도록 `chmod 600`을 적용합니다. 소스·배포 파일 밖에 보관하세요.

```sh
python3 android/build.py --gpu --reims --software --panvk-userptr-probe --release --version-name 0.1.3-preview --version-code 4 --keystore /private/path/recipient.p12 --ks-pass-file /private/path/password.txt --key-alias recipient --output /private/path/NaruVM-recipient.apk
```

기본 연구 빌드는 기존 별도 디버그키를 사용합니다. 배포 빌드는 서명 입력을 명시해야 하며 기존 출력 덮어쓰기를 거절하고 디버깅을 끄며 컴파일 경로를 치환하고 로컬 네이티브 심볼·디버그 데이터를 제거합니다. 게시자의 개인 키는 필요하지 않습니다.

## LGPL 구성요소를 바꾸어 설치

중요한 게스트 데이터가 없는 별도 에뮬레이터·기기를 사용하세요. 라이브러리 소스를 수정하고 같은 구성으로 네이티브 대상을 다시 빌드한 뒤 자신의 키로 APK를 빌드하면 됩니다. 한정된 검사는 `third_party/libplist/src/plist.c`의 `plist_new_string`에서 입력이 `NaruVM LGPL replacement probe`일 때만 `NaruVM recipient-modified LGPL library`로 치환하고 나머지 입력은 보존합니다.

정상 그래픽 빌드가 `android/build/native`를 구성한 뒤 정적으로 연결되는 해당 실행 파일을 재빌드합니다.

```sh
cmake --build android/build/native --target henge_lab -j 2
```

기존 APK에서 해당 네이티브 파일만 교체하여 자신의 키로 서명할 수도 있습니다. 이 파일은 Android 포장 때문에 `.so` 이름을 가진 ELF 실행 파일입니다.

```sh
python3 android/repackage_apk.py --apk /path/to/NaruVM-v0.1.3-preview-arm64-v8a.apk --replace-native libhenge_lab.so=android/build/native/bin/libhenge_lab.so --release --keystore /private/path/recipient.p12 --ks-pass-file /private/path/password.txt --key-alias recipient --output /private/path/NaruVM-modified.apk
adb install /private/path/NaruVM-modified.apk
```

도구는 선택한 네이티브 파일·서명 메타데이터만 바꾸고 나머지 앱·고지는 보존합니다. 새 파일명·ARM64가 아닌 교체 파일을 거절하고 정렬·서명을 검사합니다. 별도 SDK는 `--sdk`, 별도 JDK는 `JAVA_HOME`을 지정하세요.

수정 APK에서 `lib/arm64-v8a/libhenge_lab.so`를 추출하여 에뮬레이터 `/data/local/tmp/naruvm-recipient-worker`에 보내고 `chmod 755` 후 `license-probe` 인자로 실행하면 `LGPL_REPLACEMENT_VALUE`와 라이브러리 반환값이 출력됩니다. 단순 재서명이 아닌 소스 동작 변경을 구분하는 검사이며 Apple 이미지·게스트 계정·루트·게시자 키는 필요하지 않습니다. macOS 부팅·안정성 검사는 아닙니다.

## 기존 공개 APK 업데이트

v0.1.1-preview·v0.1.2-preview는 같은 공개 배포 키를 사용합니다. `adb install -r NaruVM-v0.1.3-preview-arm64-v8a.apk`로 업데이트하세요. v0.1.2 → v0.1.3 업데이트에서 앱 UID와 자체 작성 데이터 표식 보존을 한 에뮬레이터에서 확인했습니다(observed). 모든 게스트 데이터의 마이그레이션 검증은 아닙니다. 디버그 키가 다른 기존 연구 앱에는 적용하지 마세요. [추가 프레임·입력·서비스 검증과 수집 방식](response-verification.ko.md).
