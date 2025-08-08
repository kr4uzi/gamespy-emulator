#include <array>
#include <print>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#define _WINSOCK_DEPRECATED_NO_WARNINGS // gethostbyname is deprecated
#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <detours/detours.h>

//
// redirect all dns lookup requests to configured (+gamespy launch parameter) or autodetected gamespy emulator
//

bool g_redirect = false;
LONG g_detours_error = 0;
auto gs_gethostbyname = ::gethostbyname;
std::array<char, 4> g_redirect_address;
hostent* WINAPI redirect_gethostbyname(const char* name)
{
    if (!g_redirect || !name || !std::strstr(name, "gamespy")) {
        std::println("[gamespy] forwarding {}", name);
        return gs_gethostbyname(name);
    }

    static auto addr_list = std::array<char*, 2>{ g_redirect_address.data(), nullptr };
    thread_local auto host = std::string{ name };
    thread_local auto instance = hostent{
        .h_name = host.data(),
        .h_aliases = { nullptr },
        .h_addrtype = AF_INET,
        .h_length = sizeof(in_addr),
        .h_addr_list = addr_list.data()
    };

    std::println("[gamespy] redirecting {}", name);
    return &instance; 
}
static_assert(std::is_same_v<decltype(gs_gethostbyname), decltype(&redirect_gethostbyname)>, "gamespy and redirect gethostbyname signature must match");

bool detect_gamespy(const std::string_view& game)
{
    ::WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::println(std::cerr, "[gamespy] windows sockets failed to initialize");
        return false;
    }

    auto sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::println(std::cerr, "[gamespy] failed to create UDP socket");
        ::WSACleanup();
        return false;
    }

    struct _cleanup {
        decltype(sock)& _sock;
        ~_cleanup()
        {
            ::closesocket(_sock);
            ::WSACleanup();
        }
    } cleanup{ sock };

    BOOL broadcastEnabled = TRUE;
    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcastEnabled), sizeof(broadcastEnabled)) < 0) {
        std::println(std::cerr, "[gamespy] failed to enable UDP broadcast");
        return false;
    }

    DWORD timeout = 5000;
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)) < 0) {
        std::println(std::cerr, "[gamespy] failed enable timeout");
    }


    ::sockaddr_in broadcastAddr;
    std::memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(27900); // gamepsy master port
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    std::vector<char> message = { 9, 0, 0, 0, 0 };
    message.append_range(game);
    message.push_back(0);

    if (::sendto(sock, message.data(), static_cast<int>(message.size()), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        std::println(std::cerr, "[gamespy] failed to broadcast to gamespy master server (UDP:27900)");
        return false;
    }

    std::println("[gamespy] waiting for gamespy master server response...");
    char buffer[8];
    ::sockaddr_in responseAddr;
    int responseAddrLen = sizeof(responseAddr);
    if (::recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&responseAddr, &responseAddrLen) < 0) {
        std::println(std::cerr, "[gamespy] no response from gamespy master server");
        return false;
    }

    std::array<char, INET_ADDRSTRLEN> responseIP;
    ::inet_ntop(AF_INET, &responseAddr.sin_addr, responseIP.data(), responseIP.size());
    std::println("[gamespy] response received from {}", responseIP);
    std::println("[gamespy] redirecting all requests to {}", responseIP);
    std::memcpy(g_redirect_address.data(), &responseAddr.sin_addr, g_redirect_address.size());
    return true;
}

std::string to_utf8(const wchar_t* wstr)
{
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    auto str = std::string(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), len, nullptr, nullptr);
    str.resize(len - 1); // prevenet null terminator from begin part of the string (.size / .length)
    return str;
}

//
// inject redirect of all gamespy lookups
//
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
   
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::println("[gamespy] attaching redirect.dll");
        ::MessageBoxW(nullptr, L"attaching redirect.dll", L"Error", MB_OK);

        int argc = 0;
        std::string game = GAMESPY_GAMENAME;
        std::unique_ptr<LPWSTR[], decltype(&::LocalFree)> argv(::CommandLineToArgvW(::GetCommandLineW(), &argc), ::LocalFree);
        for (int i = 0; i < argc - 1; i++) {
            using namespace std::string_view_literals;
            if (argv[i] == L"gamename"sv) {
                game = to_utf8(argv[i + 1]);
            }
            else if (!g_redirect && argv[i] == L"+gamespy"sv) {
                if (::InetPtonW(AF_INET, argv[i + 1], g_redirect_address.data()) != 1) {
                    auto message = std::format(L"the gamespy address is invalid {}", argv[i + 1]);
                    ::MessageBoxW(nullptr, message.c_str(), L"Error", MB_OK);
                    return FALSE;
                }

                g_redirect = true;
            }
        }

        if (!g_redirect) {
            std::println("[gamespy] no address provided, trying to find local server...");
            if (!detect_gamespy(game)) {
                return TRUE;
            }

            g_redirect = true;
        }

        std::println("[gamespy] redirecting all gamespy lookups to {:d}.{:d}.{:d}.{:d}", g_redirect_address[0], g_redirect_address[1], g_redirect_address[2], g_redirect_address[3]);

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((PVOID*)&gs_gethostbyname, redirect_gethostbyname);
        g_detours_error = DetourTransactionCommit();
        if (g_detours_error) {
            std::println("[gamespy][detours] failed to commit - error ", g_detours_error);
            return FALSE;
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching redirect.dll");
        if (!g_detours_error) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach((PVOID*)&gs_gethostbyname, redirect_gethostbyname);
            g_detours_error = DetourTransactionCommit();
        }
        else {
            g_detours_error = 0;
        }

        if (g_detours_error) {
            std::println("[gamespy][detours] failed to commit - error ", g_detours_error);
            return FALSE;
        }
    }

    return TRUE;
}
