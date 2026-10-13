#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include "MinHook.h"
#include "mastery_policy.h"
#pragma comment(lib, "bcrypt.lib")

extern "C" {
void* g_mastery_trampoline = nullptr;
void* g_mastery_after_trampoline = nullptr;
void MasteryDetour();
void MasteryAfterDetour();
}
namespace {
constexpr wchar_t ExpectedHash[] = L"B9C3642884A8F3A463782F36C9A3A62BC14C88105315220C733FE6C2229BA904";
constexpr uintptr_t BeforeRva = 0x3ed3ed;
constexpr uintptr_t AfterRva = 0x3ed4bc;
constexpr uintptr_t ProfileVtableRva = 0xed1f30;
constexpr uint8_t BeforeBytes[] = {0x46,0x8d,0x04,0x28,0x45,0x33,0xed};
constexpr uint8_t AfterBytes[] = {0xf3,0x0f,0x10,0x46,0x44,0xe8,0x9a,0x98,0x94,0x00};
HMODULE selfModule = nullptr;
uintptr_t imageBase = 0;
HANDLE logHandle = INVALID_HANDLE_VALUE;
SRWLOCK logLock = SRWLOCK_INIT;
struct Pending {
    const void* profile;
    DWORD thread;
    uint32_t oldTotal;
    uint32_t originalGain;
    uint32_t adjustedGain;
    float before;
};
// This pipeline runs synchronously on the game thread. A lock protects logging
// correlation only; no game objects are ever read on a background thread.
Pending pending{};
SRWLOCK pendingLock = SRWLOCK_INIT;

void log(const char* fmt, ...) noexcept {
    if (logHandle == INVALID_HANDLE_VALUE) return;
    char body[1400], line[1600];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
    va_end(args);
    SYSTEMTIME now; GetLocalTime(&now);
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, GetCurrentThreadId(), body);
    if (n <= 0) return;
    AcquireSRWLockExclusive(&logLock);
    DWORD written;
    WriteFile(logHandle, line, static_cast<DWORD>(n), &written, nullptr);
    FlushFileBuffers(logHandle);
    ReleaseSRWLockExclusive(&logLock);
}

bool hashFile(const wchar_t* path, wchar_t (&out)[65]) noexcept {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        uint8_t buffer[65536], digest[32]; DWORD count = 0;
        bool readOk = true;
        for (;;) {
            if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) { readOk = false; break; }
            if (!count) break;
            if (BCryptHashData(hash, buffer, count, 0) < 0) { readOk = false; break; }
        }
        if (readOk && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
            constexpr wchar_t hex[] = L"0123456789ABCDEF";
            for (unsigned i=0; i<32; ++i) { out[2*i] = hex[digest[i] >> 4]; out[2*i+1] = hex[digest[i] & 15]; }
            out[64] = 0; ok = true;
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

uint32_t adjustChecked(const uint8_t* profile, const uint8_t* settings,
                       uint32_t current, uint32_t gained) noexcept {
    if (!profile || !settings || *reinterpret_cast<const uintptr_t*>(profile) != imageBase + ProfileVtableRva) {
        log("PASS-THROUGH invalid profile type"); return gained;
    }
    const float before = *reinterpret_cast<const float*>(profile + 0x44);
    const auto* curve = reinterpret_cast<const uint16_t*>(settings + 0x138);
    const auto cap = *reinterpret_cast<const uint16_t*>(settings + 0x14a);
    const auto decision = mastery::adjust(before, current & 0xffff, gained, curve, cap);
    log("AWARD before=%.9g old_total=%u earned=%u adjusted=%u target_total=%u cap=%u reason=%d",
        before, current & 0xffff, gained, decision.gain, curve[7], cap, static_cast<int>(decision.reason));
    AcquireSRWLockExclusive(&pendingLock);
    pending = {profile, GetCurrentThreadId(), current & 0xffff, gained, decision.gain, before};
    ReleaseSRWLockExclusive(&pendingLock);
    return decision.gain;
}

DWORD WINAPI initialize(void*) noexcept {
    wchar_t modulePath[32768], exePath[32768];
    DWORD n = GetModuleFileNameW(selfModule, modulePath, _countof(modulePath));
    if (!n || n >= _countof(modulePath)) return 0;
    wchar_t* slash = wcsrchr(modulePath, L'\\');
    if (!slash) return 0;
    wcscpy_s(slash + 1, _countof(modulePath) - (slash + 1 - modulePath), L"RWInstantLevel9.log");
    logHandle = CreateFileW(modulePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    log("START RWInstantLevel9 1.0.0; permanent hero XP only");
    wcscpy_s(slash + 1, _countof(modulePath) - (slash + 1 - modulePath), L"RWInstantLevel9.disabled");
    if (GetFileAttributesW(modulePath) != INVALID_FILE_ATTRIBUTES) {
        log("DISABLED marker exists; no hooks installed"); return 0;
    }
    n = GetModuleFileNameW(nullptr, exePath, _countof(exePath));
    if (!n || n >= _countof(exePath)) { log("REFUSED cannot locate executable"); return 0; }
    wchar_t digest[65]{};
    if (!hashFile(exePath, digest) || wcscmp(digest, ExpectedHash) != 0) {
        log("REFUSED executable SHA-256 mismatch; no hooks installed"); return 0;
    }
    imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* before = reinterpret_cast<void*>(imageBase + BeforeRva);
    auto* after = reinterpret_cast<void*>(imageBase + AfterRva);
    if (memcmp(before, BeforeBytes, sizeof(BeforeBytes)) || memcmp(after, AfterBytes, sizeof(AfterBytes))) {
        log("REFUSED live instruction signature mismatch; no hooks installed"); return 0;
    }
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) { log("REFUSED MinHook init: %s", MH_StatusToString(s)); return 0; }
    s = MH_CreateHook(before, MasteryDetour, &g_mastery_trampoline);
    if (s == MH_OK) s = MH_CreateHook(after, MasteryAfterDetour, &g_mastery_after_trampoline);
    if (s == MH_OK) s = MH_QueueEnableHook(before);
    if (s == MH_OK) s = MH_QueueEnableHook(after);
    if (s == MH_OK) s = MH_ApplyQueued();
    if (s != MH_OK) {
        log("REFUSED hook installation: %s", MH_StatusToString(s));
        MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize(); return 0;
    }
    log("READY supported EXE SHA-256 verified; before_RVA=0x3ed3ed after_RVA=0x3ed4bc; cap=9");
    return 0;
}
} // namespace

extern "C" uint32_t AdjustMasteryGain(const uint8_t* profile, const uint8_t* settings,
                                      uint32_t current, uint32_t gained) noexcept {
    __try { return adjustChecked(profile, settings, current, gained); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        log("PASS-THROUGH unexpected award context; original gain kept"); return gained;
    }
}

extern "C" void ObserveMasteryCommit(const uint8_t* profile, const uint8_t* report) noexcept {
    __try {
        Pending item{};
        AcquireSRWLockExclusive(&pendingLock);
        if (pending.profile == profile && pending.thread == GetCurrentThreadId()) {
            item = pending; pending = {};
        }
        ReleaseSRWLockExclusive(&pendingLock);
        if (!item.profile) return;
        const float after = *reinterpret_cast<const float*>(profile + 0x44);
        const uint16_t reportedOld = *reinterpret_cast<const uint16_t*>(report + 0xa70);
        const uint16_t reportedGain = *reinterpret_cast<const uint16_t*>(report + 0xa72);
        log("COMMIT before=%.9g after=%.9g old_total=%u applied=%u requested=%u reached9=%s",
            item.before, after, reportedOld, reportedGain, item.adjustedGain, after == 8.0f ? "yes" : "no");
    } __except (EXCEPTION_EXECUTE_HANDLER) { log("OBSERVE failed; game commit not modified"); }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = module;
        wchar_t host[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, host, _countof(host));
        const wchar_t* name = (n && n < _countof(host)) ? wcsrchr(host, L'\\') : nullptr;
        if (n && n < _countof(host) && name && _wcsicmp(name + 1, L"Ravenswatch.exe") == 0) {
            HANDLE thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
        }
    }
    // No unhook/unload work under the loader lock during process shutdown.
    return TRUE;
}
