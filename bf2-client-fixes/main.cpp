#include <print>
#include <string_view>
#include <type_traits>
#include <GameSpy/ghttp/ghttp.h>
#include <detours/detours.h>

LONG g_detours_error = 0;

//
// prevent URL abuse (sponsor URL allows for remote program execution)
// https://aluigi.altervista.org/adv/bf2urlz-adv.txt
//
template<typename T>
struct func_traits;

template<typename R, typename... Args>
struct func_traits<R(Args...)>
{
};

template<typename R, typename... Args>
struct add_fastcall_t;

template<typename R, typename... Args>
struct add_fastcall_t<func_traits<R(Args...)>>
{
    using type = R(__fastcall*)(Args...);
};

template<typename T>
using add_fast_call = typename add_fastcall_t<func_traits<T>>::type;

// ghidra revealed that ghttpSaveEx is using __fast_call calling convention which we need to follow to be able to detour it
typedef add_fast_call<decltype(ghttpSaveEx)> bf2_ghttpSaveEx_t;
auto bf2_ghttpSaveEx = reinterpret_cast<bf2_ghttpSaveEx_t>(0x59BD00);

GHTTPRequest __fastcall patched_ghttpSaveEx(const gsi_char* URL, const gsi_char* filename, const gsi_char* headers, GHTTPPost post, GHTTPBool throttle, GHTTPBool blocking, ghttpProgressCallback progressCallback, ghttpCompletedCallback completedCallback, void* param) {
    if (std::string_view(filename).contains("..")) {
        std::println("[gamespy] directory traversal attempt prevented {}", filename);
        return GHTTPInvalidURL;
    }

    // forward the call to the (at this point detoured) original ghttpSaveEx function
    return bf2_ghttpSaveEx(URL, filename, headers, post, throttle, blocking, progressCallback, completedCallback, param);
}

static_assert(std::is_same_v<bf2_ghttpSaveEx_t, decltype(&patched_ghttpSaveEx)>, "bf2 and patched ghttpSaveEx signature must match");

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
   
    if (::DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::println("[gamespy][bf2] attaching client-fixes");
        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((PVOID*)&bf2_ghttpSaveEx, patched_ghttpSaveEx);

        g_detours_error = DetourTransactionCommit();
        if (g_detours_error) {
            std::println("[gamespy][bf2] detours failed to commit - error {}", g_detours_error);
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching client-fixes");
        if (!g_detours_error) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach((PVOID*)&bf2_ghttpSaveEx, patched_ghttpSaveEx);
            g_detours_error = DetourTransactionCommit();

            if (g_detours_error) {
                std::println("[gamespy][bf2] detours failed to commit - error {}", g_detours_error);
            }
        }
    }

    return TRUE;
}