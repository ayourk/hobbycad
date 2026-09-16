// SPDX-License-Identifier: GPL-3.0-only
// HobbyCAD — src/hobbycad/win/hobbycad-com.c — Console launcher (hobbycad.com)
//
// hobbycad.exe is a GUI-subsystem program, so a double-click opens no
// console window. The price is that cmd and PowerShell do not wait for a
// GUI-subsystem program and PowerShell will not pipe into one, which
// breaks "hobbycad --no-gui < script" and every scripted use. This stub
// is the console-subsystem face of the same program: it starts
// hobbycad.exe from its own directory with the same arguments and the
// same standard handles, waits, and returns its exit code. Both shells
// resolve a bare "hobbycad" to hobbycad.com before hobbycad.exe (PATHEXT
// lists .COM first), so typing the name gets console semantics and the
// Start menu, Explorer and file associations, which name the .exe, get
// no console. Visual Studio ships devenv.com beside devenv.exe for the
// same reason. Plain C, kernel32 only, no Qt.

#include <windows.h>
#include <stdio.h>
#include <string.h>

static const wchar_t* skipProgramToken(const wchar_t* p)
{
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') { ++p; }
        if (*p == L'"') { ++p; }
    } else {
        while (*p && *p != L' ' && *p != L'\t') { ++p; }
    }
    while (*p == L' ' || *p == L'\t') { ++p; }
    return p;
}

int main(void)
{
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 1) {
        fwprintf(stderr, L"hobbycad.com: cannot locate itself\n");
        return 1;
    }
    // hobbycad.com -> hobbycad.exe, same directory
    size_t len = wcslen(exe);
    if (len < 4 || _wcsicmp(exe + len - 4, L".com") != 0) {
        fwprintf(stderr, L"hobbycad.com: unexpected launcher name %ls\n", exe);
        return 1;
    }
    wcscpy(exe + len - 4, L".exe");

    const wchar_t* rest = skipProgramToken(GetCommandLineW());
    size_t cmdLen = wcslen(exe) + wcslen(rest) + 4;
    wchar_t* cmd = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, cmdLen * sizeof(wchar_t));
    if (!cmd) { return 1; }
    swprintf(cmd, cmdLen, L"\"%ls\" %ls", exe, rest);

    // Ctrl+C belongs to the child, which shares this console; the stub
    // only waits.
    SetConsoleCtrlHandler(NULL, TRUE);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"hobbycad.com: cannot start %ls (error %lu)\n", exe, GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
