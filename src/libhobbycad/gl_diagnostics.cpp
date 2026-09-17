// =====================================================================
//  src/libhobbycad/gl_diagnostics.cpp — GL failure report, portable half
// =====================================================================
//
//  Vendor knowledge and the conclusions drawn from GlDriverFacts. No
//  platform headers: the per-platform gatherers are gl_facts_*.cpp.
//
//  Where a rule restates another project's behavior, the source is named
//  beside it: Mesa's driver choice follows its src/loader/
//  pci_id_driver_map.h and loader.c, libglvnd's vendor library name
//  follows its src/GLX/libglxmapping.c.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/gl_diagnostics.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <string>

namespace hobbycad {
namespace gldiag {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

bool contains(const std::string& s, const char* part)
{
    return s.find(part) != std::string::npos;
}

std::string hex4(unsigned v)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04x", v & 0xffffu);
    return buf;
}

/// Vendor of a Linux/BSD kernel GPU driver, for GPUs that are not on PCI
/// (Arm SoCs) and as a cross-check for those that are.
GlVendor vendorFromKernelDriver(const std::string& d)
{
    if (d == "nouveau" || startsWith(d, "nvidia")) return GlVendor::Nvidia;
    if (startsWith(d, "amdgpu") || startsWith(d, "radeon")) return GlVendor::Amd;
    if (d == "xe" || startsWith(d, "i915") || d == "inteldrm") return GlVendor::Intel;
    if (d == "asahi") return GlVendor::Apple;
    if (d == "msm" || startsWith(d, "msm-") || d == "adreno") return GlVendor::Qualcomm;
    if (d == "panfrost" || d == "panthor" || d == "lima") return GlVendor::Arm;
    if (startsWith(d, "vc4") || d == "v3d") return GlVendor::Broadcom;
    if (d == "vmwgfx") return GlVendor::VMware;
    if (d == "vboxvideo") return GlVendor::VirtualBox;
    if (d == "virtio_gpu" || d == "virtio-pci") return GlVendor::Virtio;
    if (d == "qxl" || startsWith(d, "bochs") || startsWith(d, "cirrus")) return GlVendor::Qemu;
    if (d == "hyperv_drm" || d == "hyperv_fb") return GlVendor::Microsoft;
    return GlVendor::Unknown;
}

/// True when only a firmware-provided framebuffer is bound: the machine
/// has a display but no GPU driver.
bool isFirmwareFramebuffer(const std::string& d)
{
    return d == "simpledrm" || d == "simple-framebuffer" || d == "efifb" || d == "efi-framebuffer"
        || d == "vesafb" || d == "ofdrm";
}

GlVendor adapterVendor(const GpuAdapter& a)
{
    GlVendor v = vendorFromPciId(a.pciVendor);
    if (v == GlVendor::Unknown) v = a.vendorHint;
    if (v == GlVendor::Unknown) v = vendorFromKernelDriver(a.kernelDriver);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------
//  Vendors
// ---------------------------------------------------------------------

const char* vendorName(GlVendor v)
{
    switch (v) {
    case GlVendor::Nvidia:     return "NVIDIA";
    case GlVendor::Amd:        return "AMD";
    case GlVendor::Intel:      return "Intel";
    case GlVendor::Apple:      return "Apple";
    case GlVendor::Qualcomm:   return "Qualcomm";
    case GlVendor::Arm:        return "Arm";
    case GlVendor::Broadcom:   return "Broadcom";
    case GlVendor::Mesa:       return "Mesa";
    case GlVendor::Microsoft:  return "Microsoft";
    case GlVendor::VMware:     return "VMware";
    case GlVendor::VirtualBox: return "VirtualBox";
    case GlVendor::Parallels:  return "Parallels";
    case GlVendor::Virtio:     return "virtio";
    case GlVendor::Qemu:       return "QEMU";
    case GlVendor::Other:      return "other";
    case GlVendor::Unknown:    break;
    }
    return "unknown";
}

GlVendor vendorFromPciId(unsigned id)
{
    switch (id) {
    case 0x10de: case 0x12d2:  return GlVendor::Nvidia;
    case 0x1002: case 0x1022:  return GlVendor::Amd;
    case 0x8086: case 0x8087:  return GlVendor::Intel;
    case 0x106b:               return GlVendor::Apple;
    case 0x5143: case 0x17cb:  return GlVendor::Qualcomm;
    case 0x13b5:               return GlVendor::Arm;
    case 0x14e4:               return GlVendor::Broadcom;
    case 0x1414:               return GlVendor::Microsoft;
    case 0x15ad:               return GlVendor::VMware;
    case 0x80ee:               return GlVendor::VirtualBox;
    case 0x1ab8:               return GlVendor::Parallels;
    case 0x1af4:               return GlVendor::Virtio;
    case 0x1234: case 0x1b36:  return GlVendor::Qemu;
    case 0x102b:                               // Matrox
    case 0x1a03:                               // ASPEED (server BMC)
    case 0x1ed5:                               // Moore Threads
    case 0x0014:                               // Loongson
    case 0x1d17:                               // Zhaoxin
    case 0x5333:                               // S3
    case 0x6766:                               // Glenfly
                               return GlVendor::Other;
    default:                   return GlVendor::Unknown;
    }
}

GlVendor vendorFromDtCompatible(const std::string& compatible)
{
    // Device-tree compatible strings are "vendor,device" (kernel
    // Documentation/devicetree/bindings/vendor-prefixes.yaml).
    const std::string c = lower(compatible);
    if (startsWith(c, "brcm,")) return GlVendor::Broadcom;
    if (startsWith(c, "arm,")) return GlVendor::Arm;
    if (startsWith(c, "qcom,")) return GlVendor::Qualcomm;
    if (startsWith(c, "apple,")) return GlVendor::Apple;
    if (startsWith(c, "nvidia,")) return GlVendor::Nvidia;
    if (startsWith(c, "amd,")) return GlVendor::Amd;
    if (startsWith(c, "vivante,") || startsWith(c, "imagination,") || startsWith(c, "img,")
        || startsWith(c, "rockchip,") || startsWith(c, "allwinner,") || startsWith(c, "mediatek,")
        || startsWith(c, "samsung,") || startsWith(c, "fsl,") || startsWith(c, "ti,"))
        return GlVendor::Other;
    return GlVendor::Unknown;
}

GlVendor vendorFromAcpiId(const std::string& acpiVendor)
{
    const std::string v = lower(acpiVendor);
    if (v == "qcom") return GlVendor::Qualcomm;
    if (v == "msft") return GlVendor::Microsoft;
    if (v == "nvda") return GlVendor::Nvidia;
    return GlVendor::Unknown;
}

bool isVirtualGpuVendor(GlVendor v)
{
    return v == GlVendor::VMware || v == GlVendor::VirtualBox || v == GlVendor::Parallels
        || v == GlVendor::Virtio || v == GlVendor::Qemu;
}

// ---------------------------------------------------------------------
//  Library names
// ---------------------------------------------------------------------

GlLibrary classifyGlLibrary(const std::string& baseName)
{
    using Role = GlLibrary::Role;
    GlLibrary r;
    r.name = baseName;
    const std::string n = lower(baseName);
    auto set = [&](GlVendor v, Role role) { r.vendor = v; r.role = role; return r; };
    auto versioned = [&](GlVendor v, Role role) {
        r.version = nvidiaVersionFromText(baseName);
        return set(v, role);
    };

    // --- Linux and the BSDs (ELF shared objects) ---------------------
    if (contains(n, ".so")) {
        // libglvnd's vendor-neutral fronts, and a legacy libGL.
        if (startsWith(n, "libgl.so") || startsWith(n, "libglx.so") || startsWith(n, "libopengl.so")
            || startsWith(n, "libgldispatch.so") || startsWith(n, "libegl.so"))
            return set(GlVendor::Unknown, Role::Dispatch);
        // NVIDIA's proprietary user space; the version is in the name.
        if (startsWith(n, "libglx_nvidia.so") || startsWith(n, "libegl_nvidia.so")
            || startsWith(n, "libnvidia-glcore.so") || startsWith(n, "libnvidia-eglcore.so")
            || startsWith(n, "libnvidia-glsi.so") || startsWith(n, "libnvidia-tls.so")
            || startsWith(n, "libnvidia-gpucomp.so"))
            return versioned(GlVendor::Nvidia, Role::Driver);
        // AMD's proprietary OpenGL for Linux ("OpenGL Pro", /opt/amdgpu-pro):
        // its own GLX vendor library and a DRI driver, named like Mesa's, so
        // tested before Mesa's rules.
        if (startsWith(n, "libglx_amd.so") || startsWith(n, "amdgpu_dri.so"))
            return set(GlVendor::Amd, Role::Driver);
        // Mesa. libglvnd loads "libGLX_<vendor>.so.0" (libglxmapping.c);
        // before Mesa 25.0 the GL dispatch table was a separate libglapi.
        if (startsWith(n, "libglx_mesa.so") || startsWith(n, "libegl_mesa.so")
            || startsWith(n, "libglapi.so"))
            return set(GlVendor::Mesa, Role::Dispatch);
        // Mesa's drivers: one libgallium-<version>.so, with the per-driver
        // <name>_dri.so names as links to it (targets/dri/meson.build).
        if (startsWith(n, "libgallium")) {
            static const std::regex ver("libgallium-(.+)\\.so");
            std::smatch m;
            if (std::regex_search(baseName, m, ver)) r.version = m[1].str();
            return set(GlVendor::Mesa, Role::Driver);
        }
        if (contains(n, "_dri.so")) {
            if (startsWith(n, "swrast_dri") || startsWith(n, "kms_swrast_dri")
                || startsWith(n, "libdril_dri")) {
                return set(GlVendor::Mesa,
                           startsWith(n, "libdril") ? Role::Dispatch : Role::Software);
            }
            if (startsWith(n, "zink_dri") || startsWith(n, "d3d12_dri"))
                return set(GlVendor::Mesa, Role::Layered);
            return set(GlVendor::Mesa, Role::Driver);
        }
        return r;
    }

    // --- Windows (DLL module names, case-insensitive) ----------------
    if (contains(n, ".dll")) {
        if (n == "opengl32.dll") return set(GlVendor::Microsoft, Role::Dispatch);
        if (n == "nvoglv64.dll" || n == "nvoglv32.dll") return set(GlVendor::Nvidia, Role::Driver);
        // AMD registers atig6pxx/atiglpxx as the OpenGL driver and names its
        // GL implementation, atio6axx/atioglxx, separately.
        if (n == "atig6pxx.dll" || n == "atiglpxx.dll"
            || n == "atio6axx.dll" || n == "atioglxx.dll")
            return set(GlVendor::Amd, Role::Driver);
        if (startsWith(n, "ig") && (contains(n, "icd64.dll") || contains(n, "icd32.dll")))
            return set(GlVendor::Intel, Role::Driver);
        if (n == "openglon12.dll") return set(GlVendor::Microsoft, Role::Layered);
        if (n == "libgallium_wgl.dll") return set(GlVendor::Mesa, Role::Software);
        if (n == "vm3dgl64.dll" || n == "vm3dgl.dll") return set(GlVendor::VMware, Role::Driver);
        // VirtualBox: VBoxGL (Mesa-based, current), VBoxOGL (older).
        if (startsWith(n, "vboxgl") || startsWith(n, "vboxogl"))
            return set(GlVendor::VirtualBox, Role::Driver);
        return r;
    }

    // --- macOS (framework, dylib and plug-in binary names) -----------
    if (n == "opengl" || n == "libgl.dylib" || n == "libglu.dylib" || n == "glengine"
        || n == "libglimage.dylib" || n == "libglprogrammability.dylib")
        return set(GlVendor::Apple, Role::Dispatch);
    if (contains(n, "gldriver")) {
        if (startsWith(n, "appleintel")) return set(GlVendor::Intel, Role::Driver);
        if (startsWith(n, "amdradeon") || startsWith(n, "atiradeon")) {
            return set(GlVendor::Amd, Role::Driver);
        }
        if (startsWith(n, "geforce") || startsWith(n, "nvda")) {
            return set(GlVendor::Nvidia, Role::Driver);
        }
        return set(GlVendor::Apple, Role::Driver);
    }
    if (n == "applemetalopenglrenderer") return set(GlVendor::Apple, Role::Layered);
    if (n == "glrendererfloat") return set(GlVendor::Apple, Role::Software);
    return r;
}

// ---------------------------------------------------------------------
//  Versions
// ---------------------------------------------------------------------

std::string nvidiaVersionFromText(const std::string& text)
{
    // 3-digit major, 2-digit minor, optional 2-digit patch: "610.57.04",
    // "550.120", "390.157". Not matched inside a longer number.
    static const std::regex ver("(?:^|[^0-9])([0-9]{3}\\.[0-9]{2,3}(?:\\.[0-9]{2})?)(?![0-9])");
    std::smatch m;
    if (std::regex_search(text, m, ver)) return m[1].str();
    return std::string();
}

std::string nvidiaVersionFromWindowsDriver(const std::string& v)
{
    // "31.0.15.6070": the last digit of the third field and the four digits
    // of the fourth make NVIDIA's five-digit version, 560.70.
    // Firefox's GfxInfo.cpp documents the same rule.
    static const std::regex shape("^[0-9]+\\.[0-9]+\\.([0-9]+)\\.([0-9]{1,4})$");
    std::smatch m;
    if (!std::regex_match(v, m, shape)) return std::string();
    const std::string third = m[1].str();
    std::string fourth = m[2].str();
    fourth.insert(0, 4 - fourth.size(), '0');
    const std::string digits = third.substr(third.size() - 1) + fourth;
    const std::string major = digits.substr(0, 3);
    const std::string minor = digits.substr(3, 2);
    return std::to_string(std::stoi(major)) + "." + minor;
}

}  // namespace gldiag

namespace gldiag {

namespace {

/// The Mesa driver Mesa's loader would choose for an adapter, following
/// src/loader/pci_id_driver_map.h: Intel -> iris (crocus and i915 for
/// older chips), AMD -> radeonsi (r600, r300 older), NVIDIA -> nouveau or
/// zink, virtio -> virtio_gpu, VMware -> vmwgfx (svga); anything else ->
/// the kernel driver's own name.
std::string mesaDriverFor(const GpuAdapter& a)
{
    switch (vendorFromPciId(a.pciVendor)) {
    case GlVendor::Intel:  return "iris (crocus or i915 on older chips)";
    case GlVendor::Amd:    return "radeonsi (r600 or r300 on older chips)";
    case GlVendor::Nvidia: return "nouveau or zink";
    case GlVendor::Virtio: return "virtio_gpu (virgl), if the host exposes 3D";
    case GlVendor::VMware: return "vmwgfx (svga), if 3D acceleration is enabled";
    default: break;
    }
    return a.kernelDriver.empty() ? std::string("llvmpipe (software)") : a.kernelDriver;
}

/// What to do about a virtual GPU, per hypervisor.
std::string virtualGpuAdvice(GlVendor v, const std::string& name)
{
    const std::string gpu = "This is a virtual " + std::string(vendorName(v)) + " GPU"
                          + (name.empty() ? std::string() : " (" + name + ")") + ". ";
    switch (v) {
    case GlVendor::VMware:
        return gpu + "Hardware OpenGL needs \"Accelerate 3D graphics\" enabled in the virtual "
                     "machine's display settings; without it only software rendering is available.";
    case GlVendor::VirtualBox:
        return gpu + "Hardware OpenGL needs \"Enable 3D Acceleration\" in the virtual machine's "
                     "display settings (with the VMSVGA controller); without it only software "
                     "rendering is available.";
    case GlVendor::Parallels:
        return gpu + "Hardware OpenGL needs 3D acceleration enabled in the virtual machine's "
                     "graphics settings.";
    case GlVendor::Virtio:
        return gpu + "Hardware OpenGL needs the host to provide it (virgl or venus: QEMU "
                     "with -device virtio-vga-gl and a GL display); without it Mesa "
                     "renders in software.";
    case GlVendor::Qemu:
        return gpu + "QEMU's standard VGA, Bochs and QXL adapters have no 3D at all: use a "
                     "virtio GPU with virgl (-device virtio-vga-gl), or software rendering.";
    default:
        break;
    }
    return gpu + "Hardware OpenGL depends on the hypervisor offering 3D to the guest.";
}

struct Report {
    std::string body;
    std::vector<std::string> findings;
    void line(const std::string& label, const std::string& value)
    {
        std::string l = "  " + label;
        if (l.size() < 20) l.resize(20, ' ');
        body += l + ": " + value + "\n";
    }
    void find(const std::string& f)
    {
        // Two identical adapters give identical findings; say it once.
        if (std::find(findings.begin(), findings.end(), f) == findings.end()) findings.push_back(f);
    }
};

void describeUnixConclusions(const GlDriverFacts& f, const GlReportOptions& o, Report& r,
                             bool mesaClient, bool mesaHardware, bool nvidiaUser,
                             const std::string& nvidiaUserVersion)
{
    // NVIDIA: the kernel module and the user-space libraries must match.
    const std::string verdict = nvidiaVersionVerdict(f.nvidiaKernelVersion, nvidiaUserVersion);
    if (!verdict.empty()) r.find(verdict);

    bool nvidiaGpu = false, nvidiaKernel = false, nouveau = false;
    // Firmware framebuffer only: every adapter is on one (none has a real driver).
    bool onlyFirmware = !f.adapters.empty();
    for (const GpuAdapter& a : f.adapters) {
        if (!isFirmwareFramebuffer(a.kernelDriver)) onlyFirmware = false;
    }
    for (const GpuAdapter& a : f.adapters) {
        const GlVendor v = adapterVendor(a);
        if (v == GlVendor::Nvidia) nvidiaGpu = true;
        if (contains(a.kernelDriver, "nvidia")) nvidiaKernel = true;
        if (a.kernelDriver == "nouveau") nouveau = true;
        if (a.kernelDriver.empty() && a.pciVendor != 0) {
            r.find("The " + std::string(vendorName(v)) + " GPU (PCI " + hex4(a.pciVendor) + ":"
                   + hex4(a.pciDevice) + ") has no kernel driver bound, so nothing can render "
                   "on it. Install or load its driver.");
        }
        if (isVirtualGpuVendor(v)) r.find(virtualGpuAdvice(v, a.name));
    }
    if (!f.nvidiaKernelVersion.empty()) nvidiaKernel = true;
    if (onlyFirmware && f.adaptersKnown) {
        r.find("Only a firmware framebuffer is bound: no GPU kernel driver is loaded, so "
               "OpenGL can only run in software, if at all.");
    }
    if (f.adaptersKnown && f.adapters.empty() && f.adaptersNote.empty()) {
        r.find("No GPU is visible to the kernel.");
    }

    if (nvidiaKernel && !nvidiaUser && f.glxQueried && contains(lower(f.glxClient), "mesa")) {
        r.find("The NVIDIA kernel driver is loaded, but the GLX client in use is Mesa, so the "
               "X server is not running on the NVIDIA GPU. On a hybrid (PRIME) laptop, start "
               + o.programName
               + " with __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia.");
    } else if (nvidiaKernel && !nvidiaUser && f.glxQueried && f.glxError.empty()) {
        r.find("The NVIDIA kernel driver is loaded but no NVIDIA user-space GL library is in "
               "this process: check that the NVIDIA GL libraries are installed for this "
               "architecture and that libglvnd selects them (the GLX client line above).");
    }
    if (nvidiaUser && nouveau && !nvidiaKernel) {
        r.find("NVIDIA's GL libraries are loaded but the GPU runs the open nouveau kernel "
               "driver; the two do not work together. Use one stack or the other.");
    }
    if (nvidiaGpu && !nvidiaKernel && !nouveau && f.adaptersKnown) {
        r.find("An NVIDIA GPU is present with neither the NVIDIA nor the nouveau kernel "
               "driver loaded.");
    }

    // Mesa without a hardware driver: software rendering or a driver that
    // failed to load. LIBGL_DEBUG=verbose names which one and why.
    if (mesaClient && !mesaHardware) {
        std::string want;
        for (const GpuAdapter& a : f.adapters) {
            if (isFirmwareFramebuffer(a.kernelDriver)) continue;
            if (!want.empty()) want += "; ";
            want += std::string(vendorName(adapterVendor(a))) + " -> " + mesaDriverFor(a);
        }
        // Mesa 24.2 and later build every driver, llvmpipe included, into
        // one libgallium library, so a loaded driver library says a driver
        // was chosen but not whether it was hardware or software; its
        // absence still says the failure came before any was chosen.
        r.find("Mesa is the GL implementation but no Mesa driver library is loaded in this "
               "process, so the failure came before Mesa chose a driver (it falls back to "
               "software rendering when the hardware driver cannot be used)."
               + (want.empty() ? std::string() : " Mesa would use: " + want + ".")
               + " Run with LIBGL_DEBUG=verbose to see which driver Mesa tried and why it "
                 "was rejected.");
    }

    // GLX availability.
    if (!f.glxQueried) {
        bool haveDisplay = false, haveWayland = false;
        for (const auto& kv : f.environment) {
            if (kv.first == "DISPLAY") haveDisplay = true;
            if (kv.first == "WAYLAND_DISPLAY") haveWayland = true;
        }
        if (!haveDisplay && haveWayland) {
            r.find("This is a Wayland session with no X display. The 3D viewport uses GLX, so "
                   "it needs XWayland; check that XWayland is installed and DISPLAY is set.");
        }
    } else if (!f.glxError.empty()) {
        r.find("GLX: " + f.glxError + ". The X server this process talks to cannot create GL "
               "contexts (a VNC or virtual X server without the GLX extension, or indirect "
               "rendering over the network).");
    }
    if (f.remoteSession && f.remoteSessionKind == "SSH") {
        for (const auto& kv : f.environment) {
            if (kv.first == "DISPLAY" && kv.second.find("localhost:") == 0) {
                r.find("DISPLAY points at an SSH-forwarded X server. OpenGL over X forwarding "
                       "is indirect rendering, which modern drivers disable; run " + o.programName
                       + " on the machine's own desktop, or through a remote desktop that "
                         "renders on the server.");
            }
        }
    }
}

void describeWindowsConclusions(const GlDriverFacts& f, const GlReportOptions& o, Report& r,
                                bool icdLoaded,
                                bool mesaLoaded, bool layeredLoaded)
{
    bool anyGlDriver = false, anyActive = false;
    for (const GpuAdapter& a : f.adapters) {
        const GlVendor v = adapterVendor(a);
        if (a.active) anyActive = true;
        if (!a.openGlDrivers.empty()) anyGlDriver = true;
        const std::string what = a.name.empty() ? std::string(vendorName(v)) : a.name;
        if (v == GlVendor::Microsoft && contains(lower(a.name), "basic")) {
            r.find(what + ": Windows is using its generic display driver, so no GPU driver is "
                   "installed for this adapter. Install the GPU maker's driver.");
        } else if (a.active && a.openGlDriversKnown && a.openGlDrivers.empty()
                   && f.legacyOpenGlDrivers.empty() && f.compatibilityLayer.empty()) {
            r.find(what + " registers no OpenGL driver. Windows then offers only its GDI Generic "
                   "OpenGL 1.1, which the 3D viewport cannot use. Install the GPU maker's full "
                   "driver (not a trimmed or Windows Update one)"
                   + std::string(v == GlVendor::Qualcomm || v == GlVendor::Microsoft
                                 ? ", or the OpenCL, OpenGL, and Vulkan Compatibility Pack from "
                                   "the Microsoft Store."
                                 : "."));
        }
        if (isVirtualGpuVendor(v)) {
            r.find(virtualGpuAdvice(v, what)
                   + (o.installerOffersMesa ? " " + o.programName + "'s installer can also add the "
                                                "Mesa software renderer."
                                            : std::string()));
        }
    }
    if (!f.compatibilityLayer.empty()) {
        r.find("This is " + f.compatibilityLayer + ", not Windows: OpenGL comes from the host "
               "system's driver through it, so look at the host's GPU driver, and at the "
               "compatibility layer's own OpenGL support, rather than at Windows drivers.");
    }
    if (f.acceleratedGlFormats == 0 && f.genericGlFormats > 0 && f.compatibilityLayer.empty()) {
        r.find("The desktop offers no hardware-accelerated OpenGL pixel format, only Microsoft's "
               "generic (software) ones: this session has GDI Generic OpenGL 1.1 and nothing "
               "better.");
    }
    const bool rdp = f.remoteSession && f.remoteSessionKind == "Remote Desktop";
    const bool nonInteractive = f.remoteSession && !rdp;
    if (nonInteractive) {
        // Verified on Windows 11 over OpenSSH: no adapters, 36 generic
        // pixel formats, none accelerated.
        r.find("This process is not on an interactive desktop (" + f.remoteSessionKind + "). "
               "Windows gives such sessions no display adapter and only its software OpenGL, "
               "whatever GPU the machine has. Run " + o.programName + " from the desktop.");
    }
    if (f.adaptersKnown && f.adapters.empty() && !nonInteractive)
        r.find("Windows reports no display adapter.");
    if (!f.adapters.empty() && !anyActive) r.find("No display adapter is attached to the desktop.");
    if (rdp) {
        r.find("This is a Remote Desktop session. Whether OpenGL is available depends on the "
               "GPU driver's Remote Desktop support; without it Windows substitutes its "
               "software OpenGL 1.1. Try the same user on the machine's own console.");
    }
    if (!f.openGl32FromSystem) {
        r.find("opengl32.dll was loaded from " + f.openGl32Folder + ", not from the system "
               "folder: that is an application-local Mesa (software rendering), which works "
               "without any GPU driver but is slow.");
    } else if (!icdLoaded && !mesaLoaded && !layeredLoaded && anyGlDriver) {
        r.find("A GPU driver registers an OpenGL driver, but none is loaded in this process: "
               "the context failed before Windows loaded it, which usually means the adapter "
               "driving this window is not the one with the OpenGL driver.");
    }
}

void describeMacConclusions(const GlDriverFacts& f, const GlReportOptions& o, Report& r,
                            bool anyRenderer,
                            bool softwareRenderer, bool hardwareRenderer)
{
    if (f.remoteSession) {
        // Verified on macOS 15.1: a windowless CGL context works over SSH
        // (hardware accelerated). What SSH can take away is the on-screen
        // window, which needs the user's window server session.
        r.find("This process was started over SSH. OpenGL itself works there, but a window "
               "needs the logged-in user's window server session, which an SSH login may not "
               "have. If the viewport fails only when started over SSH, start " + o.programName
               + " from the desktop.");
    }
    if (!anyRenderer && !f.loadedLibraries.empty()) {
        r.find("OpenGL.framework is loaded but no renderer plug-in is: the framework found no "
               "usable GPU renderer for this context.");
    }
    // OpenGL.framework loads its software renderer beside the hardware one
    // as a fallback (seen on an Intel Mac), so only its being alone means
    // software rendering.
    if (softwareRenderer && !hardwareRenderer) {
        r.find("Only Apple's software OpenGL renderer is loaded, no GPU renderer: rendering "
               "falls back to the CPU.");
    }
    if (f.translated) {
        r.find(o.programName + " is running under Rosetta translation. OpenGL works there, but a "
               "native build avoids a layer between it and the GPU.");
    }
}

}  // namespace

std::string formatGlDriverReport(const GlDriverFacts& f, const GlReportOptions& o)
{
    Report r;
    r.body = "GL environment on "
           + (f.platform.empty() ? std::string("this system") : f.platform) + ":\n";
    if (!f.hardwareModel.empty()) {
        r.line("Model", f.hardwareModel + (f.cpu.empty() ? "" : ", " + f.cpu));
    }
    for (const auto& kv : f.environment) r.line(kv.first, kv.second);
    if (f.remoteSession) r.line("Session", "remote (" + f.remoteSessionKind + ")");
    if (f.translated) r.line("Translation", "Rosetta");
    if (!f.compatibilityLayer.empty()) r.line("Running under", f.compatibilityLayer);

    if (f.glxQueried) {
        if (!f.glxVersion.empty()) r.line("GLX version", f.glxVersion);
        if (!f.glxClient.empty()) r.line("GLX client", f.glxClient);
        if (!f.glxServer.empty()) r.line("GLX server", f.glxServer + " (screen 0)");
        if (!f.glxError.empty()) r.line("GLX", f.glxError);
    }

    if (f.adaptersKnown) {
        if (f.adapters.empty()) r.line("GPUs", "none found");
        for (const GpuAdapter& a : f.adapters) {
            std::string v = std::string(vendorName(adapterVendor(a)));
            if (a.pciVendor) v += " [" + hex4(a.pciVendor) + ":" + hex4(a.pciDevice) + "]";
            if (!a.name.empty()) v += " " + a.name;
            if (!a.kernelDriver.empty()) v += ", kernel driver " + a.kernelDriver;
            if (!a.driverVersion.empty()) {
                v += ", driver " + a.driverVersion;
                if (adapterVendor(a) == GlVendor::Nvidia) {
                    const std::string nv = nvidiaVersionFromWindowsDriver(a.driverVersion);
                    if (!nv.empty()) v += " (NVIDIA " + nv + ")";
                }
            }
            if (a.openGlDriversKnown) {
                std::string gl;
                for (const std::string& d : a.openGlDrivers) {
                    // Current drivers register a full DriverStore path.
                    const size_t slash = d.find_last_of("\\/");
                    gl += (gl.empty() ? "" : ", ")
                        + (slash == std::string::npos ? d : d.substr(slash + 1));
                }
                v += ", OpenGL driver " + (gl.empty() ? std::string("none registered") : gl);
            }
            if (a.primary) v += ", primary";
            if (!a.active) v += ", not attached to the desktop";
            r.line("GPU", v);
        }
    }
    if (!f.adaptersNote.empty()) r.line("GPUs", f.adaptersNote);
    if (!f.legacyOpenGlDrivers.empty()) {
        std::string l;
        for (const std::string& d : f.legacyOpenGlDrivers) l += (l.empty() ? "" : ", ") + d;
        r.line("OpenGLDrivers", l + " (machine-wide registration)");
    }
    if (f.acceleratedGlFormats >= 0) {
        r.line("GL formats", std::to_string(f.acceleratedGlFormats) + " accelerated, "
               + std::to_string(f.genericGlFormats) + " generic (software)");
    }

    // What the loaded libraries say.
    bool mesaClient = false, mesaHardware = false, nvidiaUser = false;
    bool icdLoaded = false, mesaLoaded = false, layeredLoaded = false;
    bool anyRenderer = false, softwareRenderer = false, metalRenderer = false;
    std::string nvidiaUserVersion, mesaVersion;
    if (f.loadedLibraries.empty()) {
        r.line("GL libraries", "none loaded in this process yet");
    } else {
        std::string names;
        for (const std::string& name : f.loadedLibraries) {
            names += (names.empty() ? "" : " ") + name;
            const GlLibrary lib = classifyGlLibrary(name);
            using Role = GlLibrary::Role;
            if (lib.vendor == GlVendor::Mesa) {
                mesaLoaded = true;
                if (lib.role == Role::Dispatch) mesaClient = true;
                if (lib.role == Role::Driver || lib.role == Role::Layered) mesaHardware = true;
                if (!lib.version.empty()) mesaVersion = lib.version;
            }
            if (lib.vendor == GlVendor::Nvidia && lib.role == Role::Driver) {
                nvidiaUser = true;
                if (nvidiaUserVersion.empty()) nvidiaUserVersion = lib.version;
            }
            if (lib.role == Role::Driver && lib.vendor != GlVendor::Mesa) {
                icdLoaded = true;
                anyRenderer = true;
            }
            if (lib.role == Role::Layered) {
                layeredLoaded = true;
                if (lib.vendor == GlVendor::Apple) metalRenderer = true;
                anyRenderer = true;
            }
            if (lib.role == Role::Software && lib.vendor == GlVendor::Apple) {
                softwareRenderer = true;
                anyRenderer = true;
            }
        }
        r.line("GL libraries", names);
    }
    // Where Mesa is built without libglvnd (OpenBSD), its own libGL is the
    // GLX client and no libGLX_mesa is loaded; the GLX vendor still says so.
    if (f.glxQueried && contains(lower(f.glxClient), "mesa")) mesaClient = true;
    if (!mesaVersion.empty()) r.line("Mesa", mesaVersion);
    if (!f.nvidiaKernelVersion.empty()) {
        r.line("NVIDIA kernel", f.nvidiaKernelVersion + " (" + f.nvidiaKernelSource + ")");
    }
    if (!nvidiaUserVersion.empty()) {
        r.line("NVIDIA user", nvidiaUserVersion + " (from the loaded driver library)");
    }

    // Environment overrides that change which implementation answers.
    for (const auto& kv : f.environment) {
        if (kv.first == "LIBGL_ALWAYS_SOFTWARE" || kv.first == "GALLIUM_DRIVER"
            || kv.first == "MESA_LOADER_DRIVER_OVERRIDE" || kv.first == "__GLX_VENDOR_LIBRARY_NAME"
            || kv.first == "LD_PRELOAD" || kv.first == "LIBGL_ALWAYS_INDIRECT") {
            r.find(kv.first + "=" + kv.second + " is set and changes which GL implementation "
                   "is used; try without it.");
        }
    }

    const std::string p = lower(f.platform);
    if (startsWith(p, "windows")) {
        describeWindowsConclusions(f, o, r, icdLoaded, mesaLoaded, layeredLoaded);
    } else if (startsWith(p, "macos")) {
        describeMacConclusions(f, o, r, anyRenderer, softwareRenderer, icdLoaded || metalRenderer);
    } else {
        describeUnixConclusions(f, o, r, mesaClient, mesaHardware, nvidiaUser, nvidiaUserVersion);
    }

    std::string out = r.body;
    if (!r.findings.empty()) {
        out += "Findings:\n";
        for (const std::string& s : r.findings) out += "  - " + s + "\n";
    }
    return out;
}

}  // namespace gldiag

std::string describeGlDriverEnvironment(const void* x11Display)
{
    return gldiag::formatGlDriverReport(gldiag::gatherGlDriverFacts(x11Display));
}

std::string describeGlDriverEnvironment(const void* x11Display,
                                        const gldiag::GlReportOptions& options)
{
    return gldiag::formatGlDriverReport(gldiag::gatherGlDriverFacts(x11Display), options);
}

std::string nvidiaVersionVerdict(const std::string& kernelModule, const std::string& userSpace)
{
    if (kernelModule.empty() || userSpace.empty()) return std::string();
    if (kernelModule == userSpace) return std::string();
    return "VERDICT: NVIDIA user-space driver " + userSpace
         + " does not match the loaded kernel module " + kernelModule
         + ". The driver packages were updated while the old kernel module stayed "
           "loaded, so every context creation fails with no better message than "
           "this. Reboot (or unload and reload the nvidia modules) so both are the "
           "same version.";
}

}  // namespace hobbycad
