# HobbyCAD — Claude Code Context

## Project

HobbyCAD is a Linux-native open-source parametric 3D CAD application.
GPL-3.0-only license. Phase 0 (Foundation) — buildable app with OCCT
viewer, BREP I/O, tiered startup.

## Tech Stack

- **Kernel:** OpenCASCADE (OCCT) 7.6 — B-Rep modeling, AIS viewer
- **GUI:** Qt 6 — widgets, no QML
- **3D Viewport:** OCCT AIS + OpenGL 3.3+ (WA_PaintOnScreen, not QOpenGLWidget)
- **Build:** CMake 3.20+ / Ninja / C++17 (C++20 preferred)
- **Primary platform:** Ubuntu 24.04, GCC 13

## Repository Layout

```
HobbyCAD/                    ayourk/hobbycad (main project)
├── src/hobbycad/            Application source (gui/, core/, etc.)
├── src/libhobbycad/         Shared library
├── docs/                    Plain text documentation
├── tools/linux/             Linux build/setup scripts
├── tools/windows/           Windows build/setup scripts  
├── tools/macos/             macOS build/setup scripts
├── vcpkg-overlay-ports/     OCCT overlay port for MSVC builds
├── .github/workflows/       CI: linux-build, windows-build, macos-build
├── CMakeLists.txt           Top-level CMake
├── CMakePresets.json        Build presets (linux-debug, linux-release, etc.)
└── vcpkg.json               vcpkg manifest for Windows builds
```

Related repos (separate directories under /wd8tb/ai/):
- `HobbyCAD-vcpkg/` → ayourk/hobbycad-vcpkg (custom vcpkg registry)
- `HobbyCAD-homebrew/` → ayourk/homebrew-hobbycad (Homebrew tap)
- `HobbyCAD-libs/` → PPA library packaging (not on GitHub)
- `HobbyCAD-outside/` → Session docs, not in any repo

## Build Commands

```bash
# Linux (primary)
tools/linux/build-dev.sh clean run

# Or via presets
cmake --preset linux-debug
cmake --build --preset linux-debug

# Windows (MSYS2)
cmake --preset msys2-debug
cmake --build --preset msys2-debug

# macOS
cmake --preset macos-debug
cmake --build --preset macos-debug
```

## Coding Standards

- See `docs/coding_standards.txt` for full details
- SPDX license headers, GPL-3.0-only
- File header comments with repository name
- `namespace hobbycad {}` for all project code
- Source files: `.cpp` / `.h` (not `.cxx` / `.hxx`)
- Commit messages: double-quote safe, no internal double quotes
- Signed-off-by: Aaron Yourk <ayourk@gmail.com>

## Key Technical Constraints

- **WA_PaintOnScreen:** OCCT owns the GL context. Qt child widgets
  CANNOT overlay the viewport (render behind GL surface). All viewport
  overlays must be OCCT AIS objects or AIS_Canvas2D subclasses.
- **Z-up coordinate system:** eye(1,-1,1), up(0,0,1). ViewCube uses
  SetYup(Standard_False).
- **Do NOT suggest QOpenGLWidget** — it conflicts with OCCT's viewer.
- **Platform-specific casts:** WId→Aspect_Drawable requires
  `#ifdef _WIN32` with reinterpret_cast (Windows) vs static_cast (Linux).
- **OCCT version differences:** Ubuntu has 7.6.3, MSYS2 has 7.9.2.
  Library names changed in 7.8+ (TKSTEP→TKDESTEP, etc.).
  Use occt_pick_lib() macro in CMake for compatibility.

## CI Workflows

- **Linux:** ubuntu-24.04, apt packages, cmake --preset linux-release
- **Windows:** Two jobs — MSYS2/UCRT64/GCC + MSVC/vcpkg
- **macOS:** Homebrew tap ayourk/hobbycad, Clang on ARM64

## Current State (as of 2026-02-13)

### Recently Completed
- Viewport navigation: ViewCube, orbit rings (AIS_Canvas2D), scale bar, home button
- All three CI workflows created and being debugged
- Setup scripts for all platforms reconciled
- Homebrew tap: TBB disabled, BRepFont FreeType fix, CMake 4.x compat
- vcpkg overlay port: BRepFont whitespace fix, LICENSE_LGPL_21.txt fix
- OCCT 7.8+ library name detection (occt_pick_lib macro)
- **Bindings dialog** — accessible from View > Preferences, supports up to 3 bindings per action
  - Both keyboard shortcuts and mouse bindings (Ctrl+Shift+Alt+Button+Drag)
  - Expandable tree with bindings as child nodes under actions
  - Apply button enabled only when changes detected
  - Bindings apply to QActions at runtime (no restart needed)
  - Stored in QSettings (~/.config/HobbyCAD/HobbyCAD.conf)
  - Fixed Windows CI OpenCASCADE include path issue (vcpkg overlay port)
- **Edit menu** — skeleton menu with Cut/Copy/Paste/Delete/Select All
  - All actions initially disabled (enabled when selections exist)
  - Standard keyboard shortcuts (Ctrl+X/C/V, Del, Ctrl+A)
  - Fully integrated with Bindings dialog for customization
- **Properties panel** — right-side dock panel for object properties
  - QTreeWidget placeholder ready for property editing
  - Toggleable via View > Properties or Ctrl+P
  - Visible by default, state persists via QSettings
- **Viewport toolbar** — toolbar above viewport with icon-above-label buttons
  - Buttons: Create Sketch, Box, Extrude, Revolve | Fillet, Hole | Move, Mirror
  - Icon above text label, dropdown arrows for related actions
  - Toggleable via View > Toolbar
  - FullModeWindow receives workspaceChanged signal for toolbar updates
- **View > Workspace submenu** — Design, Render, Animation, Simulation
  - Changes toolbar buttons based on selected workspace
  - Uses QActionGroup for exclusive selection
- **CLI commands** — `select <type> <name>` and `create sketch [name]`
  - Context-aware prompt shows sketch name when in sketch mode (e.g., "Sketch Sketch1>")
  - Tab completion with contextual argument hints (press Tab or ? to see hints)
  - Documentation at `docs/cli_commands.txt`
- **Sketch geometry commands** — natural English syntax with optional keywords
  - `point [at] <x>,<y>`
  - `line [from] <x1>,<y1> to <x2>,<y2>`
  - `circle [at] <x>,<y> radius|diameter <value>`
  - `rectangle [from] <x1>,<y1> to <x2>,<y2>`
  - `arc [at] <x>,<y> radius <r> [angle] <start> to <end>`
  - Numeric values support: plain numbers, parameter names, or (expressions)
  - Parenthesized expressions preserved as single tokens (spaces allowed inside)
  - Parameter name completion when typing letters in numeric fields
- **Parameter validation** — strict naming rules enforced in CLI and GUI
  - Names must start with letter or underscore (not a digit)
  - Can contain letters, numbers, and underscores
  - Reserved words (sin, cos, sqrt, pi, etc.) cannot be used
  - Parameters dialog: invalid names turn field red, disables OK/Apply buttons
  - On project load: names starting with digit auto-prefixed with underscore
  - TODO: Apply same sanitization in Document::loadParameters() when implemented

### Pending
1. **Commit and push** — uncommitted changes above need to be committed
2. **CI verification** — all three platforms need clean pass after latest fixes
3. **libhobbycad CMakeLists** — occt_pick_lib macro needs to be applied
   to src/libhobbycad/CMakeLists.txt (delivered as standalone file)
4. **ViewCube bottom view** — may still have 45° rotation issue
5. **vcpkg baseline hash** — vcpkg-configuration.json placeholder
6. **File headers** — old format, update incrementally

## Preferences

- Keep responses concise and action-oriented
- Don't ask permission — just make changes
- Always list modified files
- Plain text / PDF preferred (no docx)
- Functional over pretty — get it working first
- When build errors are pasted, fix directly without clarifying questions
- Zip naming: hobbycad-update.zip, hobbycad-vcpkg.zip, hobbycad-homebrew.zip,
  hobbycad-libs.zip (lowercase), HobbyCAD-outside.zip (capitalized)
- Shell scripts (.sh, .csh) must have execute permissions in zips
- **Build command:** `tools/linux/build-dev.sh clean run` for clean build + run
  - Note: `run` alone skips rebuild if binary exists
  - Use `clean run` to force rebuild after code changes
