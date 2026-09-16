# Layout measurement probes.
#
# Small standalone programs that ask Qt for the numbers instead of counting
# pixels in a screenshot. Each prints a table and exits; none needs a window
# manager, and each can be run under any locale to see how the answer moves:
#
#   cmake -S . -B build && cmake --build build
#   LC_ALL=de_DE.UTF-8 ./build/toolbarwidth $'Volumenkörper'
#
# Deliberately outside src/: these are development tools, not something the
# application ships, and they take their strings as arguments rather than
# reading the catalogs, so they work against any project.
#
# Separate from devtest/, which verifies that HobbyCAD's dependencies are
# present and buildable. Nothing here checks a dependency.
#
# Background and method: HobbyCAD-outside/ notes, and the companion write-up
# at /wd8tb/ai/cherryrgb/notes/qt-translation-layout.md
cmake_minimum_required(VERSION 3.19)
project(layout-metrics LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# toolbarwidth  what a ModelToolbar/SketchToolbar caption costs in pixels,
#               per language, including the dropdown arrow and margins
foreach(probe toolbarwidth)
    add_executable(${probe} ${probe}.cpp)
    target_link_libraries(${probe} PRIVATE Qt6::Widgets)
endforeach()
