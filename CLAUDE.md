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

### Library Linkage

The core library can be built as static (default) or shared:

```bash
# Static library (default) - embedded in executable
tools/linux/build-dev.sh release static

# Shared library - separate libhobbycad.so
tools/linux/build-dev.sh release shared
```

Release build sizes (as of 2026-02-14):
- Static: ~2.3 MB executable
- Shared: ~1.9 MB executable + ~950 KB library

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

## Current State (as of 2026-02-14)

### libhobbycad Library

The core library (`src/libhobbycad/`) provides reusable CAD functionality:

**Geometry Module** (`hobbycad/geometry/`):
- `types.h` — Points, vectors, arcs, bounding boxes, intersection results
- `intersections.h` — Line-line, line-circle, line-arc, circle-circle, arc-arc intersections
- `utils.h` — Vector ops, angle functions, polygon utilities, tangent circle/arc construction
- `algorithms.h` — Advanced computational geometry:
  - Convex hull (Andrew's monotone chain)
  - Polygon simplification (Douglas-Peucker, Visvalingam-Whyatt)
  - Minimal bounding circle (Welzl's algorithm)
  - Oriented bounding box (rotating calipers)
  - 2D Boolean operations (union, intersection, difference, XOR)
  - Polygon offset with miter/round/square joins
  - Ear clipping triangulation
  - Polygon with holes triangulation (bridge insertion)
  - Delaunay triangulation (Bowyer-Watson)
  - Constrained Delaunay triangulation
  - Voronoi diagram generation
  - Path analysis (length, resampling, curvature, corner detection)
  - Point set analysis (diameter, closest pair, Hausdorff distance)

**Sketch Module** (`hobbycad/sketch/`):
- `entity.h` — Entity types (line, arc, circle, rectangle, polygon, slot, ellipse, spline, text), with font properties for text
- `constraint.h` — Constraint types (dimensional: distance, radius, diameter, angle; geometric: horizontal, vertical, parallel, perpendicular, coincident, tangent, equal, midpoint, symmetric, etc.)
- `solver.h` — Parametric constraint solver (libslvs backend, optional)
- `operations.h` — 2D operations: intersection detection, offset, fillet, chamfer, trim, extend, split, mirror, connected chain finding
- `patterns.h` — Linear, rectangular, circular, mirror patterns
- `profiles.h` — Closed profile detection for extrusion
- `background.h` — Background image with calibration, rotation, opacity, flip
- `export.h` — SVG import/export, DXF import/export
- `queries.h` — Hit testing, validation, tessellation, curve analysis
- `parsing.h` — Text parsing utilities for CLI: coordinate parsing, expression parsing, value parsing

**BREP Module** (`hobbycad/brep/`):
- `operations.h` — 3D solid operations using OpenCASCADE:
  - Extrude (regular and symmetric)
  - Revolve around axis
  - Sweep along path
  - Loft between profiles
  - Boolean operations (fuse, cut, intersect)
  - 3D fillet and chamfer
  - Shell (hollow out solid)
  - 3D offset

### GUI Features

- **Sketch Canvas** — Full 2D parametric sketch editor with:
  - All entity types (line, arc, circle, rectangle, polygon, slot, ellipse, spline, text)
  - Parametric constraints (dimension tool with distance/radius/diameter/angle)
  - Geometric constraints (horizontal, vertical, parallel, perpendicular, etc.)
  - Constraint solver integration (libslvs) with over-constraint detection
  - Trim/Extend/Split operations
  - Offset, fillet, chamfer tools
  - Mirror and pattern tools
  - Profile detection for 3D operations
  - Background image support with calibration dialog
  - Multi-selection and group operations
  - Undo/redo
- **Viewport** — ViewCube, orbit rings, scale bar, home button
- **Bindings dialog** — Keyboard/mouse binding customization
- **CLI commands** — Sketch geometry commands with natural English syntax
- **Properties panel** — Object property editing
- **Workspace toolbar** — Context-sensitive toolbars

### Pending
1. **CI verification** — all three platforms need clean pass
2. **ViewCube bottom view** — may still have 45° rotation issue
3. **vcpkg baseline hash** — vcpkg-configuration.json placeholder

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
