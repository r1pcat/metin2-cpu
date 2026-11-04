#pragma once
#include <windows.h>
#include <cwchar>
constexpr DWORD CpuMagic = 0x43505531;
struct CpuState {
    DWORD magic, ownerPid, targetWindow;
    volatile LONG request;
    volatile LONG ready, enabled, closing, closed;
    char error[192];
};
inline void MappingName(wchar_t (&name)[96], DWORD pid) {
    swprintf_s(name, L"Local\\Metin2CpuMode.v1.%lu", pid);
}
