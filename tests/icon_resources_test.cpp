#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

unsigned U16(const BYTE* p) { return p[0] | (unsigned)p[1] << 8; }
unsigned U32(const BYTE* p) { return U16(p) | U16(p + 2) << 16; }

const BYTE* Resource(HMODULE module, unsigned type, unsigned id, DWORD& size) {
    HRSRC entry = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(type));
    if (!entry) return nullptr;
    size = SizeofResource(module, entry);
    return static_cast<const BYTE*>(LockResource(LoadResource(module, entry)));
}

BOOL CALLBACK CountGroups(HMODULE, LPCWSTR, LPWSTR, LONG_PTR data) {
    ++*reinterpret_cast<unsigned*>(data);
    return TRUE;
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    HMODULE exe = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    assert(exe);
    unsigned groups = 0;
    EnumResourceNamesW(exe, MAKEINTRESOURCEW(14), CountGroups, reinterpret_cast<LONG_PTR>(&groups));
    assert(groups == 1);

    std::ifstream file("res/app.ico", std::ios::binary);
    std::vector<BYTE> ico((std::istreambuf_iterator<char>(file)), {});
    constexpr unsigned sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    constexpr unsigned count = ARRAYSIZE(sizes);
    assert(ico.size() >= 6 + 16 * count && U16(ico.data() + 4) == count);
    DWORD groupBytes = 0;
    const BYTE* group = Resource(exe, 14, 1, groupBytes);
    assert(group && groupBytes == 6 + 14 * count && U16(group + 4) == count);

    for (unsigned i = 0; i < count; ++i) {
        const BYTE* source = ico.data() + 6 + 16 * i;
        const BYTE* embedded = group + 6 + 14 * i;
        assert(source[0] == sizes[i] % 256 && source[1] == source[0]);
        assert(U16(source + 6) == 32 && std::memcmp(source, embedded, 12) == 0);
        DWORD frameBytes = 0;
        const BYTE* frame = Resource(exe, 3, U16(embedded + 12), frameBytes);
        const auto offset = U32(source + 12);
        assert(frame && frameBytes == U32(source + 8) && offset <= ico.size() &&
               frameBytes <= ico.size() - offset && std::memcmp(ico.data() + offset, frame, frameBytes) == 0);
        if (sizes[i] < 256) {
            assert(frameBytes >= 40 && U32(frame) == 40 && U32(frame + 4) == sizes[i] &&
                   U32(frame + 8) == 2 * sizes[i] && U16(frame + 14) == 32);
        } else {
            assert(frameBytes >= 8 && std::memcmp(frame, "\x89PNG\r\n\x1a\n", 8) == 0);
        }
    }
    FreeLibrary(exe);
    std::puts("All eight embedded icon frames match res/app.ico");
    return 0;
}
