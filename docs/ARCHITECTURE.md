# 동작 구조와 현재 빌드 분석

이 문서는 `RWInstantLevel9 1.0.0`의 소스와 지원 게임 빌드에서 확인한 동작을 설명합니다. 빌드 방법은 [DEVELOPMENT.md](DEVELOPMENT.md), 게임 업데이트 대응 절차는 [UPDATING.md](UPDATING.md)를 참고하세요.

## 소스 역할

| 파일 | 역할 |
| --- | --- |
| [src/mod.cpp](../src/mod.cpp) | 호스트 판별, 실행 파일 검증, 훅 설치, 게임 메모리 접근, 진단 로그 |
| [src/mastery_policy.h](../src/mastery_policy.h) | 영구 경험치 보정 조건과 지급량 계산 |
| [src/mastery_hook.asm](../src/mastery_hook.asm) | 함수 중간 지점의 레지스터 보존과 C++ 콜백 연결 |
| [src/proxy.cpp](../src/proxy.cpp) | Windows 시스템 WinHTTP 로딩과 실제 export 주소 해석 |
| [src/proxy.asm](../src/proxy.asm), [src/winhttp.def](../src/winhttp.def), [src/winhttp_names.inc](../src/winhttp_names.inc) | WinHTTP export 전달 코드와 목록 |
| [tests/native_tests.cpp](../tests/native_tests.cpp), [tests/native_fixture.asm](../tests/native_fixture.asm) | 경험치 정책 및 실제 x64 훅/트램펄린 검증 |
| [tests/proxy_smoke.cpp](../tests/proxy_smoke.cpp) | export 존재와 병렬 WinHTTP 전달 검증 |

모드는 Windows x64 네이티브 DLL입니다. UE4SS나 별도의 스크립트 런타임은 사용하지 않습니다. MinHook을 정적으로 연결하며, 게임 실행 파일이나 리소스를 배포본에 포함하지 않습니다.

## 로딩과 검증 순서

게임 폴더의 `winhttp.dll`이 로드되면 WinHTTP API 호출은 실제 Windows 시스템 DLL로 전달됩니다. `proxy.cpp`는 `GetSystemDirectoryW`로 시스템 경로를 구하고 `LOAD_LIBRARY_SEARCH_SYSTEM32`로 원본 DLL을 로드합니다. `InitOnceExecuteOnce`와 원자적 포인터 게시로 지연 초기화를 보호합니다. 현재 목록은 92개 export입니다. 필수 export를 찾을 수 없으면 임의 반환값을 만들지 않고 명시적인 로더 오류 예외를 발생시킵니다.

게임 훅 초기화는 호스트 이름이 `Ravenswatch.exe`일 때만 시작합니다. `DllMain`은 초기화 스레드를 만들고, 아래 작업은 로더 잠금 밖에서 수행합니다.

1. 모드 DLL 옆에 `RWInstantLevel9.log`를 열고 비활성화 표식 `RWInstantLevel9.disabled`를 확인합니다.
2. 실행 중인 호스트의 실행 파일 SHA-256을 지원 해시와 비교합니다.
3. 로드된 EXE 기준 주소에 각 RVA를 더해 두 훅 위치의 명령어 바이트를 비교합니다.
4. 두 MinHook 트램펄린을 생성하고 함께 활성화합니다. 설치 실패 시 훅을 비활성화하고 MinHook을 정리합니다.
5. 준비 완료 시 `READY`를 기록합니다. 게임 객체는 경험치 지급 시점의 콜백에서 읽으며 초기화 스레드가 주기적으로 읽지 않습니다.

실행 파일 해시 또는 명령어가 다르면 `REFUSED`와 함께 게임 훅 설치를 중단합니다. 이 검증은 특정 바이너리에 대한 지원 범위를 제한하며, 새로운 바이너리의 ABI나 자료구조가 호환됨을 증명하지는 않습니다.

## 지원 바이너리와 분석 기준점

- 게임 버전: `1.06.00.04`
- Steam build: `25174305`
- `Ravenswatch.exe` SHA-256: `B9C3642884A8F3A463782F36C9A3A62BC14C88105315220C733FE6C2229BA904`

아래 주소는 모두 EXE 기준 **RVA**입니다. 실제 주소는 `GetModuleHandleW(nullptr) + RVA`이므로 ASLR로 기준 주소가 달라져도 같은 빌드에서는 사용할 수 있습니다.

| 대상 | 현재 값 | 의미 |
| --- | --- | --- |
| 영웅 프로필 vtable | `0xED1F30` | `oCDtHeroProfileData` 타입 확인 |
| 영구 성장도 필드 | 프로필 `+0x44`, `float` | `(표시 레벨 - 1) + 다음 레벨 진행률` |
| XP 임계값 배열 | 설정 `+0x138`, `uint16_t[9]` | 표시 레벨별 누적 XP 경계 |
| 표시 9레벨 누적 XP | 설정 `+0x146`, 배열 인덱스 `7` | 보정 지급량의 목표값 |
| 최대 표시 레벨 | 설정 `+0x14A`, `uint16_t` | 현재 정책은 정확히 `9`여야 허용 |
| 지급 전 훅 | `0x3ED3ED` | 이전 누적 XP와 지급 XP를 더하기 직전 |
| 반영 후 관찰 훅 | `0x3ED4BC` | 게임이 성장도를 반영한 뒤 결과 읽기 |
| 결과의 이전 누적 XP | 결과 객체 `+0xA70`, `uint16_t` | 관찰 로그의 `old_total` |
| 결과의 실제 지급 XP | 결과 객체 `+0xA72`, `uint16_t` | 관찰 로그의 `applied` |

현재 빌드에서 성장도 `0.0`은 표시 1레벨, `8.0`은 표시 9레벨입니다. 이 필드를 원시 XP로 해석하면 안 됩니다. 표시 레벨 계산은 `floor(progress) + 1`이며, 누적 XP로 변환할 때는 해당 구간의 임계값을 보간합니다.

재분석에 사용할 수 있는 추가 기준점은 다음과 같습니다. 함수명은 역분석에서 식별한 역할을 설명하는 이름이며 공식 공개 API가 아닙니다.

| RVA | 확인한 역할 |
| --- | --- |
| `0x2AC6F0` | `HeroRankGameLockConditionData` vtable 슬롯 14: 표시 영구 레벨 계산 |
| `0x31E2A0` | 정규화된 성장도를 설정 임계값에 따라 누적 XP로 변환 |
| `0x3EC140` | 점수 및 경험치 계산, 플레이한 영웅 탐색, 성장도 갱신, 결과/레벨 상승 처리 |
| `0x3ED790` | 결과 객체의 이전 누적 XP와 실제 지급량을 읽어 패널 갱신 |

## 지급량 정책

게임이 경험치를 지급할 때 before 훅은 `AdjustMasteryGain`을 호출합니다. 콜백은 프로필 vtable을 확인하고 `mastery::adjust`에 성장도, 기존 누적 XP의 하위 16비트, 원래 지급량, XP 곡선 및 최대 레벨을 전달합니다.

보정에는 다음 조건이 모두 필요합니다.

- 원래 지급 경험치가 양수입니다.
- 성장도가 유한하고 `0 <= progress < 8`입니다.
- 최대 표시 레벨이 `9`입니다.
- 처음 8개 임계값이 0보다 크고 엄격히 증가합니다.
- 성장도에서 다시 계산한 누적 XP와 전달받은 누적 XP의 차이가 1 이하입니다. 정수 변환 오차를 허용하기 위한 조건입니다.
- 기존 누적 XP가 9레벨 임계값 미만이고 원래 지급량이 `65535` 이하입니다.

조건을 충족하면 `adjustedGain = thresholds[7] - current`를 반환합니다. 고정 배수를 곱하지 않고 9레벨까지 남은 양으로 대체하므로 네이티브 16비트 합산의 넘침을 피합니다. 원래 지급량이 남은 양보다 큰 경우에는 지급량이 줄어들 수도 있습니다.

경험치가 0이거나 이미 9레벨이면 원래 지급량을 그대로 반환합니다. 잘못된 성장도, 예상과 다른 최대 레벨, 비정상 곡선 또는 불일치하는 누적 XP도 원래 처리를 유지합니다. 메모리 접근 중 SEH 예외가 발생하면 `AdjustMasteryGain`은 원래 지급량을 반환합니다.

콜백은 세이브나 성장도 필드에 직접 값을 쓰지 않습니다. 보정된 지급량을 레지스터에 돌려주면 게임의 원래 코드가 성장도 변환, 결과 기록, 레벨 상승 관련 처리를 계속합니다.

## 함수 중간 훅의 ABI 계약

두 훅은 일반 함수 시작점에 들어가는 `CALL`이 아니라 함수 중간으로 들어가는 `JMP`입니다. 현재 위치에서 `RSP % 16 == 0`이라는 전제가 있습니다. `mastery_hook.asm`은 플래그, `RAX/RCX/RDX/R8/R9/R10/R11`, `XMM0..XMM5`를 저장하고 32바이트 shadow space를 확보한 뒤 C++ 콜백을 호출합니다. Win64 비휘발성 레지스터는 C++ 함수 호출 규약에 따릅니다.

지급 전 지점의 계약은 다음과 같습니다.

| 레지스터 | 의미 |
| --- | --- |
| `RSI` | 영웅 프로필 |
| `RDI` | XP 설정 객체 |
| `EAX` / `R11D` | 이전 누적 XP; 콜백에는 `EAX` 전달 |
| `R13D` | 원래 지급 XP; 콜백 반환값으로 교체 |

지급 전 원본 명령어는 다음 7바이트입니다.

```text
46 8D 04 28    lea r8d, [rax+r13]
45 33 ED       xor r13d, r13d
```

훅은 `R13D`만 의도적으로 바꾼 다음 상태를 복원하고 MinHook 트램펄린으로 점프합니다. 원본 `LEA`가 보정 지급량을 합산하고 원본 `XOR`가 `R13D`와 플래그를 설정합니다. 이 순서 자체가 현재 구현의 일부입니다.

관찰 훅은 `RSI`를 프로필, `R14`를 결과 객체로 전달합니다. 원본 검사 바이트는 `F3 0F 10 46 44 E8 9A 98 94 00`입니다. `ObserveMasteryCommit`은 성장도와 결과 객체의 값을 읽어 기록할 뿐 게임 데이터를 수정하지 않습니다.

before/after 관찰 연결은 프로필 포인터와 스레드 ID가 일치하는 **단일 pending 슬롯**을 사용합니다. 현재 분석한 동기식 지급 흐름에 맞춘 구현이며, 향후 재진입·병렬 지급이 생기면 관찰 기록이 덮어써질 수 있습니다. 로그 잠금이 게임 객체 수명이나 모든 지급 흐름의 동시성을 보장하지는 않습니다.

## 진단 로그와 관찰 한계

| 표시 | 해석 |
| --- | --- |
| `READY` | 지원 실행 파일과 두 훅 바이트를 확인하고 설치를 마침 |
| `AWARD` | 입력 성장도/기존 누적 XP/원래 및 보정 지급량/정책 판단 |
| `COMMIT` | 원본 코드가 반영한 성장도와 결과 객체의 지급량 |
| `PASS-THROUGH` | 해당 콜백에서 원래 지급량을 유지 |
| `REFUSED` | 초기화 또는 검증 실패로 훅 설치 거부 |
| `DISABLED` | 비활성화 표식으로 훅 설치 생략 |

`AWARD`의 `reason`은 `mastery::Reason`의 순서대로 `0=Boost`, `1=NoAward`, `2=AlreadyMax`, `3=InvalidProgress`, `4=WrongCap`, `5=InvalidCurve`, `6=InconsistentTotal`입니다. `COMMIT reached9=yes`는 관찰한 성장도가 정확히 `8.0`이었다는 뜻입니다. 이 로그만으로 디스크 저장이나 재실행 후 유지까지 입증할 수는 없습니다.

결과 객체 `+0x2B0`의 앞선 이벤트와 점수 계산 함수 반환값에는 원래 지급량이 남을 수 있습니다. 따라서 결과 화면 일부 요약은 보정 전 XP를 표시할 수 있으며 UI 전체의 일관성은 별도 확인 대상입니다. 현재 확인된 실행 검증 범위는 [UPDATING.md](UPDATING.md#현재-검증-범위)에 구분해 두었습니다.
