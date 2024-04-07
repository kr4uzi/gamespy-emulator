#include <WinSock2.h>
#include <windows.h>
#include <detours.h>
#include <print>
#include <ranges>
#include <boost/asio/ip/address.hpp>

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
    std::vector<boost::asio::ip::address> addrs;

    [[uninitialized(_hostent)]]
    cpp_hostent(std::string name, std::vector<std::string> aliases, std::vector<boost::asio::ip::address> addrs)
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

        c_aliases.clear();
        c_aliases.append_range(aliases | std::views::transform([](auto& str) { return str.data(); }));
        c_aliases.push_back(nullptr);

        for (auto i = addrs.begin() + 1; i != addrs.end(); ++i) {
            if (i->is_v4() != addrs.front().is_v4())
                return nullptr;
        }

        for (const auto& addr : addrs) {
            if (addr.is_v4())
                c_addrs.emplace_back(std::from_range, addr.to_v4().to_bytes());
            else
                c_addrs.emplace_back(std::from_range, addr.to_v6().to_bytes());

            c_addrs_list.push_back(reinterpret_cast<char*>(c_addrs.back().data()));
        }

        auto bytesSize = addrs.front().is_v4() ?
            sizeof(boost::asio::ip::address_v4::bytes_type)
            : sizeof(boost::asio::ip::address_v6::bytes_type);

        _hostent = hostent{
            .h_name = name.data(),
            .h_aliases = c_aliases.data(),
            .h_addrtype = static_cast<decltype(hostent::h_addrtype)>(addrs.front().is_v4() ? AF_INET : AF_INET6),
            .h_length = static_cast<decltype(hostent::h_length)>(bytesSize),
            .h_addr_list = c_addrs_list.data()
        };

        return &_hostent;
    }
};

__declspec(dllexport) hostent FAR* WSAAPI GetHostByName(_In_z_ const char FAR* name)
{
    static cpp_hostent currentHost = cpp_hostent{
        std::string(name),
        {},
        { boost::asio::ip::address_v4::loopback() }
    };

    // have to decompile the BF2.exe in order to understand why there is such a huge delay (40+ second startup...)
    // the server is producing the correct responses, but there seems to be some other logic doing some delay
    if (currentHost.name == "battlefield2.available.gamespy.com")
        return nullptr;

    return currentHost.c_hostent();
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;

    if (DetourIsHelperProcess()) {
        return TRUE;
    }

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