#include <array>
#include <print>
#include <iostream>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <type_traits>
#include <stdexcept>
#include <filesystem>
#include <map>
#include <regex>
#include <tuple>
#define _WINSOCK_DEPRECATED_NO_WARNINGS // gethostbyname is deprecated
#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <detours.h>
#include "exports.h"

//
// redirect all dns lookup requests to configured (+gamespy launch parameter) or autodetected gamespy emulator
//
bool should_redirect(const char* name)
{
    return name && (std::strstr(name, "gamespy") || std::strstr(name, "dice.se"));
}
LONG g_detours_error = -1;
auto gs_gethostbyname = ::gethostbyname;
std::array<char, 4> g_redirect_address;
hostent* WINAPI redirect_gethostbyname(const char* name)
{
    if (::should_redirect(name)) {
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

    std::println("[gamespy] forwarding {}", name ? name : "(null)");
    return gs_gethostbyname(name);  
}
static_assert(std::is_same_v<decltype(gs_gethostbyname), decltype(&redirect_gethostbyname)>, "gamespy and redirect gethostbyname signature must match");

auto gs_getaddrinfo = ::getaddrinfo;
int WINAPI redirect_getaddrinfo(const char* name, const char* service, const addrinfo* hints, addrinfo** result)
{
    if (should_redirect(name))
    {
        std::println("[gamespy] redirecting {}", name);

        char ipbuf[INET_ADDRSTRLEN] = {};
        if (!::inet_ntop(AF_INET, g_redirect_address.data(), ipbuf, sizeof(ipbuf))) {
            return EAI_FAIL;
        }

        return gs_getaddrinfo(ipbuf, service, hints, result);
    }

    std::println("[gamespy] forwarding {}", name ? name : "(null)");
    return gs_getaddrinfo(name, service, hints, result);
}
static_assert(std::is_same_v<decltype(gs_getaddrinfo), decltype(&redirect_getaddrinfo)>, "gamespy and redirect getaddrinfo signature must match");

std::string to_utf8(const wchar_t* wstr)
{
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    auto str = std::string(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), len, nullptr, nullptr);
    str.resize(len - 1); // prevenet null terminator from begin part of the string (.size / .length)
    return str;
}

class WindowsError : std::exception
{
    std::wstring m_Message;
    std::string m_MessageUTF8;

public:
    WindowsError(DWORD error)
        : m_Message(ErrorMessageFromErrorCode(error)), m_MessageUTF8(to_utf8(m_Message.c_str()))
    {

    }

    virtual const wchar_t* wwhat() const noexcept          { return m_Message.c_str();     }
    virtual const char*     what() const noexcept override { return m_MessageUTF8.c_str(); }

    static std::wstring ErrorMessageFromErrorCode(DWORD errorCode)
    {
        auto messageFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        auto langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
        wchar_t* messageBuffer = nullptr;
        auto msgLength = FormatMessageW(messageFlags, nullptr, errorCode, langId, reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
        if (msgLength > 0) {
            auto bufferCleanup = std::unique_ptr<wchar_t, decltype(&::LocalFree)>{ messageBuffer, ::LocalFree };
            return messageBuffer;
        }

        return std::format(L"Failed to retrieve error message for code {}", errorCode);
    }
};

bool detect_gamespy(const std::string_view& game)
{
    auto sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        throw WindowsError(::WSAGetLastError());

    struct socket_cleanup {
        decltype(sock)& _sock;
        ~socket_cleanup() { ::closesocket(_sock); }
    } cleanup{ sock };

    BOOL broadcastEnabled = TRUE;
    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcastEnabled), sizeof(broadcastEnabled)) != 0)
        throw WindowsError(::WSAGetLastError());

    DWORD timeout = 5000;
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)) != 0) {
        // non-critical error
        std::println(std::cerr, "[gamespy] failed enable timeout");
    }

    ::sockaddr_in broadcastAddr;
    std::memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(27900); // gamepsy master port
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    std::vector<char> availablePacket = { 9, 0, 0, 0, 0 };
    availablePacket.append_range(game);
    availablePacket.push_back(0);

    if (::sendto(sock, availablePacket.data(), static_cast<int>(availablePacket.size()), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0)
        return false;

    std::println("[gamespy] waiting for gamespy master server response...");
    char buffer[8];
    ::sockaddr_in responseAddr;
    int responseAddrLen = sizeof(responseAddr);
    if (::recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&responseAddr, &responseAddrLen) < 0)
        return false;

    static_assert(sizeof(g_redirect_address) == sizeof(decltype(responseAddr)::sin_addr), "IP Address Length Missmatch");
    std::memcpy(g_redirect_address.data(), &responseAddr.sin_addr, g_redirect_address.size());
    return true;
}

//
// inject redirect of all gamespy lookups
//
bool detour_gethostbyname()
{
    ::WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        throw WindowsError(::WSAGetLastError());

    struct wsa_cleanup {
        ~wsa_cleanup() { ::WSACleanup(); }
    } cleanup;

    int argc = 0;
    std::string game = GAMESPY_GAMENAME;
    std::unique_ptr<LPWSTR[], decltype(&::LocalFree)> argv(::CommandLineToArgvW(::GetCommandLineW(), &argc), ::LocalFree);
    for (int i = 0; i < argc - 1; i++) {
        auto arg = std::wstring_view{ argv[i] };
        if (arg == L"+gamename") {
            game = to_utf8(argv[i + 1]);
        }
        else if (arg == L"+gamespy") {
            auto hints = ::ADDRINFOW{
                .ai_family = AF_INET,
                .ai_socktype = SOCK_DGRAM
            };
            ::ADDRINFOW* addrInfo = nullptr;
            if (auto res = ::GetAddrInfoW(argv[i + 1], nullptr, &hints, &addrInfo); res != 0)
                throw WindowsError(::WSAGetLastError());

            auto ipv4 = reinterpret_cast<sockaddr_in*>(addrInfo->ai_addr);
            static_assert(sizeof(g_redirect_address) == sizeof(std::remove_pointer_t<decltype(ipv4)>::sin_addr), "IP Address Length Missmatch");
            std::memcpy(g_redirect_address.data(), &ipv4->sin_addr, g_redirect_address.size());
            ::FreeAddrInfoW(addrInfo);
        }
    }

    if (g_redirect_address == decltype(g_redirect_address){}) {
        std::println("[gamespy] no address provided, trying to find local server...");
        if (!detect_gamespy(game)) {
            auto res = ::MessageBoxA(nullptr, "Online Service not available. Continue?", "Error", MB_OKCANCEL | MB_ICONQUESTION);
            return res == IDOK;
        }
    }

    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&gs_gethostbyname, redirect_gethostbyname);
    DetourAttach(&gs_getaddrinfo, redirect_getaddrinfo);
    g_detours_error = DetourTransactionCommit();
    if (g_detours_error) {
        std::println("[gamespy][detours] failed to commit - error ", g_detours_error);
    }

    return g_detours_error == 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
   
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::println("[gamespy] attaching redirect dll");

        try {
            if (!detour_gethostbyname())
                return FALSE;

            auto responseIP = std::string(INET_ADDRSTRLEN, 0);
            ::inet_ntop(AF_INET, g_redirect_address.data(), responseIP.data(), responseIP.size());
            std::println("[gamespy] redirecting all requests to {}", responseIP);
        }
        catch (const WindowsError& e) {
            std::wcout << L"[gamespy] error attaching redirect dll: " << e.wwhat() << std::endl;
            ::MessageBoxW(nullptr, e.wwhat(), L"GameSpy Initialization Error", MB_OK);
        }
        catch (std::exception& e) {

            std::println(std::cerr, "[gamespy] error attaching redirect dll: {}", e.what());
            ::MessageBoxA(nullptr, e.what(), "GameSpy Initialization Error", MB_OK);
        }

        std::error_code ec;
		std::vector<std::filesystem::path> unorderdDlls;
        std::map<unsigned, std::filesystem::path> dlls;
        auto options = std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied;
        auto [pattern, unordered] = [] {
            if constexpr (std::is_same_v<std::filesystem::path::value_type, char>)
                return std::tuple(std::regex{ ".*-(\\d+)-inject\\.dll", std::regex::icase }, "inject.dll");
            else if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>)
				return std::tuple(std::wregex{ L".*-(\\d+)-inject\\.dll", std::regex::icase }, L"inject.dll");
            else
                static_assert(false, "unknown filesystem");
        }();

        for (const auto& entry : std::filesystem::directory_iterator(".", options, ec)) {
            if (!entry.is_regular_file()) continue;

            std::match_results<std::filesystem::path::string_type::const_iterator> match;
            auto filename = entry.path().filename().native();
            if (std::regex_match(filename, match, pattern)) {
                unsigned num = std::stoul(match[1].str());
                dlls[num] = entry.path();
            } else if (filename.ends_with(unordered))
				unorderdDlls.push_back(entry.path());
        }

#ifdef _WIN32
        auto loadLib = [](const std::filesystem::path::value_type* path) {
            if constexpr (std::is_same_v<std::filesystem::path::value_type, char>)
                return ::LoadLibraryA(path);
            else if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>)
                return ::LoadLibraryW(path);
            else
                static_assert(false, "unknown filesystem");
        };
#endif

        for (const auto& [num, path] : dlls)
			loadLib(path.c_str());
        
        for (const auto& path: unorderdDlls)
			loadLib(path.c_str());
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching redirect dll");

        if (!g_detours_error) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&gs_getaddrinfo, redirect_getaddrinfo);
            DetourDetach(&gs_gethostbyname, redirect_gethostbyname);
            g_detours_error = DetourTransactionCommit();
        }
        else {
            g_detours_error = 0;
        }

        if (g_detours_error) {
            std::println(std::cerr, "[gamespy][detours] failed to commit - error ", g_detours_error);
            return FALSE;
        }
    }

    return TRUE;
}
