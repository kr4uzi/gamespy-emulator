#define NOMINMAX
#include <Windows.h>
#include <detours.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <print>
#include <vector>

#pragma comment(lib, "user32.lib")

//
// prevent the startup crash / silent login failure caused by a resolution (or refresh rate) which is no longer
// supported by the display (e.g. "VideoSettings.setResolution 1920x1080@100Hz" when only 60Hz is available).
//
// RendDX9.dll VideoOptions::setDynamicOption(int option, const std::string& value) only accepts values from the list
// of enumerated adapter modes ("%ix%i@%iHz", 32bpp, width >= 800). Anything else triggers an assertion
// (VideoOptions.cpp "Unknown DynamicOption value ") which ends in an int 3 / null write - the game dies in fullscreen
// mode and the login of a profile with an unsupported Video.con fails silently.
// This patch replaces an unknown value with the closest supported one before the original function is called.
//
// The renderer dll differs between builds (RendDX9.dll 1.5, RendDX9x2.dll, RendDX9_r.dll, EditorRendDX9.dll), so
// nothing version specific is hard coded: setDynamicOption is located by the assert message it references and the
// VideoOptions vtable by the pointer to it. Only the vtable layout is relied upon, which is the interface the game
// executable is compiled against and therefore identical across all builds:
//   slot 6: const std::vector<std::string>& getDynamicOptionValues(int option)
//   slot 7: bool setDynamicOption(int option, const std::string& value)
//   slot 8: const std::string& getDynamicOption(int option)
// setDynamicOption is only ever called through that vtable, so the vtable slot is replaced (no code patching).
//

// std::string layout of msvcp71 (which RendDX9.dll is using)
struct msvc71_string {
    void* allocator;
    union {
        char buf[16];
        char* ptr;
    };
    std::uint32_t size;
    std::uint32_t capacity;

    const char* c_str() const { return capacity >= sizeof(buf) ? ptr : buf; }
};
static_assert(sizeof(msvc71_string) == 0x1C, "msvc71 std::string must be 0x1C bytes");

// std::vector<std::string> layout of msvcp71
struct msvc71_string_vector {
    void* allocator;
    msvc71_string* first;
    msvc71_string* last;
    msvc71_string* end;
};
static_assert(sizeof(msvc71_string_vector) == 0x10, "msvc71 std::vector must be 0x10 bytes");

namespace dice::hfe::RendDX9 {
    constexpr char unknownDynamicOptionMessage[] = "Unknown DynamicOption value ";

    constexpr int getDynamicOptionValuesSlot = 6;
    constexpr int setDynamicOptionSlot = 7;
    constexpr int getDynamicOptionSlot = 8;
    constexpr int numVTableSlots = 15;

    constexpr int numDynamicOptions = 2;
    constexpr int resolutionOption = 1;
}

template<typename T> T VTOF(void* ptr)
{
    // fills in 4 bytes and zeroes the rest
    T result = 0;
    *(void**)&result = ptr;
    return result;
}

// the data layout differs between builds - only the virtual functions are used
struct VideoOptions {
    const msvc71_string_vector* getDynamicOptionValues(int option);
    bool setDynamicOption(int option, const msvc71_string* value);
    const msvc71_string* getDynamicOption(int option);
};

using getDynamicOptionValues_t = decltype(&VideoOptions::getDynamicOptionValues);
using setDynamicOption_t = decltype(&VideoOptions::setDynamicOption);
using getDynamicOption_t = decltype(&VideoOptions::getDynamicOption);
getDynamicOptionValues_t bf2_getDynamicOptionValues = nullptr;
setDynamicOption_t bf2_setDynamicOption = nullptr;
getDynamicOption_t bf2_getDynamicOption = nullptr;

HMODULE g_renderer = nullptr;
std::vector<void**> g_setDynamicOptionSlots;

struct Resolution {
    int width = 0;
    int height = 0;
    int refresh = 0;
};

bool parse_resolution(const char* value, Resolution& res)
{
    return std::sscanf(value, "%dx%d@%dHz", &res.width, &res.height, &res.refresh) == 3;
}

bool is_valid_list(const msvc71_string_vector* values)
{
    return values && values->first && values->first < values->last &&
        (reinterpret_cast<std::uintptr_t>(values->last) - reinterpret_cast<std::uintptr_t>(values->first)) % sizeof(msvc71_string) == 0;
}

// mirrors the _strnicmp comparison done by RendDX9 (compares only the length of the requested value)
bool is_known_value(const msvc71_string_vector& values, const msvc71_string* value)
{
    for (auto it = values.first; it != values.last; ++it) {
        if (::_strnicmp(it->c_str(), value->c_str(), value->size) == 0)
            return true;
    }

    return false;
}

const msvc71_string* closest_resolution(const msvc71_string_vector& values, const char* requested)
{
    Resolution wanted;
    if (!parse_resolution(requested, wanted)) {
        // unparsable value - fall back to the current desktop mode
        DEVMODEA desktop = { .dmSize = sizeof(DEVMODEA) };
        if (::EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktop)) {
            wanted = { static_cast<int>(desktop.dmPelsWidth), static_cast<int>(desktop.dmPelsHeight), static_cast<int>(desktop.dmDisplayFrequency) };
        }
    }

    // prefer the same size (only the refresh rate is off), otherwise the closest size - on equal distance prefer the lower refresh rate
    const msvc71_string* best = nullptr;
    auto bestScore = std::numeric_limits<long long>::max();
    for (auto it = values.first; it != values.last; ++it) {
        Resolution candidate;
        if (!parse_resolution(it->c_str(), candidate))
            continue;

        long long sizeDistance = std::abs(candidate.width - wanted.width) + std::abs(candidate.height - wanted.height);
        long long refreshDistance = std::abs(candidate.refresh - wanted.refresh);
        auto score = sizeDistance * 100000 + refreshDistance * 2 + (candidate.refresh > wanted.refresh ? 1 : 0);
        if (score < bestScore) {
            bestScore = score;
            best = &*it;
        }
    }

    return best;
}

bool VideoOptions::setDynamicOption(int option, const msvc71_string* value)
{
    using namespace dice::hfe::RendDX9;

    if (option >= 0 && option < numDynamicOptions && value) {
        auto values = (this->*bf2_getDynamicOptionValues)(option);
        if (is_valid_list(values) && !is_known_value(*values, value)) {
            const msvc71_string* replacement = nullptr;
            if (option == resolutionOption) {
                replacement = closest_resolution(*values, value->c_str());
            }
            else {
                // keep the current selection, otherwise use the first (default) value
                auto selected = (this->*bf2_getDynamicOption)(option);
                replacement = selected >= values->first && selected < values->last ? selected : values->first;
            }

            if (replacement) {
                std::println("[gamespy][bf2] unsupported video option {} value '{}' replaced by '{}'", option, value->c_str(), replacement->c_str());
                value = replacement;
            }
        }
    }

    return (this->*bf2_setDynamicOption)(option, value);
}

bool write_vtable_slot(void** slot, void* value)
{
    DWORD oldProtect = 0;
    if (!::VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect))
        return false;

    *slot = value;
    ::VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
    return true;
}

struct Range {
    std::uint8_t* begin = nullptr;
    std::uint8_t* end = nullptr;

    bool contains(const void* ptr) const { return ptr >= begin && ptr < end; }
};

struct Image {
    Range code;
    std::vector<Range> data;

    explicit Image(HMODULE module)
    {
        auto base = reinterpret_cast<std::uint8_t*>(module);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            Range range = { base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize };
            if (section->Characteristics & IMAGE_SCN_CNT_CODE) {
                if (!code.begin)
                    code = range;
            }
            else if (!(section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) {
                data.push_back(range);
            }
        }
    }

    // follows incremental linking thunks (jmp rel32) as found in the debug / editor builds
    std::uint8_t* resolve(std::uint8_t* code_ptr) const
    {
        for (int i = 0; i < 4 && code.contains(code_ptr) && code_ptr + 5 <= code.end && *code_ptr == 0xE9; ++i)
            code_ptr += 5 + *reinterpret_cast<std::int32_t*>(code_ptr + 1);
        return code_ptr;
    }

    bool is_vtable(void** vtable, int size) const
    {
        for (const auto& range : data) {
            if (!range.contains(vtable) || !range.contains(vtable + size - 1))
                continue;

            for (int i = 0; i < size; ++i) {
                if (!code.contains(vtable[i]))
                    return false;
            }
            return true;
        }

        return false;
    }

    // the getters take a single int (ret 4) - rules out a vtable which only happens to contain setDynamicOption at slot 7
    bool is_getter(void* function) const
    {
        auto code_ptr = resolve(static_cast<std::uint8_t*>(function));
        for (int i = 0; i < 64 && code.contains(code_ptr + i + 2); ++i) {
            if (code_ptr[i] == 0xC2 && code_ptr[i + 1] == 0x04 && code_ptr[i + 2] == 0x00)
                return true;
        }

        return false;
    }
};

std::uint8_t* find_bytes(const Range& range, const void* bytes, std::size_t size)
{
    for (auto it = range.begin; it + size <= range.end; ++it) {
        if (std::memcmp(it, bytes, size) == 0)
            return it;
    }

    return nullptr;
}

// returns all VideoOptions vtable slots pointing to setDynamicOption (empty if unsupported)
// isRenderer is set if the dll contains the VideoOptions assert message at all
std::vector<void**> find_set_dynamic_option_slots(HMODULE module, bool& isRenderer)
{
    using namespace dice::hfe::RendDX9;
    isRenderer = false;
    Image image(module);
    if (!image.code.begin)
        return {};

    // 1. the assert message (including its terminator)
    std::uint8_t* message = nullptr;
    for (const auto& range : image.data) {
        if ((message = find_bytes(range, unknownDynamicOptionMessage, sizeof(unknownDynamicOptionMessage))))
            break;
    }
    if (!message)
        return {};

    isRenderer = true;

    // 2. the single "push offset message" inside setDynamicOption
    std::uint8_t push[5] = { 0x68 };
    std::memcpy(push + 1, &message, sizeof(message));
    auto site = find_bytes(image.code, push, sizeof(push));
    if (!site || find_bytes({ site + 1, image.code.end }, push, sizeof(push)))
        return {};

    // 3. the vtable slot 7 whose function starts closest before the push - that function contains it
    constexpr std::ptrdiff_t maxDistance = 0x2000;
    std::uint8_t* function = nullptr;
    std::vector<void**> slots;
    for (const auto& range : image.data) {
        for (auto slot = reinterpret_cast<void**>(range.begin); reinterpret_cast<std::uint8_t*>(slot + 1) <= range.end; ++slot) {
            if (!image.code.contains(*slot))
                continue;

            auto target = image.resolve(static_cast<std::uint8_t*>(*slot));
            if (target > site || site - target > maxDistance || target < function)
                continue;

            auto vtable = slot - setDynamicOptionSlot;
            if (!image.is_vtable(vtable, numVTableSlots) ||
                !image.is_getter(vtable[getDynamicOptionValuesSlot]) || !image.is_getter(vtable[getDynamicOptionSlot]))
                continue;

            if (target != function) {
                function = target;
                slots.clear();
            }
            slots.push_back(slot);
        }
    }

    return slots;
}

void attach_renderer(HMODULE module)
{
    using namespace dice::hfe::RendDX9;
    if (g_renderer || !module || !::GetProcAddress(module, "initDll"))
        return;

    // other engine dlls export initDll as well
    bool isRenderer = false;
    auto slots = find_set_dynamic_option_slots(module, isRenderer);
    if (!isRenderer)
        return;

    if (slots.empty()) {
        std::println("[gamespy][bf2] VideoOptions not found in renderer - video option fallback disabled");
        g_renderer = module; // don't look again
        return;
    }

    auto vtable = slots.front() - setDynamicOptionSlot;
    bf2_getDynamicOptionValues = VTOF<getDynamicOptionValues_t>(vtable[getDynamicOptionValuesSlot]);
    bf2_setDynamicOption = VTOF<setDynamicOption_t>(vtable[setDynamicOptionSlot]);
    bf2_getDynamicOption = VTOF<getDynamicOption_t>(vtable[getDynamicOptionSlot]);

    auto patched = &VideoOptions::setDynamicOption;
    for (auto slot : slots) {
        if (!write_vtable_slot(slot, *(void**)&patched)) {
            std::println("[gamespy][bf2] failed to patch VideoOptions vtable - error {}", ::GetLastError());
            continue;
        }
        g_setDynamicOptionSlots.push_back(slot);
    }

    g_renderer = module;
    std::println("[gamespy][bf2] video option fallback attached ({} vtable slot(s))", g_setDynamicOptionSlots.size());
}

// the renderer dll is loaded at runtime by BF2.exe (LoadLibraryA + initDll), so the vtable can only be patched once it is present
auto bf2_LoadLibraryA = ::LoadLibraryA;
HMODULE WINAPI patched_LoadLibraryA(LPCSTR name)
{
    auto module = bf2_LoadLibraryA(name);
    if (!g_renderer)
        attach_renderer(module);
    return module;
}

auto bf2_LoadLibraryExA = ::LoadLibraryExA;
HMODULE WINAPI patched_LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    auto module = bf2_LoadLibraryExA(name, file, flags);
    if (!g_renderer && !(flags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)))
        attach_renderer(module);
    return module;
}

LONG g_loader_detours_error = 0;
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;

    if (::DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::println("[gamespy][bf2] attaching client-video-fallback");
        DetourRestoreAfterWith();

        for (auto name : { "RendDX9.dll", "RendDX9_r.dll", "EditorRendDX9.dll" }) {
            attach_renderer(::GetModuleHandleA(name));
        }
        if (g_renderer)
            return TRUE;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((PVOID*)&bf2_LoadLibraryA, patched_LoadLibraryA);
        DetourAttach((PVOID*)&bf2_LoadLibraryExA, patched_LoadLibraryExA);
        g_loader_detours_error = DetourTransactionCommit();
        if (g_loader_detours_error) {
            std::println("[gamespy][bf2] detours failed to commit - error {}", g_loader_detours_error);
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        std::println("[gamespy] detatching client-video-fallback");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (!g_loader_detours_error) {
            DetourDetach((PVOID*)&bf2_LoadLibraryExA, patched_LoadLibraryExA);
            DetourDetach((PVOID*)&bf2_LoadLibraryA, patched_LoadLibraryA);
        }
        if (auto error = DetourTransactionCommit()) {
            std::println("[gamespy][bf2] detours failed to commit - error {}", error);
        }

        // the renderer might have been unloaded already, in which case there is nothing to restore
        HMODULE module = nullptr;
        if (!g_setDynamicOptionSlots.empty() &&
            ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(g_setDynamicOptionSlots.front()), &module) &&
            module == g_renderer) {
            for (auto slot : g_setDynamicOptionSlots)
                write_vtable_slot(slot, *(void**)&bf2_setDynamicOption);
        }
    }

    return TRUE;
}
