#include <Windows.h>
#include <detours.h>
#include <cstddef>
#include <cstdint>
#include <print>

template<typename T> T VTOF(void* ptr)
{
    // fills in 4 bytes and zeroes the rest
    T result = 0;
    *(void**)&result = ptr;
    return result;
}

struct PyObject;
PyObject* (*PyInt_FromLong)(long ival);
PyObject* (*PyTuple_GetItem)(PyObject* p, int pos);
char* (*PyString_AsString)(PyObject* string);

namespace dice {
    namespace hfe {
        namespace python {
            auto sgl_getIsAIGame = reinterpret_cast<PyObject* (*)(PyObject * self, PyObject * args)>(0x005330D0);
        }

        namespace ServerSettings {
            struct Dummy {};
            auto getNumPlayersNeededToStart = VTOF<int (Dummy::*)(void)>((void*)0x00425D60);
        };
    }
}

struct ServerSettings {
    char gap1[0xFC];
    std::uint32_t maxPlayers;
    std::uint32_t numPlayersNeededToStart;

    char gap2[0x25D - sizeof(gap1) - sizeof(maxPlayers) - sizeof(numPlayersNeededToStart)];
    bool ranked;

    std::uint32_t getNumPlayersNeededToStart() {
        // single player should start the game and thus the snapshot sending
        return 1;

        if (ranked) return 2 * (maxPlayers > 32) + 6;
        return numPlayersNeededToStart;
    }
};

static_assert(offsetof(ServerSettings, maxPlayers) == 0xFC, "maxPlayers needs to start at 0xFC");
static_assert(offsetof(ServerSettings, numPlayersNeededToStart) == 0x100, "numPlayersNeededToStart needs to start at 0x100");
static_assert(offsetof(ServerSettings, ranked) == 0x25D, "ranked needs to start at 0x25D");

PyObject* sgl_getIsAIGame(PyObject* self, PyObject* args)
{
    // only replace the very first call so that the ranking modules are initialized during bf2 startup (python/bf2/__init__.py)
    static bool calledOnce = false;

    if (!calledOnce) {
        // this will make the __init__.py load the official stats module even for AI games
        std::println("sgl_getIsAIGame call overwritte once to return false");

        calledOnce = true;
        return ::PyInt_FromLong(0);
    }

    return ::dice::hfe::python::sgl_getIsAIGame(self, args);
}

LONG g_detours_error = 0;
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;

    if (::DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::println("[gamespy][bf2] attaching ranked-server-util");

        const auto dicePyDll = GetModuleHandleA("dice_py.dll");
        if (!dicePyDll) {
            std::println(stderr, "[gamespy][bf2] failed to load dice_py.dll");
            return FALSE;
        }

        PyInt_FromLong = (decltype(PyInt_FromLong))GetProcAddress(dicePyDll, "PyInt_FromLong");
        PyTuple_GetItem = (decltype(PyTuple_GetItem))GetProcAddress(dicePyDll, "PyTuple_GetItem");
        PyString_AsString = (decltype(PyString_AsString))GetProcAddress(dicePyDll, "PyString_AsString");
        if (!PyInt_FromLong || !PyTuple_GetItem || !PyString_AsString) {
            std::println(stderr, "failed to load functions from dice_py.dll");
            return FALSE;
        }

        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());

        auto getNumPlayersNeededToStart = &::ServerSettings::getNumPlayersNeededToStart;
        //::DetourAttach((PVOID*)&dice::hfe::python::sgl_getIsAIGame, ::sgl_getIsAIGame);
        ::DetourAttach(&(PVOID&)dice::hfe::ServerSettings::getNumPlayersNeededToStart, *(PBYTE*)&getNumPlayersNeededToStart); // (int(dice::hfe::ServerSettings::Dummy::*)())::ServerSettings::getNumPlayersNeededToStart);

        g_detours_error = ::DetourTransactionCommit();
        if (g_detours_error) {
            std::println("[gamespy][detours] failed to commit - error {}", g_detours_error);
            return FALSE;
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching ranked-server-utill");

        if (!g_detours_error) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            auto getNumPlayersNeededToStart = &::ServerSettings::getNumPlayersNeededToStart;
            ::DetourDetach(&(PVOID&)dice::hfe::ServerSettings::getNumPlayersNeededToStart, *(PBYTE*)&getNumPlayersNeededToStart);
            //::DetourDetach((PVOID*)&dice::hfe::python::sgl_getIsAIGame, ::sgl_getIsAIGame);

            g_detours_error = ::DetourTransactionCommit();
        }
        else {
            g_detours_error = 0;
        }

        if (g_detours_error) {
            std::println("[gamespy][detours] failed to commit - error {}", g_detours_error);
            return FALSE;
        }
    }

    return TRUE;
}