// Loads the proxy in a non-game host, verifies all named exports, and exercises
// lazy forwarding with a local WinHTTP session. It sends no network requests.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <iterator>
#include <thread>
#include <vector>
#include <atomic>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fwprintf(stderr, L"Usage: proxy_smoke <absolute proxy DLL path>\n");
        return 2;
    }
    const HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    const char* names[] = {
#include "../src/winhttp_names.inc"
    };
    for (const char* name : names) {
        if (!GetProcAddress(proxy, name)) {
            std::fprintf(stderr, "Missing export: %s\n", name);
            return 1;
        }
    }
    const auto open = reinterpret_cast<decltype(&WinHttpOpen)>(GetProcAddress(proxy, "WinHttpOpen"));
    const auto close = reinterpret_cast<decltype(&WinHttpCloseHandle)>(GetProcAddress(proxy, "WinHttpCloseHandle"));
    std::atomic<unsigned int> failures{0};
    std::atomic<unsigned int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < 16; ++i) {
        threads.emplace_back([&] {
            ready.fetch_add(1);
            while (!start.load()) SwitchToThread();
            // The fifth argument traverses the original caller's stack, while
            // the first four exercise the preserved argument registers.
            HINTERNET session = open(L"RW-Exp local proxy smoke", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session || !close(session)) failures.fetch_add(1);
        });
    }
    while (ready.load() != 16) SwitchToThread();
    start.store(true);
    for (auto& thread : threads) thread.join();
    if (failures.load()) {
        std::fprintf(stderr, "WinHTTP forwarding failures: %u\n", failures.load());
        return 1;
    }
    std::printf("PASS: %zu exports, 16 concurrent lazy initializations and local WinHTTP sessions\n",
                std::size(names));
    FreeLibrary(proxy);
    return 0;
}
