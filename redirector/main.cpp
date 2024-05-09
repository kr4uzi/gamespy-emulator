#include <WinSock2.h>
#include <windows.h>
#include <detours.h>
#include <string>
#include <vector>
#include <ranges>
#include <array>
#include <type_traits>
#include <sstream>

decltype(gethostbyname)* g_gethostbyname = ::gethostbyname;

class cpp_hostent
{
    std::vector<char*> c_aliases;
    std::vector<std::vector<std::uint8_t>> c_addrs;
    std::vector<char*> c_addrs_list;
    hostent _hostent;

public:
    std::string name;
    std::vector<std::string> aliases;
    std::vector<std::vector<std::uint8_t>> addrs;

    //[[uninitialized(_hostent)]]
    cpp_hostent(decltype(cpp_hostent::name) name, decltype(cpp_hostent::aliases) aliases, decltype(cpp_hostent::addrs) addrs)
        : name(std::move(name)), aliases(aliases), addrs(std::move(addrs))
    {

    }

    enum class Error
    {
        ADDRS_EMPTY,
        ADDR_TYPE_MISSMATCH
    };

    hostent* c_hostent()
    {
        if (addrs.empty())
            return nullptr;

        const auto firstAddrSize = addrs.front().size();
        if (firstAddrSize != 4 && firstAddrSize != 16)
            return nullptr;

        c_aliases.clear();
        c_aliases.append_range(aliases | std::views::transform([](auto& str) { return str.data(); }));
        c_aliases.push_back(nullptr);

        // all addr sizes must match (e.g. either all IPv4 or all IPv6)
        for (auto i = addrs.begin() + 1; i != addrs.end(); ++i) {
            if (i->size() != firstAddrSize)
                return nullptr;
        }

        for (const auto& addr : addrs) {
            c_addrs.emplace_back(std::from_range, addr);
            c_addrs_list.push_back(reinterpret_cast<char*>(c_addrs.back().data()));
        }

        _hostent = hostent{
            .h_name = name.data(),
            .h_aliases = c_aliases.data(),
            .h_addrtype = static_cast<decltype(hostent::h_addrtype)>(firstAddrSize == 4 ? AF_INET : AF_INET6),
            .h_length = static_cast<decltype(hostent::h_length)>(firstAddrSize),
            .h_addr_list = c_addrs_list.data()
        };

        return &_hostent;
    }
};

std::vector<std::uint8_t> gamespyTarget;
__declspec(dllexport) hostent* __stdcall GetHostByName(const char *name)
{
    static cpp_hostent currentHost = cpp_hostent{
        std::string(name),
        {},
        { gamespyTarget }
    };

    return currentHost.c_hostent();
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;

    static_assert(std::is_same_v<decltype(::gethostbyname), decltype(::GetHostByName)>, "gethostbyname signature missmatch!");

    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();

        auto args = std::string{ GetCommandLineA() };
        if (auto gsStart = args.find("+gamespy "); gsStart != std::string::npos) {
            auto stream = std::stringstream{ args.substr(gsStart + 9) };
            char dot;
            int ip0, ip1, ip2, ip3;
            stream >> ip0 >> dot >> ip1 >> dot >> ip2 >> dot >> ip3;
            if (!stream)
                return FALSE;

            gamespyTarget.assign_range(std::array{ ip0, ip1, ip2, ip3 });
        }

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