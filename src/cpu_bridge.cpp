#include "protocol.h"
#include <cstring>

static CpuState* state;
static HANDLE owner;
static HWND window;
static UINT_PTR timer;
static BYTE* gate;
static BYTE* trampoline;
static BYTE saved[5];
static bool hooked;
static volatile LONG enabled;

static bool __fastcall RenderGate(void* self, void*) {
    if (enabled) return false;
    return reinterpret_cast<bool(__fastcall*)(void*, void*)>(trampoline)(self, nullptr);
}

static bool Restore() {
    InterlockedExchange(&enabled, 0);
    if (!hooked) return true;
    BYTE expected[5] = {0xE9};
    *reinterpret_cast<LONG*>(expected + 1) = static_cast<LONG>(reinterpret_cast<BYTE*>(&RenderGate) - (gate + 5));
    if (memcmp(gate, expected, 5)) {
        strcpy_s(state->error, "Render gate changed by another tool; restart the client.");
        return false;
    }
    DWORD previous;
    if (!VirtualProtect(gate, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        strcpy_s(state->error, "Cannot restore render gate; rendering is enabled through the trampoline.");
        return false;
    }
    memcpy(gate, saved, 5);
    DWORD ignored;
    VirtualProtect(gate, 5, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), gate, 5);
    hooked = false;
    return true;
}

static bool EnableGate(BYTE* candidate) {
    if (hooked) { InterlockedExchange(&enabled, 1); return true; }
    static const BYTE signature[] = {0xE8,0,0,0,0,0x8B,0x0D,0,0,0,0,0xE8,0,0,0,0,0x84,0xC0,0x75,0x10,0x68,0,0,0,0,0xE8,0,0,0,0,0x83,0xC4,0x04,0x32,0xC0,0xC3,0xB0,0x01,0xC3};
    static const char mask[] = "x????xx????x????xxxxx????x????xxxxxxxxx";
    static_assert(sizeof(mask) == sizeof(signature) + 1);
    gate = candidate;
    for (size_t i = 0; i < sizeof(signature); ++i) {
        if (mask[i] == 'x' && gate[i] != signature[i]) {
            strcpy_s(state->error, "Unsupported client: render gate signature mismatch."); return false;
        }
    }
    if (!trampoline) trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) { strcpy_s(state->error, "Cannot allocate trampoline."); return false; }
    memcpy(saved, gate, 5);
    auto target = gate + 5 + *reinterpret_cast<LONG*>(gate + 1);
    trampoline[0] = 0xE8;
    *reinterpret_cast<LONG*>(trampoline + 1) = static_cast<LONG>(target - (trampoline + 5));
    trampoline[5] = 0xE9;
    *reinterpret_cast<LONG*>(trampoline + 6) = static_cast<LONG>((gate + 5) - (trampoline + 10));
    FlushInstructionCache(GetCurrentProcess(), trampoline, 16);
    DWORD previous;
    if (!VirtualProtect(gate, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        strcpy_s(state->error, "Cannot write render gate."); return false;
    }
    gate[0] = 0xE9;
    *reinterpret_cast<LONG*>(gate + 1) = static_cast<LONG>(reinterpret_cast<BYTE*>(&RenderGate) - (gate + 5));
    DWORD ignored;
    VirtualProtect(gate, 5, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), gate, 5);
    hooked = true;
    InterlockedExchange(&enabled, 1);
    return true;
}

static bool Enable() {
    constexpr DWORD gateRva = 0x0279D310;
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < gateRva + 64) {
        strcpy_s(state->error, "Unsupported client image size."); return false;
    }
    return EnableGate(base + gateRva);
}

static void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
    if (!state) return;
    if (state->closing || WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) {
        bool restored = Restore();
        state->enabled = 0;
        if (!restored) return;
        KillTimer(window, timer);
        auto old = state; state = nullptr;
        CloseHandle(owner); owner = nullptr;
        InterlockedExchange(&old->closed, 1);
        UnmapViewOfFile(old);
        return;
    }
    auto request = state->request;
    if (!request) return;
    state->error[0] = 0;
    __try {
        if (request == 1) Enable();
        else if (request == 2) Restore();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&enabled, 0);
        strcpy_s(state->error, "Client memory access failed; CPU mode disabled.");
    }
    state->enabled = enabled;
    InterlockedExchange(&state->request, 0);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK CpuHook(int code, WPARAM w, LPARAM l) {
    if (code >= 0 && !state) {
        wchar_t name[96]; MappingName(name, GetCurrentProcessId());
        auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (mapping) {
            auto candidate = static_cast<CpuState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CpuState)));
            CloseHandle(mapping);
            if (candidate) {
                auto hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(candidate->targetWindow));
                DWORD pid = 0;
                auto tid = GetWindowThreadProcessId(hwnd, &pid);
                if (candidate->magic == CpuMagic && !candidate->closing && pid == GetCurrentProcessId() && tid == GetCurrentThreadId()) {
                    HMODULE pinned;
                    auto process = OpenProcess(SYNCHRONIZE, FALSE, candidate->ownerPid);
                    if (process && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&CpuHook), &pinned)) {
                        state = candidate; owner = process; window = hwnd;
                        timer = SetTimer(window, reinterpret_cast<UINT_PTR>(&Tick), 100, Tick);
                        if (timer) InterlockedExchange(&state->ready, 1);
                        else { state = nullptr; CloseHandle(owner); UnmapViewOfFile(candidate); }
                    } else { if (process) CloseHandle(process); UnmapViewOfFile(candidate); }
                } else UnmapViewOfFile(candidate);
            }
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
