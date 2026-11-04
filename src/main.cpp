#include "protocol.h"
#include <iostream>
#include <string>
#include <cwchar>

static DWORD targetPid;
static HWND targetWindow;
static CpuState* state;
static BOOL CALLBACK FindWindow(HWND hwnd, LPARAM) {
    DWORD pid; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == targetPid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        targetWindow = hwnd; return FALSE;
    }
    return TRUE;
}
static BOOL WINAPI ConsoleExit(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        if (state) InterlockedExchange(&state->closing, 1);
        Sleep(1000); ExitProcess(0);
    }
    return FALSE;
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && !wcscmp(argv[1], L"--help")) {
        std::cout << "Metin2CpuMode.exe <metin2client.exe PID>\nCommands: on, off, status, quit\n"; return 0;
    }
    if (argc != 2) { std::cerr << "Usage: Metin2CpuMode.exe <PID>\n"; return 2; }
    wchar_t* end = nullptr; targetPid = wcstoul(argv[1], &end, 10);
    if (!targetPid || !end || *end) return 2;
    auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, targetPid);
    wchar_t image[32768]; DWORD length = 32768;
    if (!process || !QueryFullProcessImageNameW(process, 0, image, &length)) {
        std::cerr << "Cannot access client. Use the same privilege level as the game.\n"; return 3;
    }
    auto leaf = wcsrchr(image, L'\\');
    if (_wcsicmp(leaf ? leaf + 1 : image, L"metin2client.exe")) { std::cerr << "Target must be metin2client.exe.\n"; return 3; }
    wchar_t ownerName[100];
    swprintf_s(ownerName, L"Local\\Metin2CpuMode.Owner.%lu", targetPid);
    auto ownership = CreateMutexW(nullptr, TRUE, ownerName);
    auto ownershipError = GetLastError();
    auto ownershipWait = ownershipError == ERROR_ALREADY_EXISTS ? WaitForSingleObject(ownership, 0) : WAIT_OBJECT_0;
    if (!ownership || (ownershipWait != WAIT_OBJECT_0 && ownershipWait != WAIT_ABANDONED)) {
        std::cerr << "Another controller/scanner owns this client.\n"; return 5;
    }
    BOOL wow64 = FALSE;
    SYSTEM_INFO system; GetNativeSystemInfo(&system);
    if (!IsWow64Process(process, &wow64) || (system.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL && !wow64)) {
        std::cerr << "Only x86 clients are supported.\n"; return 3;
    }
    EnumWindows(FindWindow, 0);
    if (!targetWindow) { std::cerr << "Visible game window not found.\n"; return 4; }
    wchar_t name[96]; MappingName(name, targetPid);
    auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(CpuState), name);
    if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) { std::cerr << "CPU controller already attached.\n"; return 5; }
    state = static_cast<CpuState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CpuState)));
    if (!state) return 5;
    state->magic = CpuMagic; state->ownerPid = GetCurrentProcessId();
    state->targetWindow = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(targetWindow));
    wchar_t dllPath[32768]; GetModuleFileNameW(nullptr, dllPath, 32768);
    auto slash = wcsrchr(dllPath, L'\\'); if (!slash) return 6;
    wcscpy_s(slash + 1, 32768 - (slash + 1 - dllPath), L"CpuModeBridge.dll");
    auto dll = LoadLibraryW(dllPath);
    auto callback = dll ? reinterpret_cast<HOOKPROC>(GetProcAddress(dll, "_CpuHook@12")) : nullptr;
    if (!callback && dll) callback = reinterpret_cast<HOOKPROC>(GetProcAddress(dll, "CpuHook"));
    auto hook = callback ? SetWindowsHookExW(WH_GETMESSAGE, callback, dll, GetWindowThreadProcessId(targetWindow, nullptr)) : nullptr;
    if (!hook) { std::cerr << "Cannot attach CPU bridge. Windows error: " << GetLastError() << '\n'; return 6; }
    SetConsoleCtrlHandler(ConsoleExit, TRUE);
    PostMessageW(targetWindow, WM_NULL, 0, 0);
    auto deadline = GetTickCount64() + 5000;
    while (!state->ready && GetTickCount64() < deadline) Sleep(10);
    int result = 0;
    if (!state->ready) { std::cerr << "Game thread did not respond.\n"; result = 7; }
    else {
        std::cout << "Connected. CPU mode OFF. Commands: on / off / status / quit\n";
        std::string command;
        while (std::cout << "> " && std::getline(std::cin, command)) {
            if (command == "quit" || command == "q") break;
            LONG request = command == "on" ? 1 : command == "off" ? 2 : command == "status" ? 3 : 0;
            if (!request) { std::cout << "Use on, off, status or quit.\n"; continue; }
            InterlockedExchange(&state->request, request);
            PostMessageW(targetWindow, WM_NULL, 0, 0);
            deadline = GetTickCount64() + 5000;
            while (state->request && WaitForSingleObject(process, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) Sleep(10);
            if (state->request) { std::cerr << "Client response timed out or client exited.\n"; result = 7; break; }
            if (state->error[0]) std::cout << "Error: " << state->error << '\n';
            else std::cout << "CPU mode " << (state->enabled ? "ON" : "OFF") << '\n';
        }
    }
    InterlockedExchange(&state->closing, 1);
    PostMessageW(targetWindow, WM_NULL, 0, 0);
    deadline = GetTickCount64() + 3000;
    while (!state->closed && WaitForSingleObject(process, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) Sleep(10);
    if (!state->closed && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        std::cerr << "Restore not acknowledged. Restart the game before reconnecting.\n"; result = 8;
    }
    UnhookWindowsHookEx(hook);
    CloseHandle(process); CloseHandle(mapping);
    ReleaseMutex(ownership); CloseHandle(ownership);
    return result;
}
