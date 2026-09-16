================================================================================
  tests/ — HobbyCAD test suites
================================================================================

  Each suite is a directory with a run.sh. They are dependency-free by
  design: no test framework, no CMake target. Each run.sh compiles its own
  .cpp files directly against the already-built static library, so a suite
  can be run on its own in a second without configuring anything.

    tests/<suite>/run.sh [path/to/build]     default build dir is ./build

  Build the project first: every suite links
  build/src/libhobbycad/libhobbycad.a and refuses to run without it.

  A suite that links the library takes its Qt flags from the BUILD it is
  given, never from the machine. The library can be built without Qt
  (-DHOBBYCAD_BUILD_APP=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON), and such
  a build compiles with HOBBYCAD_HAS_QT 0. Handing the test Qt's cflags
  anyway would define QT_CORE_LIB, so the test would see a different
  Project and SketchData than the library it links, and the link would
  fail with undefined references that look like missing functions. The
  suites read Qt6_DIR out of the build's CMakeCache.txt instead. Suites
  that link no library (groupglyph, theme, drawconstrain's pure function)
  are unaffected and use the machine's Qt.

  Against a library-only build the GUI suites still FAIL, and that is
  correct: they cannot run, and a suite that cannot run must not pass.
  Run the library suites against such a build, not all of them:
  tests/library-suites.txt lists exactly which those are, and CI builds
  the library alone and runs that list.

  Run everything:

    for d in tests/*/; do [ -x "$d/run.sh" ] && "$d/run.sh"; done

--------------------------------------------------------------------------------
  The suites
--------------------------------------------------------------------------------

    browser     Objects-browser node model, plus GUI smoke tests for the
                widgets around it (parameters dialog, formula edit, CLI
                panel paging, preferences, background image dialog).
                Widget tests run under QT_QPA_PLATFORM=offscreen and need
                moc output; without it they FAIL, since the GUI has not
                been built.
    cli         CliEngine: sketch geometry commands, and an export/replay
                round trip that proves the emitted script actually parses.
                Qt-free, like the command layer it covers, so it runs
                against a library-only build.
    document    Document-level undo stack.
    drawconstrain
                Draw-then-constrain hint derivation. Links no library:
                the code under test is one pure QString function.
    groupglyph  Group indicator geometry: the square's missing lower-right
                corner, which is what makes it read as two overlapping
                objects and the first thing a tidy-up would undo. Renders
                the glyph offscreen and samples pixels; Qt only, no library.
    headers     Header hygiene: pure text checks for mistakes that compile
                and link cleanly and then crash at run time (macro-driven
                struct layouts depending on include order; a widely
                included header dragging in the 3D kernel).
    naming      Object name validation.
    opengl      Viewport capability detection.
    pager       Pager arithmetic (shared by the terminal and the GUI panel).
    project     Project save/load, file naming by feature id.
    solver      Constraint solver, including redundancy detection.
    strutil     The Qt-free string layer the command layer runs on:
                number text, positional substitution, padding, splitting
                and parsing, each claim measured against what QString
                did before Qt was removed.
    units       Unit parsing, conversion and display precision.

--------------------------------------------------------------------------------
  Writing a test here
--------------------------------------------------------------------------------

  A test that cannot fail is not worth keeping, and neither is one that
  passes without running: a suite or test that cannot run for want of Qt,
  moc output, the built library or the solver reports FAIL, never SKIP or
  PASS. Before adding a test, break the code it covers and confirm the
  test goes red; several tests in these suites exist specifically because
  a mutation exposed that an earlier version passed against broken code.

  Assert on the MODEL, not on the message. The CLI geometry commands once
  printed "Created circle..." while adding nothing to the sketch; a test
  reading the output would have passed throughout.

  Name each check as a sentence that reads as a claim about behavior, so a
  failure line says what is wrong without opening the source.

  devtest/ is separate: it holds build-time dependency verification, not
  tests of HobbyCAD's own behavior.
