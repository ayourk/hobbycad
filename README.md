# HobbyCAD

**A Linux-native, open-source, parametric 3D CAD application — for hobbyists, by hobbyists.**

HobbyCAD is a parametric 3D solid modeler designed for mechanical engineering, product design, and hobbyist fabrication. It delivers professional-quality tools without proprietary software, cloud lock-in, or subscription fees.

HobbyCAD is accompanied by **HobbyMesh**, a companion mesh editing and 3D printing preparation tool inspired by Autodesk Meshmixer, built from the same codebase.

> **AI Attribution:** This project's documentation, architecture decisions, and initial code generation are produced with the assistance of Claude AI (Anthropic), with human oversight, editorial direction, and final decision-making by the project author.

## Key Principles

- **Offline-first** — No cloud dependency, no account, no telemetry. Your data stays on your machine.
- **Version control is our cloud** — All collaboration and sharing flows through git and GitHub/GitLab, not a proprietary backend.
- **Linux-first, cross-platform ready** — Developed and optimized for Linux. Windows and macOS build stubs maintained for future community contributions.
- **Depth over breadth** — Core mechanical CAD done exceptionally well, rather than a sprawling, half-finished feature set.
- **Open standards** — BREP native format (open, documented), STEP/STL/OBJ interchange. No vendor lock-in.

## Features

### HobbyCAD Core

- Parametric 3D solid modeling using B-Rep geometry (OpenCASCADE)
- 2D parametric sketch editor with constraint solver
- Feature-based modeling: extrude, revolve, fillet, chamfer, pattern, Boolean operations, sweep, loft
- Feature history tree with parametric modification
- Multi-body part design with construction bodies
- Assembly design with mates and constraints
- Python scripting and plugin system with macro recording
- Built-in git integration for project history and collaboration
- Directory-first project format (`.hcad` manifest + plain-text files) — fully inspectable, hand-editable, and diff-friendly
- Tiered startup: Full Mode (OpenGL 3.3+), Reduced Mode (GUI without 3D viewport), Command-Line Mode (headless/batch)

### HobbyMesh Companion Tool

- Mesh editing, repair, and analysis
- 3D printing preparation: Make Solid, hollowing, supports, orientation optimization, wall thickness analysis
- Mesh sculpting and remeshing
- Mesh Boolean operations
- Mesh-to-B-Rep conversion (phased implementation)
- Operates standalone or as a HobbyCAD plugin

## Technology Stack

| Layer | Library | License |
|---|---|---|
| Modeling Kernel | OpenCASCADE (OCCT) | LGPL 2.1 |
| GUI Framework | Qt 6 | LGPL 3.0 |
| 3D Viewport | OCCT AIS + OpenGL 3.3+ | LGPL 2.1 |
| Constraint Solver | SolveSpace Solver | GPL 3.0 |
| Native File Format | BREP (OCCT built-in) | LGPL 2.1 |
| STEP/STL I/O | OCCT (built-in) | LGPL 2.1 |
| OBJ I/O | OCCT / Assimp | LGPL 2.1 / BSD |
| Python Scripting | CPython + pybind11 | PSF / BSD |
| Version Control | libgit2 | GPL 2.0 + LE |

**Build requirements:** CMake 3.20+, GCC 12+ or Clang 15+, C++17 (C++20 preferred), Python 3.10+

**Primary development platform:** Ubuntu 24.04 LTS

**HobbyCAD PPA** — some dependencies are not in the Ubuntu repositories and are provided via a Launchpad PPA:

```
sudo add-apt-repository ppa:ayourk/hobbycad
sudo apt-get update
```

Current PPA packages (as of 2026-02-08): libslvs, libopenmesh, lib3mf, meshfix — all built for Jammy (22.04) and Noble (24.04). See `dev_environment_setup.txt` Sections 7–8 for details.

## Development Phases

| Phase | Goal | Key Deliverable |
|---|---|---|
| **Phase 0** | Foundation | Buildable app with OCCT viewer, BREP I/O, tiered startup |
| **Phase 1** | Basic Modeling | 2D sketch → extrude → solid, project files browser, STEP export |
| **Phase 2** | Parametric Features | Fillet, chamfer, pattern, shell, multi-body, construction bodies |
| **Phase 3** | Python & Version Control | Embedded Python, plugin system, macro recorder, git integration |
| **Phase 4** | Assemblies | Multi-part assemblies, mates, interference detection, BOM |
| **Phase 5** | HobbyMesh | Mesh editing, repair, sculpting, 3D print prep |
| **Phase 6** | Advanced Features | Mesh-to-STEP, OpenSCAD import, IGES/DXF/3MF, 2D drawings |

## System Requirements

**Minimum (Full Mode):**
- Linux (Ubuntu 22.04+ recommended), Windows, or macOS
- OpenGL 3.3+ capable GPU with 1 GB+ VRAM
- 8 GB RAM, 2 GB disk space

**Reduced Mode** works without OpenGL 3.3 — the GUI launches with all non-visual features functional (sketch editing, file conversion, scripting). The 3D viewport is replaced with an informational panel.

**Command-Line Mode** requires no display server or GPU at all — suitable for headless servers, CI/CD, and batch processing.

## Licensing

HobbyCAD and HobbyMesh are licensed under the **GNU General Public License v3.0 only** (not "or later").

- **Plugins** that link against HobbyCAD libraries must be GPL 3.0
- **Python scripts** that call the scripting API are user content, not derivative works — you may license them however you choose
- **Forks** must use a different name (the HobbyCAD and HobbyMesh names are project trademarks)
- A **commercial license** may be offered in the future for entities redistributing HobbyCAD or derivatives commercially
- **AGPL 3.0** is under consideration and may be adopted at any time; the copyright assignment policy preserves this option

See `project_definition.txt` Section 12 for the full licensing framework.

## Contributing

Contributions are welcome. All code merged into the main repository requires copyright assignment to the project author, enabling unified licensing decisions. Contributors are acknowledged in the `AUTHORS` file.

A contribution agreement (CLA) mechanism will be defined before external contributions are accepted. See `project_definition.txt` Sections 12.5–12.7 for details.

## Project Documentation

All project documentation is in plain text format:

| Document | Description |
|---|---|
| `project_definition.txt` | Project scope, design principles, file format spec, development phases, licensing framework |
| `cad_library_recommendations.txt` | Library analysis, alternatives considered, licensing, HobbyMesh stack |
| `cad_use_case_document.txt` | Use cases, target audience, competitive analysis, HobbyMesh specification |
| `dev_environment_setup.txt` | Build environment setup for Ubuntu 22.04/24.04, dependency installation, verification |
| `security_risks.txt` | Threat model, mitigations for file parsing, scripting, network, supply chain |
| `AUTHORS` | Project author and contributor listing |

## Competitive Landscape

HobbyCAD occupies a unique position in the open-source CAD ecosystem:

- **vs. FreeCAD** — Clean architecture built from scratch, unified UX instead of fragmented workbenches, opinionated design choices over committee-driven feature sprawl
- **vs. Fusion 360 / Onshape** — No cloud dependency, no subscription, no telemetry, version control through git instead of proprietary cloud backends
- **vs. OpenSCAD** — Visual parametric modeling with a GUI, not code-only; Python scripting available for those who want it
- **vs. SolidWorks / Inventor** — Free and open source, Linux-native, no license server

HobbyMesh fills the gap left by Autodesk Meshmixer (abandoned, never properly available on Linux) for mesh editing and 3D print preparation.

## Contact

Aaron Yourk — ayourk@gmail.com
