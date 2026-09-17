// =====================================================================
//  src/libhobbycad/hobbycad/gl_diagnostics.h — why a GL context failed
// =====================================================================
//
//  When the viewport cannot create its OpenGL context, the toolkit says
//  only that it failed ("glXCreateContext failed", "wglCreateContext
//  failed"). These functions describe the machine instead, so the crash
//  report names the cause.
//
//  The work is split in two:
//
//    gatherGlDriverFacts()   one implementation per platform, chosen by
//                            the build (gl_facts_unix.cpp, _windows.cpp,
//                            _macos.cpp, _other.cpp). It only collects.
//    formatGlDriverReport()  portable. It turns the facts into a report
//                            and draws the conclusions: a driver version
//                            mismatch, software rendering, no OpenGL
//                            driver installed, a virtual GPU, and so on.
//
//  Because the conclusions are drawn from a plain structure, any
//  operating system and GPU combination can be tested on any machine by
//  filling the structure by hand.
//
//  describeGlDriverEnvironment() puts the two together for the caller;
//  hobbycad/opengl_info.h includes this header, so its callers see it.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GL_DIAGNOSTICS_H
#define HOBBYCAD_GL_DIAGNOSTICS_H

#include "core.h"

#include <string>
#include <utility>
#include <vector>

namespace hobbycad {
namespace gldiag {

/// Who made a GPU or a GL driver library.
enum class GlVendor {
    Unknown,
    Nvidia,
    Amd,
    Intel,
    Apple,
    Qualcomm,
    Arm,            ///< Mali (Panfrost, Lima)
    Broadcom,       ///< VideoCore (vc4, v3d)
    Mesa,           ///< Mesa's own libraries, whatever GPU they drive
    Microsoft,      ///< GDI Generic, GL-on-D3D12, Basic Display Adapter, Hyper-V
    VMware,
    VirtualBox,
    Parallels,
    Virtio,         ///< virtio-gpu (virgl, venus)
    Qemu,           ///< QEMU's emulated VGA and QXL
    Other,          ///< a known vendor with no GL-specific handling
};

/// A name for a vendor, for reports.
HOBBYCAD_EXPORT const char* vendorName(GlVendor v);

/// The vendor behind a PCI vendor ID (0x10de NVIDIA, 0x1002 AMD, ...).
HOBBYCAD_EXPORT GlVendor vendorFromPciId(unsigned pciVendorId);

/// The vendor behind a device-tree "compatible" string ("brcm,2711-v3d",
/// "arm,mali-valhall-csf", "qcom,adreno", "apple,agx-t8103").
HOBBYCAD_EXPORT GlVendor vendorFromDtCompatible(const std::string& compatible);

/// The vendor behind a Windows ACPI vendor code ("QCOM").
HOBBYCAD_EXPORT GlVendor vendorFromAcpiId(const std::string& acpiVendor);

/// True for vendors whose GPUs are virtual (a hypervisor's device).
HOBBYCAD_EXPORT bool isVirtualGpuVendor(GlVendor v);

/// What a loaded library or module is, judged from its file name.
struct GlLibrary {
    std::string name;                   ///< base name as found
    GlVendor vendor = GlVendor::Unknown;
    enum class Role {
        Unrelated,      ///< not a GL component
        Dispatch,       ///< the vendor-neutral front (libglvnd, opengl32.dll, OpenGL.framework)
        Driver,         ///< a hardware driver's GL implementation
        Software,       ///< a software rasterizer or a fallback implementation
        Layered,        ///< GL on another API (GL-on-D3D12, Zink)
    } role = Role::Unrelated;
    std::string version;                ///< from the name, when it carries one
};

/// Classify a library or module base name ("libnvidia-glcore.so.610.57.04",
/// "nvoglv64.dll", "AMDRadeonX6000GLDriver").
HOBBYCAD_EXPORT GlLibrary classifyGlLibrary(const std::string& baseName);

/// One display adapter as the platform reports it.
struct GpuAdapter {
    std::string name;               ///< marketing name, when the platform has one
    unsigned pciVendor = 0;         ///< 0 when not PCI or not known
    unsigned pciDevice = 0;
    /// Vendor for adapters that are not PCI devices: a Windows ACPI adapter
    /// ("ACPI\\VEN_QCOM"), an Arm SoC GPU named by its device-tree
    /// "compatible" string. Unknown when the PCI ID says it.
    GlVendor vendorHint = GlVendor::Unknown;
    std::string kernelDriver;       ///< Linux/BSD kernel driver (nvidia, amdgpu, i915, ...)
    std::string driverVersion;      ///< as the platform reports it
    /// Windows: the OpenGL driver(s) the display driver registers. Empty
    /// means it registers none, so Windows can offer only its GDI Generic
    /// software OpenGL 1.1.
    std::vector<std::string> openGlDrivers;
    bool openGlDriversKnown = false;
    bool active = true;             ///< attached to the desktop (Windows)
    bool primary = false;
};

/// Everything a platform could find out without a GL context.
struct GlDriverFacts {
    std::string platform;           ///< "Linux", "FreeBSD", "Windows (x64)", "macOS 15.1", ...

    /// Environment and session: variables that are set, in order.
    std::vector<std::pair<std::string, std::string>> environment;
    bool remoteSession = false;     ///< Windows Remote Desktop, macOS/Unix SSH
    std::string remoteSessionKind;
    bool translated = false;        ///< macOS: running under Rosetta
    /// A compatibility layer between the program and the real OS, such as
    /// Wine ("Wine 11.5"): its OpenGL comes from the host's driver, so the
    /// Windows driver registrations do not apply.
    std::string compatibilityLayer;

    /// GLX (X11 only). glxQueried is false when there was no display.
    bool glxQueried = false;
    std::string glxVersion;
    std::string glxClient;
    std::string glxServer;
    std::string glxError;

    /// Display adapters. adaptersKnown is false where the platform gives no list.
    std::vector<GpuAdapter> adapters;
    bool adaptersKnown = false;
    std::string adaptersNote;       ///< why the list is missing or partial

    /// GL-related libraries or modules mapped into this process (base names).
    std::vector<std::string> loadedLibraries;

    /// Windows: the folder opengl32.dll was loaded from, and whether that is
    /// the system folder. An application-local copy is a bundled Mesa (an
    /// installer can ship one), not Microsoft's.
    std::string openGl32Folder;
    bool openGl32FromSystem = true;

    /// Windows: machine-wide OpenGL drivers under the legacy
    /// "Windows NT\\CurrentVersion\\OpenGLDrivers" key.
    std::vector<std::string> legacyOpenGlDrivers;

    /// Windows: pixel formats offered on the desktop that support OpenGL,
    /// split into hardware-accelerated ones and Microsoft's generic
    /// (software) ones; -1 when not read. None accelerated means only GDI
    /// Generic OpenGL 1.1 is available to this session.
    int acceleratedGlFormats = -1;
    int genericGlFormats = -1;

    /// NVIDIA kernel driver version (Linux, FreeBSD), and where it came from.
    std::string nvidiaKernelVersion;
    std::string nvidiaKernelSource;

    std::string hardwareModel;      ///< macOS hw.model
    std::string cpu;                ///< macOS machdep.cpu.brand_string
};

/// What the report may say about the program it is written for. The advice
/// names the program ("start HobbyCAD from the desktop") and may point at
/// its installer, so a program other than HobbyCAD sets these.
struct GlReportOptions {
    /// The program's name as the user knows it.
    std::string programName = "HobbyCAD";
    /// True when the program's installer can add the Mesa software renderer
    /// (HobbyCAD's Windows installer can); the report then suggests it where
    /// no GPU driver can help, as in a virtual machine without 3D.
    bool installerOffersMesa = true;
};

/// Collect the facts for this machine and process. `x11Display` is an X11
/// Display* as void*, or nullptr; it is only used where GLX exists.
HOBBYCAD_EXPORT GlDriverFacts gatherGlDriverFacts(const void* x11Display);

/// The report, with conclusions, for a set of facts. Portable.
HOBBYCAD_EXPORT std::string formatGlDriverReport(
    const GlDriverFacts& facts, const GlReportOptions& options = GlReportOptions());

/// "610.57.04" out of any text carrying an NVIDIA driver version: the
/// Linux /proc/driver/nvidia/version line, FreeBSD's hw.nvidia.version,
/// a library name. Empty when none is found.
HOBBYCAD_EXPORT std::string nvidiaVersionFromText(const std::string& text);

/// NVIDIA's own version ("560.70") from a Windows driver version
/// ("31.0.15.6070"): the last digit of the third field and the fourth field
/// zero-padded to four digits (9.18.13.697 is 306.97). Empty when the
/// string does not have that shape.
HOBBYCAD_EXPORT std::string nvidiaVersionFromWindowsDriver(const std::string& driverVersion);

}  // namespace gldiag

/// Why a GL context could not be created, in words a person can act on.
///
/// Describes the machine as the failing process sees it, without a context:
/// the platform, the display adapters and their drivers, the GL libraries
/// loaded, the session, and conclusions drawn from them (an NVIDIA
/// kernel/user-space version mismatch, software rendering, no OpenGL driver
/// registered, a virtual GPU without 3D, a remote session). Meant to be
/// appended to the exception message where the viewport gives up. Works on
/// Linux, the BSDs, Windows and macOS; the parts are above.
///
/// `x11Display` is an X11 Display* (as void*) used for the GLX query where
/// X11 exists, or nullptr; other platforms ignore it.
HOBBYCAD_EXPORT std::string describeGlDriverEnvironment(const void* x11Display);

/// The same, for a program other than HobbyCAD: `options` gives its name and
/// whether its installer can add the Mesa software renderer.
HOBBYCAD_EXPORT std::string describeGlDriverEnvironment(
    const void* x11Display, const gldiag::GlReportOptions& options);

/// The verdict line for an NVIDIA kernel/user-space version pair; empty when
/// either is unknown or they match. Split out so it can be tested without
/// the machine it runs on being broken.
HOBBYCAD_EXPORT std::string nvidiaVersionVerdict(const std::string& kernelModule,
                                                  const std::string& userSpace);

}  // namespace hobbycad

#endif  // HOBBYCAD_GL_DIAGNOSTICS_H
