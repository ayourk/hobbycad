// =====================================================================
//  tests/project/gl_diagnostics.cpp — the GL-failure report names the cause
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  "OpenGl_Window::CreateWindow: glXCreateContext failed." told Aaron
//  nothing he did not know. The report now describes the machine and draws
//  conclusions, on every platform and for every GPU family it knows.
//
//  The conclusions are drawn from a plain GlDriverFacts, so each operating
//  system and GPU combination is tested here by describing the machine by
//  hand: a Windows laptop with no OpenGL driver, an NVIDIA box after a
//  driver update, a PRIME laptop, a Raspberry Pi, a VM without 3D, a Mac
//  over SSH. The live gatherer for the machine running the test is checked
//  for shape only, since its answers depend on that machine.
// =====================================================================
#include <hobbycad/gl_diagnostics.h>
#include <hobbycad/opengl_info.h>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <string>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::gldiag;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool has(const std::string& s, const char* needle)
{
    return s.find(needle) != std::string::npos;
}

// Set (or, with an empty value, remove) a variable in this process's own
// environment, the one the gatherers read.
static void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    SetEnvironmentVariableA(name, *value ? value : nullptr);
    _putenv_s(name, value);
#else
    if (*value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}
static std::string getEnv(const char* name)
{
#ifdef _WIN32
    char buf[1024];
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

static GpuAdapter adapter(unsigned vendor, unsigned device, const char* kernel,
                          const char* name = "")
{
    GpuAdapter a;
    a.pciVendor = vendor;
    a.pciDevice = device;
    a.kernelDriver = kernel;
    a.name = name;
    return a;
}

static GlDriverFacts linuxBox()
{
    GlDriverFacts f;
    f.platform = "Linux 6.17.0 (x86_64)";
    f.environment = {{"DISPLAY", ":0"}, {"XDG_SESSION_TYPE", "x11"}};
    f.glxQueried = true;
    f.glxVersion = "1.4";
    f.adaptersKnown = true;
    return f;
}

static GlDriverFacts windowsBox(const char* arch = "x64")
{
    GlDriverFacts f;
    f.platform = std::string("Windows (") + arch + " build)";
    f.adaptersKnown = true;
    return f;
}

static GpuAdapter winAdapter(unsigned vendor, const char* name, std::vector<std::string> gl,
                             const char* version = "")
{
    GpuAdapter a;
    a.pciVendor = vendor;
    a.name = name;
    a.openGlDrivers = std::move(gl);
    a.openGlDriversKnown = true;
    a.driverVersion = version;
    a.primary = true;
    return a;
}

static void helpers()
{
    std::printf("helpers\n");
    check(nvidiaVersionVerdict("610.43.02", "610.57.04").find("VERDICT") != std::string::npos,
          "a kernel/user-space mismatch produces a verdict");
    check(nvidiaVersionVerdict("610.57.04", "610.57.04").empty(),
          "matching versions: no verdict");
    check(nvidiaVersionVerdict("", "610.57.04").empty()
          && nvidiaVersionVerdict("610.43.02", "").empty(),
          "an unknown side: no verdict");

    check(nvidiaVersionFromText("NVRM version: NVIDIA UNIX x86_64 Kernel Module  "
                                "610.57.04  Tue Aug 12 2026")
          == "610.57.04", "NVIDIA version from the Linux NVRM line");
    check(nvidiaVersionFromText("NVIDIA UNIX x86_64 Kernel Module  550.127.05  Tue Oct  8 2024")
          == "550.127.05",
          "NVIDIA version from FreeBSD's hw.nvidia.version");
    check(nvidiaVersionFromText("libnvidia-glcore.so.550.120") == "550.120",
          "NVIDIA version from a library name");
    check(nvidiaVersionFromText("31.0.15.6070").empty()
          && nvidiaVersionFromText("libGL.so.1").empty(),
          "no false version in other version strings");

    check(nvidiaVersionFromWindowsDriver("31.0.15.6070") == "560.70",
          "Windows 31.0.15.6070 is NVIDIA 560.70");
    check(nvidiaVersionFromWindowsDriver("32.0.15.8129") == "581.29",
          "Windows 32.0.15.8129 is NVIDIA 581.29");
    check(nvidiaVersionFromWindowsDriver("27.21.14.5671") == "456.71",
          "Windows 27.21.14.5671 is NVIDIA 456.71");
    check(nvidiaVersionFromWindowsDriver("9.18.13.697") == "306.97"
          && nvidiaVersionFromWindowsDriver("9.18.13.0697") == "306.97",
          "a short fourth field is zero-padded (9.18.13.697 is 306.97)");
    check(nvidiaVersionFromWindowsDriver("1.2.3").empty()
          && nvidiaVersionFromWindowsDriver("31.0.15.60697").empty(),
          "other shapes give no NVIDIA version");
    check(nvidiaVersionFromText("NVRM version: NVIDIA UNIX Open Kernel Module for x86_64  "
                                "610.57.04  Release Build")
          == "610.57.04", "NVIDIA version from the open kernel module's NVRM line");
    check(vendorFromDtCompatible("brcm,2711-v3d") == GlVendor::Broadcom
          && vendorFromDtCompatible("arm,mali-valhall-csf") == GlVendor::Arm
          && vendorFromDtCompatible("qcom,adreno-43050c01") == GlVendor::Qualcomm
          && vendorFromDtCompatible("apple,agx-t8103") == GlVendor::Apple
          && vendorFromDtCompatible("rockchip,rk3588-mali") == GlVendor::Other
          && vendorFromDtCompatible("weird") == GlVendor::Unknown,
          "device-tree compatible strings name platform GPU vendors");
    check(vendorFromAcpiId("QCOM") == GlVendor::Qualcomm
          && vendorFromAcpiId("XXXX") == GlVendor::Unknown,
          "Windows ACPI vendor codes name the vendor");

    check(vendorFromPciId(0x10de) == GlVendor::Nvidia
          && vendorFromPciId(0x1002) == GlVendor::Amd
          && vendorFromPciId(0x8086) == GlVendor::Intel
          && vendorFromPciId(0x80ee) == GlVendor::VirtualBox
          && vendorFromPciId(0x15ad) == GlVendor::VMware
          && vendorFromPciId(0x1af4) == GlVendor::Virtio
          && vendorFromPciId(0x1414) == GlVendor::Microsoft
          && vendorFromPciId(0xabcd) == GlVendor::Unknown,
          "PCI vendor IDs map to vendors");
    check(isVirtualGpuVendor(GlVendor::VMware) && !isVirtualGpuVendor(GlVendor::Nvidia),
          "virtual vendors are known");

    using Role = GlLibrary::Role;
    auto is = [](const char* name, GlVendor v, Role r) {
        const GlLibrary l = classifyGlLibrary(name);
        return l.vendor == v && l.role == r;
    };
    check(is("libnvidia-glcore.so.610.57.04", GlVendor::Nvidia, Role::Driver)
          && classifyGlLibrary("libGLX_nvidia.so.610.57.04").version == "610.57.04",
          "NVIDIA Linux libraries, with their version");
    check(is("libGLX_mesa.so.0", GlVendor::Mesa, Role::Dispatch)
          && is("libgallium-25.2.8-0ubuntu0.24.04.2.so", GlVendor::Mesa, Role::Driver)
          && classifyGlLibrary("libgallium-25.2.8-0ubuntu0.24.04.2.so").version
             == "25.2.8-0ubuntu0.24.04.2"
          && is("iris_dri.so", GlVendor::Mesa, Role::Driver)
          && is("swrast_dri.so", GlVendor::Mesa, Role::Software)
          && is("zink_dri.so", GlVendor::Mesa, Role::Layered)
          && is("libgallium_dri.so", GlVendor::Mesa, Role::Driver)
          && is("libGL.so.19.2", GlVendor::Unknown, Role::Dispatch),
          "Mesa's libraries: front, megadriver with its version, drivers, software, layered");
    check(is("libGL.so.1", GlVendor::Unknown, Role::Dispatch)
          && is("libGLdispatch.so.0", GlVendor::Unknown, Role::Dispatch),
          "libglvnd fronts are vendor-neutral");
    check(is("amdgpu_dri.so", GlVendor::Amd, Role::Driver)
          && is("libGLX_amd.so.0", GlVendor::Amd, Role::Driver)
          && is("libglapi.so.0", GlVendor::Mesa, Role::Dispatch),
          "AMD's proprietary GL is not mistaken for Mesa; Mesa's old libglapi is Mesa");
    check(is("nvoglv64.dll", GlVendor::Nvidia, Role::Driver)
          && is("NVOGLV64.DLL", GlVendor::Nvidia, Role::Driver)
          && is("atig6pxx.dll", GlVendor::Amd, Role::Driver)
          && is("ig9icd64.dll", GlVendor::Intel, Role::Driver)
          && is("opengl32.dll", GlVendor::Microsoft, Role::Dispatch)
          && is("OpenGLOn12.dll", GlVendor::Microsoft, Role::Layered)
          && is("libgallium_wgl.dll", GlVendor::Mesa, Role::Software)
          && is("vm3dgl64.dll", GlVendor::VMware, Role::Driver)
          && is("VBoxGL.dll", GlVendor::VirtualBox, Role::Driver)
          && is("igxelpicd64.dll", GlVendor::Intel, Role::Driver)
          && is("atio6axx.dll", GlVendor::Amd, Role::Driver)
          && is("qcdx12arm64xum.dll", GlVendor::Unknown, Role::Unrelated),
          "Windows modules, case-insensitive; "
          "Qualcomm's Direct3D driver is not a GL driver");
    check(is("AMDRadeonX6000GLDriver", GlVendor::Amd, Role::Driver)
          && is("AppleIntelKBLGraphicsGLDriver", GlVendor::Intel, Role::Driver)
          && is("GeForceGLDriver", GlVendor::Nvidia, Role::Driver)
          && is("AppleMetalOpenGLRenderer", GlVendor::Apple, Role::Layered)
          && is("GLRendererFloat", GlVendor::Apple, Role::Software)
          && is("OpenGL", GlVendor::Apple, Role::Dispatch),
          "macOS renderer plug-ins and the framework");
    check(is("libc.so.6", GlVendor::Unknown, Role::Unrelated)
          && is("kernel32.dll", GlVendor::Unknown, Role::Unrelated)
          && is("Foundation", GlVendor::Unknown, Role::Unrelated),
          "unrelated libraries are not GL components");
}

static void linuxCases()
{
    std::printf("Linux and BSD machines\n");
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x10de, 0x2489, "nvidia")};
        f.nvidiaKernelVersion = "610.43.02";
        f.nvidiaKernelSource = "/sys/module/nvidia/version";
        f.glxClient = "NVIDIA Corporation 1.4";
        f.loadedLibraries = {"libGL.so.1", "libGLX_nvidia.so.610.57.04",
                             "libnvidia-glcore.so.610.57.04"};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "VERDICT") && has(r, "610.57.04") && has(r, "610.43.02"),
              "NVIDIA after a driver update: the version mismatch is the verdict");
        check(has(r, "NVIDIA [10de:2489]") && has(r, "kernel driver nvidia"),
              "and the GPU is listed with its driver");
        f.nvidiaKernelVersion = "610.57.04";
        check(!has(formatGlDriverReport(f), "VERDICT"), "matching NVIDIA versions: no verdict");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x8086, 0x9a49, "i915"), adapter(0x10de, 0x25a2, "nvidia")};
        f.nvidiaKernelVersion = "550.120";
        f.glxClient = "Mesa Project and SGI 1.4";
        f.loadedLibraries = {"libGL.so.1", "libGLX_mesa.so.0", "libgallium-25.2.8.so"};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "__NV_PRIME_RENDER_OFFLOAD=1"),
              "PRIME laptop on the Intel GPU: the offload variables are named");
        check(!has(r, "no Mesa driver library"),
              "and Mesa's driver library is recognized as loaded");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x1002, 0x73df, "amdgpu")};
        f.glxClient = "Mesa Project and SGI 1.4";
        f.loadedLibraries = {"libGL.so.1", "libGLX_mesa.so.0"};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "no Mesa driver library") && has(r, "AMD -> radeonsi")
              && has(r, "LIBGL_DEBUG=verbose"),
              "AMD GPU with Mesa failing before its driver loads: "
              "the expected driver and the debug switch are named");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x8086, 0x3e92, "i915")};
        f.loadedLibraries = {"libGL.so.1", "libGLX_mesa.so.0", "swrast_dri.so"};
        check(has(formatGlDriverReport(f), "Intel -> iris"),
              "Intel with only the software driver: iris is expected");
    }
    {
        // OpenBSD 7.9 as observed: Mesa without libglvnd, its own libGL,
        // the unversioned megadriver name, no adapter list.
        GlDriverFacts f = linuxBox();
        f.platform = "OpenBSD 7.9 (amd64)";
        f.adaptersKnown = false;
        f.adaptersNote = "this system builds its GPU drivers into the kernel";
        f.glxClient = "Mesa Project and SGI 1.4";
        f.loadedLibraries = {"libGL.so.19.2", "libgallium_dri.so"};
        check(!has(formatGlDriverReport(f), "no Mesa driver library"),
              "OpenBSD with a context: nothing flagged");
        f.loadedLibraries = {"libGL.so.19.2"};
        check(has(formatGlDriverReport(f), "no Mesa driver library"),
              "OpenBSD failing before Mesa loads a driver: caught through the GLX client string");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x10de, 0x1c82, "nouveau")};
        f.loadedLibraries = {"libGLX_nvidia.so.550.120", "libnvidia-glcore.so.550.120"};
        check(has(formatGlDriverReport(f), "nouveau kernel driver"),
              "NVIDIA libraries on nouveau: the conflict is named");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x10de, 0x2489, "")};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "has no kernel driver bound") && has(r, "neither the NVIDIA nor the nouveau"),
              "NVIDIA GPU with no driver at all");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0, 0, "simpledrm", "card0")};
        check(has(formatGlDriverReport(f), "Only a firmware framebuffer"),
              "firmware framebuffer only: no GPU driver");
        f.adapters = {adapter(0, 0, "simpledrm", "card0"),
                      adapter(0x8086, 0x9a49, "i915", "card1")};
        check(!has(formatGlDriverReport(f), "Only a firmware framebuffer"),
              "a firmware framebuffer beside a real GPU driver is not flagged");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x80ee, 0xbeef, "vboxvideo")};
        check(has(formatGlDriverReport(f), "virtual VirtualBox GPU"),
              "VirtualBox guest: 3D acceleration is named");
        f.adapters = {adapter(0x1af4, 0x1050, "virtio_gpu")};
        check(has(formatGlDriverReport(f), "virtual virtio GPU")
              && has(formatGlDriverReport(f), "virgl"),
              "virtio-gpu guest: the host's virgl is named");
        f.adapters = {adapter(0x1234, 0x1111, "", "vgapci0")};
        const std::string q = formatGlDriverReport(f);
        check(has(q, "no 3D at all") && has(q, "has no kernel driver bound")
              && !has(q, "firmware framebuffer"),
              "QEMU standard VGA with no driver (the FreeBSD VM): "
              "no 3D, and not called a firmware framebuffer");
    }
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0, 0, "v3d", "card1")};
        check(has(formatGlDriverReport(f), "Broadcom"),
              "Raspberry Pi (a platform GPU, no PCI): the vendor comes from the driver");
        f.adapters = {adapter(0, 0, "vc4-drm", "card0 (brcm,bcm2711-vc5)")};
        f.adapters[0].vendorHint = vendorFromDtCompatible("brcm,bcm2711-vc5");
        check(has(formatGlDriverReport(f),
                  "Broadcom card0 (brcm,bcm2711-vc5), kernel driver vc4-drm"),
              "a Pi's display controller: vendor from the device tree, "
              "platform driver name kept");
        f.adapters = {adapter(0, 0, "panfrost", "card0")};
        check(has(formatGlDriverReport(f), "Arm card0, kernel driver panfrost"),
              "Mali via panfrost too");
    }
    {
        GlDriverFacts f = linuxBox();
        f.glxQueried = false;
        f.environment = {{"WAYLAND_DISPLAY", "wayland-0"}, {"XDG_SESSION_TYPE", "wayland"}};
        check(has(formatGlDriverReport(f), "XWayland"),
              "Wayland without an X display: XWayland is named");
    }
    {
        GlDriverFacts f = linuxBox();
        f.remoteSession = true;
        f.remoteSessionKind = "SSH";
        f.environment = {{"DISPLAY", "localhost:10.0"}};
        check(has(formatGlDriverReport(f), "SSH-forwarded X server"),
              "SSH X forwarding: indirect rendering is named");
    }
    {
        GlDriverFacts f = linuxBox();
        f.glxVersion.clear();
        f.glxError = "glXQueryVersion failed: the X server offers no GLX";
        check(has(formatGlDriverReport(f), "without the GLX extension"),
              "an X server without GLX");
    }
    {
        GlDriverFacts f = linuxBox();
        f.environment.emplace_back("LIBGL_ALWAYS_SOFTWARE", "1");
        check(has(formatGlDriverReport(f), "LIBGL_ALWAYS_SOFTWARE=1 is set"),
              "a software override is named");
    }
    {
        GlDriverFacts f = linuxBox();
        f.platform = "FreeBSD 14.4-RELEASE (amd64)";
        f.adapters = {adapter(0x10de, 0x2489, "nvidia.ko nvidia-modeset.ko", "vgapci0")};
        f.nvidiaKernelVersion = nvidiaVersionFromText(
            "NVIDIA UNIX x86_64 Kernel Module  550.127.05  Tue Oct  8 2024");
        f.nvidiaKernelSource = "sysctl hw.nvidia.version";
        f.loadedLibraries = {"libGLX_nvidia.so.550.127.05"};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "550.127.05 (sysctl hw.nvidia.version)") && !has(r, "VERDICT")
              && !has(r, "no kernel driver"),
              "FreeBSD with NVIDIA: the module list counts as the driver, versions agree");
        f.loadedLibraries = {"libGLX_nvidia.so.550.135"};
        check(has(formatGlDriverReport(f), "VERDICT"),
              "and a FreeBSD mismatch is caught the same way");
    }
}

static void windowsCases()
{
    std::printf("Windows machines\n");
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x8086, "Intel(R) UHD Graphics 620", {})};
        f.loadedLibraries = {"opengl32.dll"};
        check(has(formatGlDriverReport(f), "registers no OpenGL driver")
              && has(formatGlDriverReport(f), "GDI Generic"),
              "an Intel laptop on a trimmed driver: no OpenGL driver registered");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x1414, "Microsoft Basic Display Adapter", {})};
        check(has(formatGlDriverReport(f), "no GPU driver is installed"),
              "Basic Display Adapter: no GPU driver");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x10de, "NVIDIA GeForce RTX 3080 Ti", {"nvoglv64"},
                                 "31.0.15.6070")};
        f.loadedLibraries = {"nvoglv64.dll", "opengl32.dll"};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "(NVIDIA 560.70)") && has(r, "OpenGL driver nvoglv64") && !has(r, "Findings"),
              "a healthy NVIDIA machine: the driver is named and nothing is flagged");
    }
    {
        // A Snapdragon as Windows reports it: ACPI\VEN_QCOM, no PCI IDs,
        // only Direct3D drivers registered.
        GlDriverFacts f = windowsBox("ARM64");
        f.adapters = {winAdapter(0, "Qualcomm(R) Adreno(TM) X1-85 GPU", {})};
        f.adapters[0].vendorHint = vendorFromAcpiId("QCOM");
        const std::string r = formatGlDriverReport(f);
        check(has(r, "Compatibility Pack") && has(r, ": Qualcomm Qualcomm(R) Adreno"),
              "Windows on ARM without a GL driver: Qualcomm from the ACPI ID, "
              "the Compatibility Pack named");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x8086, "Intel(R) HD Graphics 4000", {})};
        f.legacyOpenGlDrivers = {"MSOGL -> ig7icd64.dll"};
        const std::string r = formatGlDriverReport(f);
        check(!has(r, "registers no OpenGL driver") && has(r, "machine-wide registration"),
              "a legacy machine-wide OpenGL registration counts as a registered driver");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x10de, "NVIDIA GeForce RTX 3080 Ti",
            {"C:\\Windows\\System32\\DriverStore\\FileRepository\\"
             "nv_dispi.inf_amd64_abc\\nvoglv64.dll"})};
        f.acceleratedGlFormats = 0;
        f.genericGlFormats = 24;
        const std::string r = formatGlDriverReport(f);
        check(has(r, "OpenGL driver nvoglv64.dll,") && !has(r, "DriverStore"),
              "a full DriverStore path is shown as its file name");
        check(has(r, "no hardware-accelerated OpenGL pixel format")
              && has(r, "0 accelerated, 24 generic"),
              "no accelerated pixel format: GDI Generic is named from the formats themselves");
        f.acceleratedGlFormats = 60;
        check(!has(formatGlDriverReport(f), "no hardware-accelerated"),
              "accelerated formats present: not flagged");
    }
    {
        GlDriverFacts f = windowsBox();
        f.remoteSession = true;
        f.remoteSessionKind = "Remote Desktop";
        f.adapters = {winAdapter(0x10de, "NVIDIA RTX A4000", {"nvoglv64"})};
        check(has(formatGlDriverReport(f), "Remote Desktop session"), "Remote Desktop is named");
    }
    {
        // Windows 11 as seen over OpenSSH (the builder VM): no adapters,
        // only generic pixel formats.
        GlDriverFacts f = windowsBox();
        f.remoteSession = true;
        f.remoteSessionKind = "SSH";
        f.acceleratedGlFormats = 0;
        f.genericGlFormats = 36;
        const std::string r = formatGlDriverReport(f);
        check(has(r, "not on an interactive desktop (SSH)")
              && !has(r, "Windows reports no display adapter"),
              "a Windows SSH session: the session, not a missing adapter, is named");
        f.remoteSessionKind = "services session (session 0)";
        check(has(formatGlDriverReport(f), "not on an interactive desktop (services session"),
              "the services session is named the same way");
        f.remoteSession = false;
        check(has(formatGlDriverReport(f), "Windows reports no display adapter"),
              "an interactive session with no adapter still says so");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x1414, "Microsoft Basic Display Adapter", {})};
        f.loadedLibraries = {"libgallium_wgl.dll", "opengl32.dll"};
        f.openGl32Folder = "C:\\Program Files\\HobbyCAD\\bin";
        f.openGl32FromSystem = false;
        check(has(formatGlDriverReport(f), "application-local Mesa"),
              "HobbyCAD's bundled Mesa is recognized");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x15ad, "VMware SVGA 3D", {})};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "virtual VMware GPU") && has(r, "Accelerate 3D graphics")
              && has(r, "Mesa software renderer"),
              "a VMware guest: the 3D setting and the Mesa fallback are named");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x1002, "AMD Radeon RX 6800", {"atig6pxx.dll"})};
        f.loadedLibraries = {"opengl32.dll"};
        check(has(formatGlDriverReport(f), "none is loaded in this process"),
              "an OpenGL driver registered but never loaded is named");
    }
    {
        GlDriverFacts f = windowsBox();
        f.compatibilityLayer = "Wine 11.5";
        f.adapters = {winAdapter(0x10de, "NVIDIA GeForce RTX 3080 Ti", {}),
                      winAdapter(0x10de, "NVIDIA GeForce RTX 3080 Ti", {})};
        const std::string r = formatGlDriverReport(f);
        check(has(r, "Wine 11.5, not Windows") && !has(r, "registers no OpenGL driver"),
              "under Wine the Windows driver rules are replaced by a pointer to the host");
        f.compatibilityLayer.clear();
        const std::string w = formatGlDriverReport(f);
        const size_t first = w.find("registers no OpenGL driver");
        check(first != std::string::npos
              && w.find("registers no OpenGL driver", first + 1) == std::string::npos,
              "two identical adapters give one finding, not two");
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x8086, "Intel(R) UHD Graphics", {"ig9icd64.dll"}),
                      winAdapter(0x10de, "NVIDIA GeForce RTX 4060 Laptop GPU", {"nvoglv64"})};
        f.adapters[1].primary = false;
        f.adapters[1].active = false;
        const std::string r = formatGlDriverReport(f);
        check(has(r, "not attached to the desktop") && has(r, "primary"),
              "a hybrid laptop lists both GPUs and their roles");
    }
}

static void macCases()
{
    std::printf("macOS machines\n");
    GlDriverFacts f;
    f.platform = "macOS 15.1 (x86_64)";
    f.hardwareModel = "Macmini8,1";
    f.cpu = "Intel(R) Core(TM) i7-8700B CPU @ 3.20GHz";
    // The set an Intel Mac mini actually loads for a hardware context
    // (macOS 15.1): the software renderer rides along as the fallback.
    f.loadedLibraries = {"AppleIntelKBLGraphicsGLDriver", "GLEngine", "GLRendererFloat",
                         "OpenGL", "libGL.dylib", "libGLImage.dylib",
                         "libGLProgrammability.dylib", "libGLU.dylib"};
    std::string r = formatGlDriverReport(f);
    check(has(r, "Macmini8,1") && !has(r, "Findings"),
          "an Intel Mac on its GPU, software fallback loaded beside it: nothing flagged");
    f.remoteSession = true;
    f.remoteSessionKind = "SSH";
    r = formatGlDriverReport(f);
    check(has(r, "started over SSH") && has(r, "OpenGL itself works there"),
          "a Mac process started over SSH: the window, not OpenGL, is the risk");
    f.remoteSession = false;
    f.loadedLibraries = {"OpenGL", "GLEngine", "GLRendererFloat"};
    check(has(formatGlDriverReport(f), "Only Apple's software OpenGL renderer"),
          "the software renderer alone is named");
    f.platform = "macOS 26.0 (arm64)";
    f.loadedLibraries = {"OpenGL", "AppleMetalOpenGLRenderer"};
    f.translated = true;
    r = formatGlDriverReport(f);
    check(!has(r, "software OpenGL renderer") && has(r, "Rosetta"),
          "Apple silicon: Metal renderer, Rosetta named");
    f.loadedLibraries = {"OpenGL", "AppleMetalOpenGLRenderer", "GLRendererFloat"};
    check(!has(formatGlDriverReport(f), "Only Apple's software"),
          "Metal renderer beside the fallback: not software");
    f.loadedLibraries = {"OpenGL"};
    f.translated = false;
    check(has(formatGlDriverReport(f), "no renderer plug-in"),
          "the framework without any renderer is named");
}

static void programOptions()
{
    std::printf("the program the report is for\n");
    // Every finding that names the program or its installer, on one list:
    // PRIME and X forwarding (Linux), a VM without 3D and an SSH session
    // (Windows), SSH and Rosetta (macOS).
    std::vector<GlDriverFacts> machines;
    {
        GlDriverFacts f = linuxBox();
        f.adapters = {adapter(0x8086, 0x9a49, "i915"), adapter(0x10de, 0x25a0, "nvidia")};
        f.nvidiaKernelVersion = "610.57.04";
        f.glxClient = "Mesa Project and SGI";
        f.loadedLibraries = {"libGL.so.1", "libGLX_mesa.so.0", "libgallium-25.2.8.so"};
        machines.push_back(f);
        GlDriverFacts g = linuxBox();
        g.environment = {{"DISPLAY", "localhost:10.0"}};
        g.remoteSession = true;
        g.remoteSessionKind = "SSH";
        machines.push_back(g);
    }
    {
        GlDriverFacts f = windowsBox();
        f.adapters = {winAdapter(0x15ad, "VMware SVGA 3D", {})};
        machines.push_back(f);
        GlDriverFacts g = windowsBox();
        g.remoteSession = true;
        g.remoteSessionKind = "SSH";
        machines.push_back(g);
    }
    {
        GlDriverFacts f;
        f.platform = "macOS 15.1 (arm64)";
        f.loadedLibraries = {"OpenGL", "AppleMetalOpenGLRenderer"};
        f.remoteSession = true;
        f.remoteSessionKind = "SSH";
        f.translated = true;
        machines.push_back(f);
    }
    const char* situation[] = {"PRIME", "X forwarding", "Windows VM", "Windows SSH",
                               "Mac SSH and Rosetta"};

    GlReportOptions mesh;
    mesh.programName = "HobbyMesh";
    int namedByDefault = 0, namedForMesh = 0, strayName = 0;
    for (size_t i = 0; i < machines.size(); ++i) {
        const std::string d = formatGlDriverReport(machines[i]);
        const std::string m = formatGlDriverReport(machines[i], mesh);
        if (has(d, "HobbyCAD")) ++namedByDefault;
        if (has(m, "HobbyMesh")) ++namedForMesh;
        if (has(m, "HobbyCAD")) {
            ++strayName;
            std::printf("    HobbyCAD still named for HobbyMesh: %s\n", situation[i]);
        }
    }
    check(namedByDefault == 5, "by default every one of the five advice lines names HobbyCAD");
    check(namedForMesh == 5 && strayName == 0,
          "another program's name replaces it in all five, leaving none behind");

    const std::string macMesh = formatGlDriverReport(machines[4], mesh);
    check(has(macMesh, "start HobbyMesh from the desktop")
          && has(macMesh, "HobbyMesh is running under Rosetta"),
          "the name reads correctly inside the sentence");
    check(has(formatGlDriverReport(machines[2]),
              "HobbyCAD's installer can also add the Mesa software renderer"),
          "by default the installer's Mesa option is offered");
    check(has(formatGlDriverReport(machines[2], mesh), "HobbyMesh's installer can also add"),
          "an installer that does offer Mesa is named by the program's name");
    mesh.installerOffersMesa = false;
    const std::string vmMesh = formatGlDriverReport(machines[2], mesh);
    check(!has(vmMesh, "installer") && has(vmMesh, "Accelerate 3D graphics"),
          "a program whose installer has no Mesa option is not told to use it; "
          "the VM advice stays");
}

static void liveMachine()
{
    std::printf("this machine\n");
    const GlDriverFacts f = gatherGlDriverFacts(nullptr);
    check(!f.platform.empty(), "the gatherer names the platform");
    check(!f.glxQueried, "without a display no GLX query is attempted");
    const std::string r = describeGlDriverEnvironment(nullptr);
    std::printf("%s", r.c_str());
    check(has(r, "GL environment on ") && has(r, f.platform.c_str()),
          "the report has its heading and platform");
    check(f.adaptersKnown || !f.adaptersNote.empty(),
          "the adapter list is given, or why it is not");

    // The live wrapper must pass the options on. Only a finding that names
    // the program shows that, so pretend this process came in over SSH with
    // X forwarding: every platform gatherer except the fallback reads
    // SSH_CONNECTION, and each platform's SSH advice names the program.
    setEnv("SSH_CONNECTION", "192.0.2.1 50000 192.0.2.2 22");
    const std::string oldDisplay = getEnv("DISPLAY");
    setEnv("DISPLAY", "localhost:10.0");
    const GlDriverFacts ssh = gatherGlDriverFacts(nullptr);
    GlReportOptions mesh;
    mesh.programName = "HobbyMesh";
    const std::string m = describeGlDriverEnvironment(nullptr, mesh);
    setEnv("SSH_CONNECTION", "");
    setEnv("DISPLAY", oldDisplay.c_str());
    if (ssh.remoteSession && ssh.remoteSessionKind == "SSH") {
        check(has(m, "HobbyMesh") && !has(m, "HobbyCAD"),
              "the live report takes the options too");
    } else {
        std::printf("  (live options not checked: "
                    "this platform or session reports no SSH session)\n");
        check(!has(m, "HobbyCAD"), "the live report takes the options too");
    }
}

int main()
{
    std::printf("GL failure diagnostics\n");
    helpers();
    linuxCases();
    windowsCases();
    macCases();
    programOptions();
    liveMachine();
    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
