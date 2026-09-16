================================================================================
  scripts/ — Helper Scripts
================================================================================

  Helper scripts for development tasks such as code generation, release
  preparation, and CI utilities.

  Platform-specific build and environment setup scripts are in
  tools/ (not here).


  ci/: CI dependency scripts
  --------------------------

  The Linux and MSYS2 workflows call these instead of carrying their own
  copies of a dependency recipe, so a recipe is written once and every job
  uses the same one. They run the same way on a hosted runner, in a
  container, in ci-sim, or by hand.

    install-build-deps.sh deb|rpm|arch
        Installs build dependencies from the packaging, which is the only
        list of them: debian/control (apt-get build-dep),
        packaging/rpm/hobbycad.spec (dnf builddep) and
        packaging/arch/PKGBUILD (depends and makedepends). --provided
        leaves out packages a job builds from source instead; --extra adds
        what a job needs beyond the packaging.

    build-opencascade.sh, build-libslvs.sh, build-nlohmann-json.sh,
    build-libwebp.sh
        Build one pinned dependency each from its source archive, into
        /usr/local by default. OpenCASCADE and libslvs build static for
        distributions that ship neither at the pinned version.

    fetch.sh <dependency> <directory>
        Download and unpack a pinned source archive. Used by the build
        scripts, and directly by a job that keeps its own build steps.

    check-launchpad.py [--offline] [--strict] [-v]
        Finds Launchpad +sourcefiles URLs in the workflows, packaging and
        scripts and asks Launchpad what became of each version (still
        published, superseded, deleted) and whether the file still
        downloads; an active URL fails, one in a comment is a warning.
        It also compares each versions.json pin with what the PPA
        publishes per series, so a PPA upload the pins lag behind shows
        up. Needs network access; --offline does the URL scan only.

    select-msvc-toolset.ps1 -Arch x64|arm64 [-Preferred 14.44]
                            [-TripletSource DIR -TripletOut DIR]
                            [-MinRuntime VER]
        Picks the MSVC toolset and Visual C++ runtime for the Windows
        builds: MSVC 14.44 (Visual Studio 2022, v143) while the runner
        image has it, otherwise the newest toolset and its matching
        runtime, with a warning. Writes a copy of the vcpkg triplets that
        pins the same toolset, since vcpkg ignores the developer
        environment, and with -MinRuntime refuses a runtime older than the
        toolset a separate build job used.

    lib.sh
        Shared helpers, sourced by the scripts above.

    pins.sh
        Generated. The version, URL and SHA-256 of every source archive the
        scripts build, rendered from versions.json by
        tools/check-versions.py --write-pins. Change versions.json and
        regenerate; the checker reports a stale pins.sh as drift.

  Every download is checked against its pinned SHA-256 before it is
  unpacked, and an HTML error page is refused. tools/check-versions.py also
  reports a workflow that downloads a pinned source directly instead of
  going through these scripts.
