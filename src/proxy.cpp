// Forward every WinHTTP export to the real DLL in the Windows system directory.
// No game hooks, network requests, or DLL entry point belong in this file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iterator>

namespace {
constexpr const char* kWinHttpNames[] = {
#include "winhttp_names.inc"
};
INIT_ONCE g_proxy_once = INIT_ONCE_STATIC_INIT;
HMODULE g_system_winhttp = nullptr;
DWORD g_proxy_error = ERROR_SUCCESS;
}

extern "C" {
// Naturally aligned pointer stores/loads are atomic on x64. MASM uses the fast
// read path; initialization publishes each pointer with an interlocked store.
FARPROC g_winhttp_procs[std::size(kWinHttpNames)] = {};
}

namespace {
BOOL CALLBACK InitializeProxy(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t path[32768];
    const UINT length = GetSystemDirectoryW(path, static_cast<UINT>(std::size(path)));
    constexpr wchar_t suffix[] = L"\\winhttp.dll";
    if (!length || length + std::size(suffix) > std::size(path)) {
        g_proxy_error = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        return FALSE;
    }
    CopyMemory(path + length, suffix, sizeof(suffix));
    g_system_winhttp = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_system_winhttp) {
        g_proxy_error = GetLastError();
        return FALSE;
    }
    for (size_t i = 0; i < std::size(kWinHttpNames); ++i) {
        const FARPROC address = GetProcAddress(g_system_winhttp, kWinHttpNames[i]);
        if (address) {
            InterlockedExchangePointer(
                reinterpret_cast<PVOID volatile*>(&g_winhttp_procs[i]),
                reinterpret_cast<PVOID>(address));
        }
    }
    return TRUE;
}

[[noreturn]] void MissingExport(unsigned int index, DWORD error) noexcept {
    OutputDebugStringA("RW-Exp: could not forward a required system WinHTTP export: ");
    if (index < std::size(kWinHttpNames)) OutputDebugStringA(kWinHttpNames[index]);
    OutputDebugStringA("\n");
    SetLastError(error);
    // A generic zero-return stub would violate functions with other return
    // conventions. Report the loader-style failure explicitly instead.
    const ULONG_PTR details[] = {index, error};
    RaiseException(0xC0000139L, EXCEPTION_NONCONTINUABLE, 2, details);
    TerminateProcess(GetCurrentProcess(), error);
    __assume(false);
}
}

extern "C" FARPROC ResolveWinHttpProc(unsigned int index) noexcept {
    const DWORD previous_error = GetLastError();
    if (index >= std::size(kWinHttpNames)) MissingExport(index, ERROR_PROC_NOT_FOUND);
    if (!InitOnceExecuteOnce(&g_proxy_once, InitializeProxy, nullptr, nullptr)) {
        MissingExport(index, g_proxy_error ? g_proxy_error : ERROR_DLL_INIT_FAILED);
    }
    const auto address = reinterpret_cast<FARPROC>(InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_winhttp_procs[index]), nullptr, nullptr));
    if (!address) MissingExport(index, ERROR_PROC_NOT_FOUND);
    SetLastError(previous_error);
    return address;
}
