// =====================================================================
//  tests/project/named_points.cpp — named coordinates survive save/load
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
#include <cstdio>
#include <filesystem>

using namespace hobbycad;
namespace fs = std::filesystem;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_named_points";
    fs::remove_all(base); fs::create_directories(base);
    std::printf("named points round trip\n");
    const std::string dir = base + "/proj";
    {
        Project p;
        NamedPointData np; np.name = "corner"; np.xExpr = "10"; np.yExpr = "w/2"; np.zExpr = "5";
        check(p.addNamedPoint(np), "addNamedPoint");
        std::string err; check(p.save(dir, &err), "save");
    }
    {
        Project p; std::string err; check(p.load(dir, &err), "load");
        check(p.namedPoints().size() == 1, "one named point loads");
        if (!p.namedPoints().empty()) {
            const auto& np = p.namedPoints()[0];
            check(np.name == "corner" && np.xExpr == "10" && np.yExpr == "w/2" && np.zExpr == "5",
                  "name and component expressions survive the round trip");
        }
        // add-or-update by name
        NamedPointData up; up.name = "corner"; up.xExpr = "1"; up.yExpr = "2"; up.zExpr = "3";
        p.addNamedPoint(up);
        check(p.namedPoints().size() == 1 && p.namedPoints()[0].xExpr == "1",
              "addNamedPoint updates an existing name in place");
    }
    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
