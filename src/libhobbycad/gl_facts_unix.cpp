// =====================================================================
//  src/libhobbycad/gl_facts_unix.cpp — GL driver facts on Linux and BSD
// =====================================================================
//
//  Built on every Unix but macOS (see CMakeLists.txt). The X11 session,
//  GLX, dlopen() and dl_iterate_phdr() are common to Linux, FreeBSD,
//  OpenBSD, NetBSD and DragonFly; only the kernel side differs, and those
//  few lines are the conditionals below:
//
//    Linux      /sys/class/drm/cardN names every GPU's PCI vendor and kernel
//               driver; /sys/module/nvidia/version the NVIDIA kernel module.
//    FreeBSD,   sysctl dev.vgapci.N.%pnpinfo names each VGA-class PCI device;
//    DragonFly  kldfind() says which DRM or NVIDIA kernel module is loaded;
//               sysctl hw.nvidia.version the NVIDIA kernel module.
//    OpenBSD,   the DRM drivers are part of the kernel and there is no
//    NetBSD     NVIDIA driver from NVIDIA (NetBSD has nouveau), so there is
//               no adapter list to read here.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/gl_diagnostics.h>

#include "gl_facts_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include <dlfcn.h>
#include <link.h>
#include <sys/utsname.h>

#if defined(__linux__)
#  include <filesystem>
#  include <iterator>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#  include <sys/types.h>
#  include <sys/param.h>
#  include <sys/linker.h>
#endif

namespace hobbycad {
namespace gldiag {

namespace {

std::string platformName()
{
    struct utsname u;
    if (uname(&u) == 0) return std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
#if defined(__linux__)
    return "Linux";
#else
    return "Unix";
#endif
}

void collectEnvironment(GlDriverFacts& f)
{
    // Which display the process was pointed at, and every override that
    // silently changes which GL implementation answers.
    detail::collectEnvironment(f, {
        "DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE", "QT_QPA_PLATFORM",
        "LIBGL_ALWAYS_SOFTWARE", "LIBGL_ALWAYS_INDIRECT", "LIBGL_DRIVERS_PATH",
        "GALLIUM_DRIVER", "MESA_LOADER_DRIVER_OVERRIDE", "MESA_GL_VERSION_OVERRIDE",
        "__GLX_VENDOR_LIBRARY_NAME", "__EGL_VENDOR_LIBRARY_FILENAMES",
        "__NV_PRIME_RENDER_OFFLOAD", "DRI_PRIME", "LD_LIBRARY_PATH", "LD_PRELOAD",
    });
    detail::collectSshSession(f);
}

/// GLX strings through the libGL this process already has, so the failing
/// configuration is the one described.
void collectGlx(GlDriverFacts& f, const void* display)
{
    if (!display) return;
    f.glxQueried = true;
    // Linux and FreeBSD name the library libGL.so.1; OpenBSD versions it
    // differently, so the unversioned name is the fallback.
    static const char* const candidates[] = {"libGL.so.1", "libGL.so"};
    void* lib = nullptr;
#if defined(RTLD_NOLOAD)
    for (const char* c : candidates) {
        if ((lib = dlopen(c, RTLD_LAZY | RTLD_NOLOAD)) != nullptr) break;
    }
#endif
    for (const char* c : candidates) {
        if (lib) break;
        lib = dlopen(c, RTLD_LAZY);
    }
    if (!lib) {
        f.glxError = "libGL could not be loaded";
        return;
    }
    using QueryVersion = int (*)(void*, int*, int*);
    using ClientString = const char* (*)(void*, int);
    using ServerString = const char* (*)(void*, int, int);
    auto queryVersion = reinterpret_cast<QueryVersion>(dlsym(lib, "glXQueryVersion"));
    auto clientString = reinterpret_cast<ClientString>(dlsym(lib, "glXGetClientString"));
    auto serverString = reinterpret_cast<ServerString>(dlsym(lib, "glXQueryServerString"));
    void* dpy = const_cast<void*>(display);
    // GLX_VENDOR and GLX_VERSION from <GL/glx.h>, which is not included:
    // this file must build where no GL headers are installed.
    constexpr int GLX_VENDOR_NAME = 1;
    constexpr int GLX_VERSION_NAME = 2;
    if (queryVersion) {
        int major = 0, minor = 0;
        if (queryVersion(dpy, &major, &minor))
            f.glxVersion = std::to_string(major) + "." + std::to_string(minor);
        else
            f.glxError = "glXQueryVersion failed: the X server offers no GLX";
    }
    if (clientString) {
        const char* v = clientString(dpy, GLX_VENDOR_NAME);
        const char* n = clientString(dpy, GLX_VERSION_NAME);
        f.glxClient = std::string(v ? v : "?") + " " + (n ? n : "?");
    }
    if (serverString) {
        const char* v = serverString(dpy, 0, GLX_VENDOR_NAME);
        const char* n = serverString(dpy, 0, GLX_VERSION_NAME);
        f.glxServer = std::string(v ? v : "?") + " " + (n ? n : "?");
    }
}

int collectLoadedObject(struct dl_phdr_info* info, size_t, void* data)
{
    if (info->dlpi_name && *info->dlpi_name) {
        detail::noteGlLibrary(*static_cast<std::set<std::string>*>(data), info->dlpi_name);
    }
    return 0;
}

void collectLoadedLibraries(GlDriverFacts& f)
{
    std::set<std::string> names;
    dl_iterate_phdr(&collectLoadedObject, &names);
    f.loadedLibraries.assign(names.begin(), names.end());
}

#if defined(__linux__)

std::string readFirstLine(const std::string& path)
{
    std::ifstream in(path);
    std::string line;
    if (in && std::getline(in, line)) return line;
    return std::string();
}

unsigned readHex(const std::filesystem::path& p)
{
    const std::string s = readFirstLine(p.string());
    return s.empty() ? 0u : static_cast<unsigned>(std::strtoul(s.c_str(), nullptr, 16));
}

void collectKernelSide(GlDriverFacts& f)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path drm("/sys/class/drm");
    if (!fs::is_directory(drm, ec)) {
        f.adaptersNote = "/sys/class/drm is not available";
    } else {
        std::set<std::string> cards;
        for (const auto& entry : fs::directory_iterator(drm, ec)) {
            const std::string n = entry.path().filename().string();
            // "card0" is a GPU; "card0-HDMI-A-1" is one of its connectors.
            if (n.rfind("card", 0) == 0 && n.find('-') == std::string::npos) cards.insert(n);
        }
        for (const std::string& c : cards) {
            const fs::path dev = drm / c / "device";
            GpuAdapter a;
            a.pciVendor = readHex(dev / "vendor");
            a.pciDevice = readHex(dev / "device");
            const fs::path drv = fs::read_symlink(dev / "driver", ec);
            if (!ec) a.kernelDriver = drv.filename().string();
            ec.clear();
            a.name = c;
            // A platform GPU (Arm SoC) has no PCI IDs; its device-tree
            // "compatible" strings (NUL-separated) name the vendor.
            if (a.pciVendor == 0) {
                std::ifstream in((dev / "of_node" / "compatible").string(), std::ios::binary);
                std::string all((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
                const std::string first = all.c_str();
                if (!first.empty()) {
                    a.vendorHint = vendorFromDtCompatible(first);
                    a.name += " (" + first + ")";
                }
            }
            f.adapters.push_back(a);
        }
        f.adaptersKnown = true;
        if (cards.empty()) f.adaptersNote = "no DRM device: no GPU kernel driver is bound";
    }
    f.nvidiaKernelVersion = readFirstLine("/sys/module/nvidia/version");
    if (!f.nvidiaKernelVersion.empty()) {
        f.nvidiaKernelSource = "/sys/module/nvidia/version";
    } else {
        const std::string nvrm = readFirstLine("/proc/driver/nvidia/version");
        f.nvidiaKernelVersion = nvidiaVersionFromText(nvrm);
        if (!f.nvidiaKernelVersion.empty()) f.nvidiaKernelSource = "/proc/driver/nvidia/version";
    }
}

#elif defined(__FreeBSD__) || defined(__DragonFly__)

unsigned pnpField(const std::string& pnp, const char* key)
{
    const std::string k = std::string(key) + "=";
    size_t at = pnp.find(k);
    if (at == std::string::npos) return 0;
    return static_cast<unsigned>(std::strtoul(pnp.c_str() + at + k.size(), nullptr, 16));
}

void collectKernelSide(GlDriverFacts& f)
{
    // Every VGA-class PCI device attaches as vgapci; its pnpinfo carries the
    // PCI IDs and needs no privileges (pciconf does).
    for (int unit = 0; unit < 16; ++unit) {
        const std::string key = "dev.vgapci." + std::to_string(unit) + ".%pnpinfo";
        const std::string pnp = detail::sysctlString(key.c_str());
        if (pnp.empty()) {
            if (unit > 0) break;
            continue;
        }
        GpuAdapter a;
        a.name = "vgapci" + std::to_string(unit);
        a.pciVendor = pnpField(pnp, "vendor");
        a.pciDevice = pnpField(pnp, "device");
        f.adapters.push_back(a);
    }
    // Which GPU kernel module is loaded is not tied to a unit here, so it is
    // reported per machine: the formatter lists these beside the adapters.
    static const char* const modules[] = {
        "nvidia.ko", "nvidia-modeset.ko", "nvidia-drm.ko",
        "i915kms.ko", "amdgpu.ko", "radeonkms.ko", "vmwgfx.ko", "drm.ko",
    };
    std::string loaded;
    for (const char* m : modules) {
        if (kldfind(m) >= 0) loaded += std::string(loaded.empty() ? "" : " ") + m;
    }
    for (GpuAdapter& a : f.adapters) a.kernelDriver = loaded;   // empty: no GPU module loaded
    if (f.adapters.empty() && !loaded.empty()) {
        GpuAdapter a;
        a.name = "(no vgapci device)";
        a.kernelDriver = loaded;
        f.adapters.push_back(a);
    }
    f.adaptersKnown = true;
    f.nvidiaKernelVersion = nvidiaVersionFromText(detail::sysctlString("hw.nvidia.version"));
    if (!f.nvidiaKernelVersion.empty()) f.nvidiaKernelSource = "sysctl hw.nvidia.version";
}

#else  // OpenBSD, NetBSD, others

void collectKernelSide(GlDriverFacts& f)
{
#  if defined(__OpenBSD__)
    f.adaptersNote = "OpenBSD builds its GPU drivers into the kernel and has none from NVIDIA; "
                     "dmesg shows the inteldrm, amdgpu or radeondrm attach line";
#  elif defined(__NetBSD__)
    f.adaptersNote = "NetBSD builds its GPU drivers into the kernel; dmesg shows the "
                     "i915drmkms, amdgpu, radeon or nouveau attach line";
#  else
    f.adaptersNote = "no GPU driver information is gathered on this system";
#  endif
}

#endif

}  // namespace

GlDriverFacts gatherGlDriverFacts(const void* x11Display)
{
    GlDriverFacts f;
    f.platform = platformName();
    collectEnvironment(f);
    collectGlx(f, x11Display);
    collectLoadedLibraries(f);
    collectKernelSide(f);
    return f;
}

}  // namespace gldiag
}  // namespace hobbycad
