# NaruVM

[English](README.md)

Henge의 ARM64 JIT·소프트웨어 MMU·가상 장치를 Android NDK/Bionic 앱으로 옮긴 **독립 실험 포트**입니다. 그래픽은 Reims → metal2vulkan → Vulkan 경로를 사용합니다. 원본 Henge의 공식 Android 지원 제품은 아닙니다.

현재 공개 준비 범위는 **소스 코드**입니다. APK·Apple ROM/IPSW·게스트 디스크·계정·서명키는 공개 구성에 포함하지 않습니다. APK 배포에는 별도의 대응 소스·라이선스 검증이 남아 있습니다.

## 현재 상태

2026-11-16에 관측한 실기기는 Lenovo TB373FU 한 대입니다. 바탕화면·개발 도구 실행을 관찰했고, 최근 검사에서는 자동 Mali 경로로 로그인 화면·입력·수동 정상 종료를 확인했습니다. 간헐적 GPU 오류·태블릿 재부팅 원인과 느린 앱 반응은 해결되지 않았습니다. 일상 사용·네이티브 수준 속도·장기 안정성이 완성된 상태가 아닙니다.

Y700·스냅드래곤 갤럭시·다른 ARM64 태블릿과 휴대폰에는 공통 기능 검사·그래픽 선택 경로를 구현했습니다. **다른 실기기의 macOS 실행과 Adreno 가속은 미검증**입니다. 기종별 사양과 1차 출처는 [조사표](android/device-support.json)를 확인하세요. Y700은 세대/지역별 정확한 모델을 확인해야 합니다.

## 처음 확인하기

소스 루트에서 실행합니다. Python3.12+, C 컴파일러, JDK21이 필요합니다.

```sh
python3 tools/check_android_portability.py
python3 tools/check_publication.py
```

성공 표시는 `PORTABILITY_HOST_RESULT failures=0`과 `PUBLICATION_SOURCE_RESULT failures=0`입니다. 기기·ROM·네트워크 없이 정책·포장·소스 구성을 검사하며 macOS 부팅을 입증하지 않습니다. 코드/문서 수정 후 공개 체크아웃의 manifest가 오래됐다면 `python3 tools/export_android_source.py --refresh-manifest`로 갱신하고 실제 변경 목록을 검토하세요.

기본 진단 APK와 선택 그래픽 빌드의 준비물·명령·한계는 [빌드 안내](docs/building.md)에 있습니다. 현재 전체 빌드 도구는 Apple Silicon Mac 기준입니다. Linux 호스트 검사가 통과해도 Linux에서 전체 APK 빌드를 지원한다는 의미는 아닙니다.

## 실행 설정

기본 추천은 **2코어 / 게스트 RAM2048 MiB / JIT384 MiB**입니다. CPU·메모리·그래픽 선택은 다음 실행에 적용됩니다. 메모리가 적은 기기에서는 Android에 남길 여유가 부족할 수 있습니다.

앱의 **기기 호환성 검사 · 디스크 사용 안 함**은 CPU 명령어·페이지 크기·JIT 매핑과 공유 메모리 기반4096픽셀 결과를 검사합니다. 통과는 기초 조건만 확인한 것입니다. 고정 게스트 ISA에 없는 명령어 보완은 구현하지 않아 구형 ARM64 기기는 거절될 수 있습니다. 실기기 관측은4K페이지뿐이며16K는 빌드·정책 검사 범위입니다.

자동 선택은 TB373FU에서 기존 PanVK를 유지합니다. 다른 모델에서는 별도로 포함한 Turnip → 시스템 Vulkan → CPU 렌더링을 사전 검사합니다. 명시 선택이 실패하면 중단하며 VM 시작 후 자동 전환하지 않습니다. Turnip 실제 드라이버는 기본 포함하지 않고, 시스템 드라이버를 교체하지 않습니다. CPU 렌더링은 매우 느립니다. 정상 VM은 **Mac 종료**로 수동 종료하며 자동 실행 제한·재시작은 없습니다.

그래픽 빌드에는 실험용 다운로드·새 설치·첫 부팅 흐름이 연결되어 있습니다. 전체 자동 설치 검증은 아직 하지 않았습니다. 기본 진단 APK는 그래픽 실행을 포함하지 않습니다. [설치 안내](docs/building.md#download-install-and-first-boot)를 확인하세요.

## macOS 버전과 자동 설치

그래픽 빌드의 **다운로드·설치 후 실행**에서 버전을 선택합니다. 다운로드 →
크기·SHA-256 검증 → 별도 새 디스크 설치 → 첫 부팅이 연결된 실험 기능이며,
전체 자동 설치의 실기기 검증은 아직 하지 않았습니다. 계정 설정은 macOS에서
마칩니다. 기존 Mac은 새 설치가 성공하기 전까지 유지합니다. 중단한 다운로드는
이어받고, 중단한 설치 디스크는 보존하며 재시도 시 별도 새 디스크를 만듭니다.
앱 화면은 한국어·영어를 지원합니다.

| 버전 / 빌드 | Apple 다운로드 | Android 검증 범위 |
| --- | --- | --- |
| 15.6.1 / 24G90 | 15.66 GiB | 이전 태블릿 바탕화면 진입 관측; 새 전체 자동 설치 미검증 |
| 26.6.2 / 25G83 | 18.42 GiB | 실험 선택·다운로드·설치 경로; 실제 설치·부팅 미검증 |
| 27.2 / 26B123 | 23.84 GiB | 실험 선택·다운로드·설치 경로; 실제 설치·부팅 미검증 |

2026-11-16 Apple CDN 메타데이터를 관측했습니다. 선택 가능하거나 파일이 존재하는
것이 Android 호환성을 의미하지 않습니다. 여유 공간은 앱의 추정치보다 더 필요할
수 있습니다. 설치 중에는 앱을 열어 두세요. Android가 백그라운드 다운로드를
중단하면 앱에서 다시 시도해 이어받습니다. 일반 VM은 수동 종료하며 시간 제한이
없습니다. [설치 안내](docs/building.md#download-install-and-first-boot)와
[Apple 사용권 조건](docs/licensing.md)을 먼저 확인하세요.

## 외부 마우스와 키보드

Android가 인식한 USB·Bluetooth 마우스/키보드의 입력을 Mac 화면으로 전달하는 경로를 구현했습니다. Mac 화면을 눌러 포커스를 주면 키 입력을 받습니다. 마우스 이동·좌우/가운데 버튼·드래그·가로/세로 휠을 처리하며, 도구의 **외부 마우스 잠금 · Esc로 해제** (English: **Capture external mouse · Esc to release**)를 누르면 상대 이동을 사용합니다. Esc는 먼저 포착을 해제합니다.

Windows/Meta 키는 Command, Alt는 Option, Ctrl은 Control에 대응합니다. 한/영은 macOS 입력 소스를 따릅니다. Android가 먼저 사용하는 시스템 단축키는 Mac으로 전달되지 않을 수 있습니다. 일반 USB 장치를 macOS에 직접 연결하는 기능은 아닙니다.

관측 범위: 호스트 상태 검사와 TB373FU의 Android 작성 이벤트 검사를 통과했습니다. **물리 USB·Bluetooth 장치로 macOS를 조작하는 전체 경로는 아직 미검증**입니다. [입력 검증 방법](docs/building.md#external-input-verification)을 참고하세요.

## 라이선스와 참여

Henge/Android 변경의 GPL-2.0-or-later와 구성요소별 원본 고지를 유지합니다. [라이선스 검토](docs/licensing.md), [NOTICE](NOTICE), [구성요소 목록](android/publication/licenses.json)을 확인하세요. 오픈소스 라이선스는 Apple 소프트웨어 권한을 부여하지 않습니다. macOS Sequoia 약관에는 Apple 브랜드 시스템에 관한 제한이 있으므로 기기 소유만으로 macOS의 Android 사용권이 생긴다고 해석하지 마세요.

연구 APK 조합에는 원본이 허용한 GPLv3 선택지를 적용합니다. 외부 패키지 29개 항목의 버전과 일치하는 공식 레시피 및 소스 아카이브 해시를 기록했습니다. 바이너리를 실제로 생성한 소스와의 완전한 대응 검증은 별도이며, 이 소스 미리보기가 APK 배포용 전체 대응 소스를 뜻하지 않습니다. [의존 소스 수집 절차](docs/publication.md#dependency-source-input-collection)를 참고하세요.

[기여 안내](CONTRIBUTING.md) · [보안/문제 보고](SECURITY.md) · [구조](docs/architecture.md) · [변경 기록](CHANGELOG.md) · [게시 준비 절차](docs/publication.md)

[현재 APK 소스 대응 검사](docs/source-correspondence.md): 네이티브53개 출처와 Java·Manifest·리소스를 대조했습니다. 패키지 원본 일치와 전체 소스 대응 입증은 구분하며, APK 공개 배포 준비 완료를 뜻하지 않습니다.

[최종 소스 공개 검사](docs/publication.md#3-run-the-final-offline-source-gate)는 실제 Git 스테이징 내용, 호스트 검사, 실패 경계 검사와 동일한 소스 압축 파일 재생성을 검증합니다. CI도 같은 명령을 실행합니다. 소스 저장소 게시 준비와 APK 공개 배포 조건은 각각 판정합니다.
