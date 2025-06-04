#include <array>
#include <print>
#include <string>
#include <type_traits>
#include <WinSock2.h>
#include <GameSpy/ghttp/ghttp.h>
#include <string_view>
#include <Windows.h>
#include <winternl.h>
#include <ws2tcpip.h>
#include <detours/detours.h>

//
// redirect all dns lookup requests to configured (+gamespy launch parameter) or autodetected gamespy emulator
//
auto bf2_gethostbyname = ::gethostbyname;
std::array<char, 4> redirect_address;
hostent* __stdcall redirect_gethostbyname(const char* name)
{
    static std::array<char*, 2> addr_list{ redirect_address.data(), nullptr };
    static thread_local std::string host{ name };
    static thread_local hostent instance{
        .h_name = host.data(),
        .h_aliases = { nullptr },
        .h_addrtype = AF_INET,
        .h_length = sizeof(in_addr),
        .h_addr_list = addr_list.data()
    };

    if (strstr(name, "gamespy")) {
        std::println("[gamespy] redirecting {}", name);
        return &instance;
    }
    else {
        std::println("[gamespy] forwarding {}", name);
    }

    return bf2_gethostbyname(name);
}

static_assert(std::is_same_v<decltype(bf2_gethostbyname), decltype(&redirect_gethostbyname)>, "bf2 and redirect gethostbyname signature must match");

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

bool detect_gamespy()
{
    std::println("[gamespy] autodetecting gamespy server");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::println("[gamespy] WSAStartup failed");
        return false;
    }

    auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::println("[gamespy] failed to create UDP socket");
        WSACleanup();
        return false;
    }

    BOOL broadcastEnabled = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnabled, sizeof(broadcastEnabled)) < 0) {
        std::println("[gamespy] failed to enable UDP broadcast");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    struct sockaddr_in broadcastAddr;
    std::memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(27900); // gamepsy master port
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    const char message[] = "\x09\0\0\0\0battlefield2";
    if (sendto(sock, message, sizeof(message), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        std::println("[gamespy] failed to broadcast to gamespy master server (UDP:27900)");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    std::println("[gamespy] waiting for gamespy master server response...");
    char buffer[8];
    struct sockaddr_in responseAddr;
    int responseAddrLen = sizeof(responseAddr);
    if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&responseAddr, &responseAddrLen) < 0) {
        std::println("[gamespy] no response from gamespy master server");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    char responseIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &responseAddr.sin_addr, responseIP, INET_ADDRSTRLEN);
    std::println("[gamespy] response received from {}", responseIP);
    std::println("[gamespy] redirecting all requests to {}", responseIP);
    std::memcpy(redirect_address.data(), &responseAddr.sin_addr, sizeof(redirect_address));

    closesocket(sock);
    WSACleanup();
    return true;
}

//
// redirect print to parent console and inject redirect + patch(es)
//
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
   
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fpOut = NULL;
            freopen_s(&fpOut, "CONOUT$", "w", stdout);

            FILE* fpErr = NULL;
            freopen_s(&fpErr, "CONOUT$", "w", stderr);
        }

        auto args = std::string{ GetCommandLineA() };
        const std::string_view param{ "+gamespy " };
        if (const auto gsStart = args.find(param); gsStart != std::string::npos) {
            const auto gsIP = args.substr(gsStart + param.length(), param.find(' ', gsStart + param.length()));
            if (inet_pton(AF_INET, gsIP.c_str(), redirect_address.data()) != 1) {
                std::println("[gamespy] invalid gamespy address");
                return FALSE;
            }

            std::println("[gamespy] redirecting all requests to {}", gsIP);
        }
        else if (!detect_gamespy())
            return FALSE;

        std::println("[gamespy] attaching redirector.dll");

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((PVOID*)&bf2_gethostbyname, redirect_gethostbyname);
        DetourAttach((PVOID*)&bf2_ghttpSaveEx, patched_ghttpSaveEx);
        auto error = DetourTransactionCommit();
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching redirector.dll");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach((PVOID*)&bf2_ghttpSaveEx, patched_ghttpSaveEx);
        DetourDetach((PVOID*)&bf2_gethostbyname, redirect_gethostbyname);
        auto error = DetourTransactionCommit();
    }

    return TRUE;
}