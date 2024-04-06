#include <cstdio>
#include <WinSock2.h>
#include <windows.h>
#include <detours.h>
#include <filesystem>
#include <optional>
#include <print>

std::FILE* g_Log = nullptr;
decltype(gethostbyname)* g_gethostbyname = ::gethostbyname;

static hostent* GetHostByName(_In_z_ const char FAR* name)
{
    std::println(g_Log, "GetHostByName: {}", name);
    return (*g_gethostbyname)(name);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;

    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (g_Log == nullptr)
        fopen_s(&g_Log, "test.log", "w");

    if (dwReason == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)g_gethostbyname, GetHostByName);
        auto error = DetourTransactionCommit();
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_gethostbyname, GetHostByName);
        auto error = DetourTransactionCommit();
    }

    return TRUE;
}