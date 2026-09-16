# HobbyCAD {{VERSION}}

{{PREVIEW_NOTICE}}

HobbyCAD is a Linux-native, open-source, parametric 3D CAD application,
for hobbyists, by hobbyists: a solid modeler for mechanical design and
home fabrication, also built for macOS and Windows.

Released {{DATE}} from tag `{{TAG}}`. Every package below was built by
[GitHub Actions](https://github.com/ayourk/hobbycad/actions) from that tag.

## What changed

{{CHANGES}}

## Downloads

{{ASSETS}}

`SHA256SUMS.txt` lists every file; verify with `sha256sum -c SHA256SUMS.txt`
on Linux or macOS, or `Get-FileHash` on Windows.

## Which file

**Linux**
- Ubuntu: `sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install
  hobbycad`. The PPA carries this version for every supported series with
  the pinned Qt 6.4.2, OpenCASCADE and libslvs packages; no `.deb` is
  attached here.
- Fedora and AlmaLinux 9: the `.rpm` files. Arch: the `.pkg.tar.zst`.
- Any distribution: the `.AppImage` (make it executable and run it) or the
  `.flatpak` bundle (`flatpak install HobbyCAD-x86_64.flatpak`).

**macOS**
- `HobbyCAD-{{VERSION}}-x86_64.dmg` for Intel Macs, `-arm64.dmg` for Apple
  Silicon, `-universal.dmg` for either.

**Windows**
- `HobbyCAD-{{VERSION}}-Setup.exe`: the installer for most people. One file
  for x64 and ARM64 PCs; installs per user by default. On x64 it offers an
  optional Mesa software OpenGL for PCs without a graphics driver.
- `HobbyCAD-{{VERSION}}-x64.msi`, `-arm64.msi`: per-machine packages for
  managed installs (Group Policy, Intune).
- `HobbyCAD-{{VERSION}}-windows-x64.zip`, `-arm64.zip`: no installer; unzip
  anywhere and run `bin\hobbycad.exe`.
- Windows on ARM ships no OpenGL driver; install Microsoft's free
  "OpenCL, OpenGL, and Vulkan Compatibility Pack" from the Microsoft Store.
  The installer points at it.

## Reporting problems

A crash writes a log to `~/.config/HobbyCAD/crash.log` (Linux),
`~/Library/Logs/HobbyCAD` (macOS) or `%APPDATA%\HobbyCAD` (Windows). Please
attach it, with the output of `hobbycad --version`, to an
[issue](https://github.com/ayourk/hobbycad/issues).
