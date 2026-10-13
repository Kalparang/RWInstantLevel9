# 개발 및 재빌드

이 저장소에는 모드의 소스, 빌드에 필요한 MinHook 소스, 테스트와 유지보수 문서가 포함되어 있습니다. 게임 실행 파일, 게임 리소스, 실제 세이브는 포함하지 않습니다. 모드를 사용하는 데에는 루트의 `winhttp.dll`만 필요합니다.

## 파일 구성

| 경로 | 역할 |
| --- | --- |
| [`src/mod.cpp`](../src/mod.cpp) | 지원 EXE 해시·명령어 검증, 훅 설치, XP 콜백, 런타임 로그 |
| [`src/mastery_policy.h`](../src/mastery_policy.h) | 게임 API와 분리된 XP 지급량 계산 및 입력 검증 |
| [`src/mastery_hook.asm`](../src/mastery_hook.asm) | 중간 훅의 레지스터 보존 및 원래 명령어로 복귀 |
| [`src/proxy.cpp`](../src/proxy.cpp), `proxy.asm`, `winhttp.def`, `winhttp_names.inc` | 시스템 WinHTTP export 전달 |
| [`src/winhttp_exports.json`](../src/winhttp_exports.json) | 프록시 생성에 사용한 시스템 DLL 해시 및 export 이름·ordinal 목록 |
| [`third_party/minhook`](../third_party/minhook) | MinHook v1.3.4 및 라이선스, x64 디코더 |
| [`tests`](../tests) | 네이티브 훅·프록시 테스트와 합성 프로필 검사 테스트 |
| [`tools/build.ps1`](../tools/build.ps1) | Release 빌드, 자동 테스트, `dist` 출력 |
| [`tools/generate_proxy.py`](../tools/generate_proxy.py) | 로컬 Windows의 WinHTTP export에서 프록시 파일 생성 |
| [`tools/inspect_profile.py`](../tools/inspect_profile.py) | 명시한 프로필 파일을 읽기 전용으로 검사·비교 |
| [`tools/update_checksums.py`](../tools/update_checksums.py) | 공개 파일 체크섬 생성 및 확인 |

후킹 근거와 ABI는 [ARCHITECTURE.md](ARCHITECTURE.md), 게임 업데이트 대응과 검증 순서는 [UPDATING.md](UPDATING.md)를 참고하세요. MinHook 라이선스 원문은 [THIRD_PARTY_NOTICES.txt](../THIRD_PARTY_NOTICES.txt)에도 포함되어 있습니다.

## 빌드 환경

- Windows x64
- Visual Studio 2022의 C++ 데스크톱 개발 도구, MSVC x64, MASM, Windows SDK
- CMake 3.22 이상과 CTest
- Python 3.10 이상; Python 패키지 추가 설치는 필요하지 않습니다.

`cmake`, `ctest`, `python`을 PATH에서 사용할 수 있는 PowerShell에서 저장소 루트를 현재 폴더로 두고 실행합니다.

```powershell
.\tools\build.ps1
```

도구가 PATH에 없으면 실행 파일 경로를 `-CMake` 및 `-Python`으로 지정할 수 있습니다. 기본 생성기는 `Visual Studio 17 2022`입니다. 다른 Visual Studio 생성기를 사용하려면 `-Generator`로 지정하세요. 이 스크립트는 Visual Studio용 `-A x64`를 사용합니다.

빌드 및 테스트 결과는 `build/`, 배포 후보 DLL과 그 체크섬은 `dist/`에 생성됩니다. 두 폴더는 Git에서 제외됩니다. `-BuildDirectory`와 `-DistDirectory`로 별도 출력 위치를 지정할 수도 있습니다. **빌드 스크립트는 루트의 검증된 배포 DLL을 교체하거나 게임에 설치하지 않습니다.**

런타임은 정적 CRT(`/MT`)를 사용합니다. 모드 사용자에게 Visual Studio, CMake, Python 또는 개발 파일을 설치하도록 요구하지 않습니다.

## 자동 검증

빌드 스크립트가 아래 두 단계를 모두 실행하며, 실패하면 배포 후보를 복사하지 않고 중단합니다.

```powershell
ctest --test-dir build -C Release --output-on-failure
python -B -m unittest discover -s tests -v
```

- `proxy_forwarding`: 현재 프록시의 92개 export 존재 및 16개 스레드의 WinHTTP 지연 초기화·인자 전달을 검사합니다. 로컬 세션만 열고 네트워크 요청은 보내지 않습니다.
- `mastery_native_hooks`: 실제 x64 detour/trampoline 1,000회, 레지스터·플래그·SSE 보존, 후처리 관찰 훅, XP 정책 경계 조건을 검사합니다. 현재 기준 11,042개 assertion입니다.
- Python: CRC, 클래스 레지스트리 변경, 잘못된 값, 읽기 전용 동작, 프로필 비교 등 16개 테스트입니다. 테스트 데이터는 코드에서 생성하며 실제 세이브를 사용하지 않습니다.

자동 테스트는 게임 외부에서 실행됩니다. 지원 버전의 실게임에서 XP 지급 및 저장을 검증하는 절차는 별도로 필요합니다. 기존 개인 개발 환경의 설치·제거 스크립트와 해당 통합 테스트는 이 공개 저장소에 포함하지 않습니다. 설치와 백업은 [README](../README.md)의 수동 절차를 따릅니다.

## 읽기 전용 프로필 확인

게임이 저장 중이지 않을 때 확보한 로컬 복사본을 검사하세요. 입력 경로는 필수이며 기본 게임 설치 경로는 없습니다.

```powershell
python -B .\tools\inspect_profile.py .\backups\after\Profile_1.ob
python -B .\tools\inspect_profile.py .\backups\after\Profile_1.ob --compare .\backups\before\Profile_1.ob --json
```

`backups/before`와 `backups/after`는 사용자가 따로 준비하는 예시 폴더입니다. 검사 도구는 세이브를 수정하거나 백업하지 않습니다. 출력에는 입력 파일의 절대 경로, SHA-256, 영웅별 진행도가 있으므로 원본 출력을 저장소나 이슈에 그대로 올리지 마세요. 표시 레벨은 저장값에서 추정하며 해금 전체나 게임 재실행 후 동작을 증명하지 않습니다. 영웅 GUID는 게임의 영웅 정의 식별자입니다.

## WinHTTP 프록시 재생성

생성된 파일은 이미 포함되어 있으므로 일반 빌드에는 이 단계가 필요하지 않습니다. Windows export 호환성을 변경할 때만 다음을 실행하고 생성 전후 차이를 검토하세요.

```powershell
python -B .\tools\generate_proxy.py
```

기본 입력은 현재 Windows의 System32 `winhttp.dll`이며 `--source`로 다른 입력, `--output`으로 출력 폴더를 지정할 수 있습니다. 시스템 DLL 자체는 배포하지 않습니다. export 이름과 ordinal은 입력 Windows 버전에 따라 달라질 수 있으므로 생성했다고 호환 범위가 넓어졌다고 판단하지 마세요. `proxy.asm`, `winhttp.def`, `winhttp_names.inc`, `winhttp_exports.json`은 같은 생성 결과를 함께 반영하고 프록시 테스트를 다시 실행합니다. 메타데이터에는 개인 경로 대신 입력 파일 이름만 기록합니다.

## 새 버전 배포

1. [UPDATING.md](UPDATING.md)에 따라 소스와 테스트를 수정하고 자동 검사 및 실게임 검증을 수행합니다. 세이브 전체를 미리 백업합니다.
2. `src/mod.cpp`의 시작 로그에 있는 모드 버전, README의 지원 게임 버전·EXE 해시, 구조 문서와 검증 범위를 함께 갱신합니다.
3. 검증한 후보만 `dist/winhttp.dll`에서 루트 `winhttp.dll`로 복사합니다. 컴파일러·SDK·빌드 시각에 따라 바이트 해시는 달라질 수 있으므로 기존 해시와 동일해야 재빌드 성공인 것은 아닙니다.
4. 아래 명령으로 DLL뿐 아니라 공개 소스·문서·라이선스까지 체크섬을 갱신하고 확인합니다. 매니페스트 자체는 해시 대상에서 제외됩니다.

```powershell
python -B .\tools\update_checksums.py
python -B .\tools\update_checksums.py --check
```

5. `git status --short`와 업로드할 파일 목록을 확인합니다. `.gitignore`는 이미 추적 중인 파일이나 수동으로 올리는 압축파일을 정리하지 않으므로 세이브, 로그, 덤프, 백업, 빌드 캐시, 개인 경로와 인증정보가 포함되지 않았는지 확인하세요. 체크섬 도구는 무결성 검사용이며 개인정보 검사나 패키지 생성 기능은 아닙니다.
6. 저장소에는 소스와 개발 문서를 유지하고, 모드 사용자용 Release 첨부 파일에는 `winhttp.dll`, 사용법, 해당 배포 파일의 체크섬 및 `THIRD_PARTY_NOTICES.txt`를 포함하면 됩니다. 파일 구성이 다른 패키지에는 그 구성에 맞는 체크섬을 별도로 생성하세요.

`.gitattributes`는 Git의 자동 줄바꿈 변환을 비활성화하여 체크아웃 후에도 파일 바이트와 체크섬을 유지합니다. `.gitattributes`와 `.gitignore`도 저장소에 함께 게시하세요. 편집기가 줄바꿈을 바꿨다면 변경을 검토한 후 매니페스트를 다시 생성해야 합니다.

현재 루트의 DLL은 기존 단독 플레이에서 XP 보정과 디스크 저장까지 확인한 배포본입니다. 이 공개 소스의 빌드 성공은 새로운 실게임 검증 결과를 뜻하지 않습니다. 재실행 후 유지, 보상 해금 전체, 협동 플레이는 별도 검증 전입니다.
