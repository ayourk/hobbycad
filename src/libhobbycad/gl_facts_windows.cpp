// =====================================================================
//  src/libhobbycad/gl_facts_windows.cpp — GL driver facts on Windows
// =====================================================================
//
//  Built on Windows only (see CMakeLists.txt); MSVC and MinGW alike.
//
//  Windows has one OpenGL front, opengl32.dll, which forwards to the
//  installable client driver (ICD) the display driver registers under its
//  own registry key. An adapter that registers none leaves only Microsoft's
//  GDI Generic OpenGL 1.1 software implementation, which no modern viewer
//  can use; that is what a Basic Display Adapter, a missing driver, many
//  virtual machines and older Remote Desktop sessions give. So the facts
//  here are: each adapter, the ICD it registers and its driver version, the
//  GL modules this process actually loaded, and whether it is a remote
//  session.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/gl_diagnostics.h>

#include <map>
#include <set>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace hobbycad {
namespace gldiag {

namespace {

std::string narrow(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}

std::string platformName()
{
#if defined(_M_ARM64) || defined(__aarch64__)
    const char* arch = "ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
    const char* arch = "x64";
#else
    const char* arch = "x86";
#endif
    std::string name = std::string("Windows (") + arch + " build)";
    // Tell an x64 build running under emulation on ARM64 hardware.
    USHORT processMachine = 0, nativeMachine = 0;
    using IsWow64Process2Fn = BOOL (WINAPI*)(HANDLE, USHORT*, USHORT*);
    if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) {
        auto fn = reinterpret_cast<IsWow64Process2Fn>(
            reinterpret_cast<void*>(GetProcAddress(k32, "IsWow64Process2")));
        if (fn && fn(GetCurrentProcess(), &processMachine, &nativeMachine)
            && nativeMachine == 0xAA64 /* IMAGE_FILE_MACHINE_ARM64 */) {
            name += " on ARM64 hardware";
        }
    }
    return name;
}

/// An environment variable through the native (UTF-16) call; empty if unset.
std::string envValue(const wchar_t* name)
{
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(name, buf, 2048);
    if (n == 0 || n >= 2048) return std::string();
    return narrow(buf);
}

void collectEnvironment(GlDriverFacts& f)
{
    static const wchar_t* const names[] = {
        L"QT_OPENGL", L"QT_QPA_PLATFORM", L"GALLIUM_DRIVER", L"MESA_GL_VERSION_OVERRIDE",
        L"LIBGL_ALWAYS_SOFTWARE",
    };
    for (const wchar_t* n : names) {
        const std::string v = envValue(n);
        if (!v.empty()) f.environment.emplace_back(narrow(n), v);
    }
    // Which kind of session: Remote Desktop renders through the remote
    // display driver; SSH (Win32-OpenSSH sets SSH_CONNECTION) and the
    // services session (0) have no interactive desktop at all, so Windows
    // shows them no display adapter and only software OpenGL.
    DWORD session = 0;
    const bool haveSession = ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0;
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        f.remoteSession = true;
        f.remoteSessionKind = "Remote Desktop";
    } else if (!envValue(L"SSH_CONNECTION").empty()) {
        f.remoteSession = true;
        f.remoteSessionKind = "SSH";
    } else if (haveSession && session == 0) {
        f.remoteSession = true;
        f.remoteSessionKind = "services session (session 0)";
    } else if (haveSession && session != WTSGetActiveConsoleSessionId()) {
        f.remoteSession = true;
        f.remoteSessionKind = "session " + std::to_string(session) + ", not the console's";
    }
}

/// "PCI\VEN_10DE&DEV_2489&SUBSYS_..." -> vendor and device IDs. Windows on
/// ARM reports its Adreno as "ACPI\VEN_QCOM&DEV_0C36...": a four-letter
/// vendor code, not a PCI number.
void parsePciIds(const std::string& id, GpuAdapter& a)
{
    const size_t v = id.find("VEN_");
    if (id.rfind("ACPI\\", 0) == 0 && v != std::string::npos) {
        a.vendorHint = vendorFromAcpiId(id.substr(v + 4, 4));
        return;
    }
    if (v != std::string::npos)
        a.pciVendor = static_cast<unsigned>(std::strtoul(id.c_str() + v + 4, nullptr, 16));
    const size_t d = id.find("DEV_");
    if (d != std::string::npos)
        a.pciDevice = static_cast<unsigned>(std::strtoul(id.c_str() + d + 4, nullptr, 16));
}

/// Read a string or multi-string value; each string becomes one entry.
bool readRegStrings(HKEY key, const wchar_t* value, std::vector<std::string>& out)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return false;
    if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ) return false;
    std::wstring buf(size / sizeof(wchar_t) + 2, L'\0');
    DWORD got = size;
    if (RegQueryValueExW(key, value, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&buf[0]), &got) != ERROR_SUCCESS) return false;
    for (const wchar_t* p = buf.c_str(); *p; p += wcslen(p) + 1) {
        out.push_back(narrow(p));
        if (type != REG_MULTI_SZ) break;
    }
    return true;
}

/// The adapter's own registry key: EnumDisplayDevices reports it as
/// "\Registry\Machine\System\CurrentControlSet\Control\Video\{GUID}\0000".
void readAdapterKey(const std::wstring& deviceKey, GpuAdapter& a)
{
    static const std::wstring prefix = L"\\Registry\\Machine\\";
    if (deviceKey.size() <= prefix.size()
        || _wcsnicmp(deviceKey.c_str(), prefix.c_str(), prefix.size()) != 0) return;
    const std::wstring sub = deviceKey.substr(prefix.size());
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    std::vector<std::string> version;
    if (readRegStrings(key, L"DriverVersion", version) && !version.empty()) {
        a.driverVersion = version.front();
    }
    // Value names are matched without regard to case, which matters: NVIDIA
    // writes "...WoW", Intel and Microsoft "...Wow". AMD also names its real
    // GL implementation under OpenGLVendorName. The Microsoft driver docs
    // place these on the adapter's Class key, which this key reaches.
    a.openGlDriversKnown = true;
#if defined(_WIN64)
    readRegStrings(key, L"OpenGLDriverName", a.openGlDrivers);
    readRegStrings(key, L"OpenGLVendorName", a.openGlDrivers);
#else
    // A 32-bit process on 64-bit Windows uses the WoW registration.
    if (!readRegStrings(key, L"OpenGLDriverNameWow", a.openGlDrivers))
        readRegStrings(key, L"OpenGLDriverName", a.openGlDrivers);
    readRegStrings(key, L"OpenGLVendorNameWow", a.openGlDrivers);
#endif
    RegCloseKey(key);
}

void collectAdapters(GlDriverFacts& f)
{
    std::map<std::wstring, size_t> byKey;   // adapter key -> index in f.adapters
    DISPLAY_DEVICEW dd;
    for (DWORD i = 0;; ++i) {
        ZeroMemory(&dd, sizeof dd);
        dd.cb = sizeof dd;
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        // One entry per output; several outputs share an adapter key.
        const std::wstring key = dd.DeviceKey;
        const bool active = (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        const bool primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        const auto known = byKey.find(key);
        if (known != byKey.end()) {
            GpuAdapter& a = f.adapters[known->second];
            a.active = a.active || active;
            a.primary = a.primary || primary;
            continue;
        }
        byKey.emplace(key, f.adapters.size());
        GpuAdapter a;
        a.name = narrow(dd.DeviceString);
        parsePciIds(narrow(dd.DeviceID), a);
        a.active = active;
        a.primary = primary;
        readAdapterKey(key, a);
        f.adapters.push_back(a);
    }
    f.adaptersKnown = true;
}

/// The legacy machine-wide registration, still written by some drivers:
/// either a value holding the DLL name or a subkey with a "Dll" value
/// (ReactOS's opengl32 icdload.c reads both forms).
void collectLegacyDrivers(GlDriverFacts& f)
{
    HKEY key = nullptr;
    const wchar_t* const path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumValueW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr)
            != ERROR_SUCCESS) {
            break;
        }
        std::vector<std::string> dll;
        if (readRegStrings(key, name, dll) && !dll.empty()) {
            f.legacyOpenGlDrivers.push_back(dll.front());
        }
    }
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr)
            != ERROR_SUCCESS) {
            break;
        }
        HKEY sub = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &sub) != ERROR_SUCCESS) continue;
        std::vector<std::string> dll;
        if (readRegStrings(sub, L"Dll", dll) && !dll.empty())
            f.legacyOpenGlDrivers.push_back(narrow(name) + " -> " + dll.front());
        RegCloseKey(sub);
    }
    RegCloseKey(key);
}

/// Count the desktop's OpenGL pixel formats: hardware-accelerated ones
/// versus Microsoft's generic software ones (PFD_GENERIC_FORMAT without
/// PFD_GENERIC_ACCELERATED). No context is created for this.
void collectPixelFormats(GlDriverFacts& f)
{
    HDC dc = GetDC(nullptr);
    if (!dc) return;
    PIXELFORMATDESCRIPTOR pfd;
    const int count = DescribePixelFormat(dc, 1, sizeof pfd, &pfd);
    int accelerated = 0, generic = 0;
    for (int i = 1; i <= count; ++i) {
        if (!DescribePixelFormat(dc, i, sizeof pfd, &pfd)) continue;
        if (!(pfd.dwFlags & PFD_SUPPORT_OPENGL)) continue;
        const bool isGeneric = (pfd.dwFlags & PFD_GENERIC_FORMAT) != 0
                            && (pfd.dwFlags & PFD_GENERIC_ACCELERATED) == 0;
        if (isGeneric) ++generic; else ++accelerated;
    }
    ReleaseDC(nullptr, dc);
    if (count > 0) {
        f.acceleratedGlFormats = accelerated;
        f.genericGlFormats = generic;
    }
}

void collectLoadedModules(GlDriverFacts& f)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    wchar_t sysDir[MAX_PATH] = L"";
    const UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
    std::set<std::string> names;
    MODULEENTRY32W me;
    me.dwSize = sizeof me;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        const std::string base = narrow(me.szModule);
        if (classifyGlLibrary(base).role != GlLibrary::Role::Unrelated) names.insert(base);
        if (_wcsicmp(me.szModule, L"opengl32.dll") == 0) {
            std::wstring path = me.szExePath;
            const size_t slash = path.find_last_of(L"\\/");
            const std::wstring folder =
                (slash == std::wstring::npos) ? path : path.substr(0, slash);
            f.openGl32Folder = narrow(folder.c_str());
            f.openGl32FromSystem = sysLen > 0 && folder.size() == sysLen
                && _wcsnicmp(folder.c_str(), sysDir, sysLen) == 0;
        }
    }
    CloseHandle(snap);
    f.loadedLibraries.assign(names.begin(), names.end());
}

/// Wine exports wine_get_version from its ntdll; Windows does not.
std::string wineVersion()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return std::string();
    using WineGetVersion = const char* (*)();
    auto fn = reinterpret_cast<WineGetVersion>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version")));
    if (!fn) return std::string();
    const char* v = fn();
    return std::string("Wine ") + (v ? v : "?");
}

}  // namespace

GlDriverFacts gatherGlDriverFacts(const void*)
{
    GlDriverFacts f;
    f.platform = platformName();
    f.compatibilityLayer = wineVersion();
    collectEnvironment(f);
    collectAdapters(f);
    collectLegacyDrivers(f);
    collectPixelFormats(f);
    collectLoadedModules(f);
    return f;
}

}  // namespace gldiag
}  // namespace hobbycad
