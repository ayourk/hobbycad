# HobbyCAD — Claude Code Context

## Project

HobbyCAD is a Linux-native open-source parametric 3D CAD application.
GPL-3.0-only license. Phase 0 (Foundation) — buildable app with OCCT
viewer, BREP I/O, tiered startup.

## Tech Stack

- **Kernel:** OpenCASCADE (OCCT) 7.9 — B-Rep modeling, AIS viewer
- **Core Types:** Custom Point2D/Rect2D/Vec3 (types.h) — Qt-free, implicit Qt conversions when available
- **GUI:** Qt 6.4.2+ — widgets, no QML
- **3D Viewport:** OCCT AIS + OpenGL 3.3+ (WA_PaintOnScreen, not QOpenGLWidget)
- **Build:** CMake 3.20+ / Ninja / C++17 (C++20 preferred)
- **Primary platform:** Ubuntu 24.04, GCC 13

## Repository Layout

```
HobbyCAD/                    ayourk/hobbycad (main project)
├── src/hobbycad/            Application source (gui/, core/, etc.)
├── src/libhobbycad/         Shared library (Qt-free core types)
│   └── hobbycad/
│       ├── types.h          Point2D, Rect2D, Vec3, container helpers
│       ├── qt_compat.h      Qt interop (toQt/fromQt conversions)
│       └── format.h         String formatting (replaces QString::number)
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
- Commit messages must be approved by user before committing
- Signed-off-by: Aaron Yourk <ayourk@gmail.com>
- No Co-Authored-By trailers

## Library Design Philosophy

- Library functions don't need to be used by the main project to justify their existence
- The library is designed to be useful to third-party developers and plugins
- Include utility functions that may be helpful even if we use alternatives internally
- **When moving code from GUI to library:** Be prepared to improve the library version
  based on the GUI implementation, especially during code duplication checks. The GUI
  often has more complete handling (e.g., additional entity types, edge cases) that the
  library version lacks. Merge the best parts from both sides rather than blindly
  replacing the GUI code with the existing library version.
- **Qt-free public API:** The library's public API uses Qt-free types (Point2D, Rect2D,
  Vec3, std::string, std::vector). When Qt 6.4.2+ is available (HOBBYCAD_HAS_QT),
  implicit conversion operators enable seamless interop with QPointF, QRectF, etc. The
  GUI uses these implicit conversions at the boundary. For containers and strings,
  explicit conversion functions are provided in qt_compat.h.

## Key Technical Constraints

- **WA_PaintOnScreen:** OCCT owns the GL context. Qt child widgets
  CANNOT overlay the viewport (render behind GL surface). All viewport
  overlays must be OCCT AIS objects or AIS_Canvas2D subclasses.
- **Z-up coordinate system:** eye(1,-1,1), up(0,0,1). ViewCube uses
  SetYup(Standard_False).
- **Do NOT suggest QOpenGLWidget** — it conflicts with OCCT's viewer.
- **Platform-specific casts:** WId→Aspect_Drawable requires
  `#ifdef _WIN32` with reinterpret_cast (Windows) vs static_cast (Linux).
- **OCCT library names:** Changed in 7.8+ (TKSTEP→TKDESTEP, etc.).
  Use occt_pick_lib() macro in CMake for compatibility with older versions.

## CI Workflows

- **Linux:** ubuntu-24.04, apt packages, cmake --preset linux-release
- **Windows:** Two jobs — MSYS2/UCRT64/GCC + MSVC/vcpkg
- **macOS:** Homebrew tap ayourk/hobbycad, Clang on ARM64

## Current State (as of 2026-02-14)

### libhobbycad Library

The core library (`src/libhobbycad/`) provides reusable CAD functionality:

**Core Types** (`hobbycad/types.h`, `hobbycad/qt_compat.h`, `hobbycad/format.h`):
- Point2D, Rect2D, Vec3 — Qt-free geometry types with implicit Qt conversion when available
- Container helpers: contains(), indexOf(), removeOne(), removeAll(), removeAt(), valueAt()
- Numeric helpers: fuzzyCompare(), fuzzyIsNull()
- Qt interop: toQt()/fromQt() for points, rects, strings; toQVector()/fromQVector() for containers
- String formatting: formatDouble(), formatStorageDouble(), format() (printf-style)
- HOBBYCAD_HAS_QT macro — set to 1 when Qt 6.4.2+ is detected, 0 otherwise

**Units** (`hobbycad/units.h`):
- Length unit conversion (mm, cm, m, in, ft)
- **Storage Convention**: All internal measurements and save files use millimeters (mm)
- Display conversion happens only at the GUI layer
- Formatting: `formatValue()`, `formatValueWithUnit()`, `formatAngle()`
- Parsing: `parseValueWithUnit()` for user input
- Angle utilities: degree/radian conversion, normalization, `atan2Degrees()`
- Precision: DisplayPrecision=4 (trailing zeros trimmed), StoragePrecision=15

**Geometry Module** (`hobbycad/geometry/`):
- `types.h` — Points, vectors, arcs, bounding boxes, intersection results
- `intersections.h` — Line-line, line-circle, line-arc, circle-circle, arc-arc intersections
- `utils.h` — Vector ops, angle functions, polygon utilities, tangent circle/arc construction, ray operations (project/distance/pointOnRay), angle snapping
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
  - Linear slots: 2 points (arc centers) + radius (half-width)
  - Arc slots: 3 points (arc center, start, end) + radius (half-width) + arcFlipped flag
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
  - **Entity snap points** (always active):
    - Endpoints, midpoints, centers, quadrant points
    - Slot centerline points (arc centers, midpoint)
    - Construction geometry provides snap points
  - **Line modes**: Two-Point, Horizontal (Y-constrained), Vertical (X-constrained), Tangent, Construction
  - **Arc modes**: 3-Point, Center-Start-End, Start-End-Radius, Tangent
  - **Tangent tools** (line and arc):
    - First click: Snap to circle/arc perimeter to set tangent point
    - Second click: Constrained to tangent ray (line) or arc path (arc)
    - Entity snaps only used if snap point lies exactly on the constrained path
  - **Modifier keys during drawing**:
    - Ctrl: Angle snap (45° increments) for lines/rectangles/linear slots
    - Alt: Disable entity snapping (constraint still applies for tangent tools)
    - Shift: Arc direction flip (>180° arcs) for arc slots, tangent arcs, and Center-Start-End arcs
- **Viewport** — ViewCube, orbit rings, scale bar, home button
- **Bindings dialog** — Keyboard/mouse binding customization
- **CLI commands** — Sketch geometry commands with natural English syntax
- **Properties panel** — Object property editing
- **Workspace toolbar** — Context-sensitive toolbars

### Pending
1. **CI verification** — all three platforms need clean pass
2. **ViewCube bottom view** — may still have 45° rotation issue
3. **vcpkg baseline hash** — vcpkg-configuration.json placeholder

## Chat History

Claude Code stores session data under `~/.claude/` in several directories:

| Directory | Format | HobbyCAD Files | Content |
|-----------|--------|----------------|---------|
| `debug/` | UUID `.txt` | 68 | Full conversation transcripts (prompts + responses + tool calls) |
| `todos/` | UUID `.json` | 101 | Per-session todo/task lists |
| `plans/` | named `.md` | 1 | Implementation plans (`cuddly-watching-cerf.md`, 268 lines) |
| `shell-snapshots/` | timestamped `.sh` | 0 | Zsh environment snapshots (no conversation content) |
| `session-env/` | UUID dirs | 0 | Session environment variables |
| `history.jsonl` | JSONL | 0 | Minimal session index |

### Key debug sessions

| File | Mentions | Lines | Date Range | Content |
|------|----------|-------|------------|---------|
| `a70b2b6e-a326-4c85-8fe2-b617c9b6e0fe.txt` | 10,487 | 2.8M | Feb 12–24 | Main dev log — 84 session continuations covering Phase 0/1 build-out |
| `42e8e323-13c9-4f72-83fb-cfcd248aa997.txt` | 73 | 11K | Feb 24–25 | CI/CD overhaul, PPA packaging, Phase 1 completion |
| `2c0385a1-5e24-49c2-b709-10039f4a37e1.txt` | 6 | 873 | Feb 14 | Dark mode QSS theme editing |

The remaining 65 debug files have 1 mention each (inherited system context only).

To search: `grep -rli "keyword" ~/.claude/debug/`

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
- **Debug logging:** Write to `build/DEBUG.log` using fprintf:
  ```cpp
  {
      static FILE* logFile = fopen("build/DEBUG.log", "w");  // "w" for first call, "a" for subsequent
      if (logFile) {
          fprintf(logFile, "label: value=%.2f\n", value);
          fflush(logFile);
      }
  }
  ```
