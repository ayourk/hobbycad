=====================================================================
  packaging/dports/README.txt — DragonFly BSD port (draft)
=====================================================================

  cad/hobbycad/ is a dports port for HobbyCAD, kept here while it cannot
  be submitted. pkg-descr says what the port is and why it is not ready;
  the short version is that it needs two ports dports does not have:

    cad/opencascade   dports carries 7.8.1; HobbyCAD pins 8.0.1, which
                      builds on the BSDs only with the portability fixes
                      in the 8.0.1+p1 orig tarball (versions.json).
    cad/libslvs       no port exists; SolveSpace ships libslvs only as
                      part of the application, and HobbyCAD uses the
                      3.2p1 series with its fatal-error handler.

  Both have to land in dports before this port can.

  Building HobbyCAD on DragonFly today, outside the ports tree, is
  covered in docs/freebsd_build.txt (section 8, DragonFly BSD): which
  compiler to use, why the base gcc 8.3 cannot provide std::filesystem,
  and the Qt headers dports ships.

  The Makefile's comments record the port-specific traps: the
  compiler:c++17-lang requirement and the localbase include path
  OpenCASCADE needs on every BSD.
