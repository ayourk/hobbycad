// The transform command, headless: point-to-point, copy, stored group pivot,
// "about center", mirror across a line.
#include <hobbycad/strutil.h>
#include <cstdio>
#include <cmath>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
static bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

namespace {
struct Fixture {
    CliHistory history; HeadlessDocumentHost host; CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    void rect() {
        run("create sketch XY T");
        run("line 0,0 to 100,0"); run("line 100,0 to 100,50"); run("line 100,50 to 0,50"); run("line 0,50 to 0,0");
        run("constrain coincident 1.1 2.0"); run("constrain coincident 2.1 3.0"); run("constrain coincident 3.1 4.0"); run("constrain coincident 4.1 1.0");
        run("constrain horizontal 1"); run("constrain vertical 2"); run("constrain horizontal 3"); run("constrain vertical 4");
    }
    const Project& proj() const { return *host.hostProject(); }
    // The pending sketch is private to the engine; "finish" saves it, and the
    // saved copy is what the tests read.
    bool finished = false;
    const SketchData& saved() { if (!finished) { run("finish"); finished = true; } return proj().sketches().back(); }
    const std::vector<SketchEntityData>& entities() { return saved().entities; }
    const std::vector<sketch::Group>& groups() { return saved().groups; }
};
}

int main() {
    std::printf("=== point-to-point lands the from-point on the to-point ===\n");
    {
        Fixture f; f.rect(); f.run("group R entities 1,2,3,4");
        const CliResult r = f.run("transform point-to-point 0,0 10,5 group R");
        ck(r.exitCode == 0, "accepted");
        ck(f.entities().size() >= 4 && near(f.entities()[0].points[0].x, 10) && near(f.entities()[0].points[0].y, 5), "corner 0 is at 10,5");
    }
    std::printf("=== copy: originals stay, clones move, a new group appears ===\n");
    {
        Fixture f; f.rect(); f.run("group R entities 1,2,3,4");
        const CliResult r = f.run("transform move 200,0 copy group R");
        ck(r.exitCode == 0 && contains(r.output, "R copy"), "accepted and names the copy group");
        ck(f.entities().size() == 8, "entity count doubled");
        ck(near(f.entities()[0].points[0].x, 0), "original corner unmoved");
        bool copyGroup = false; for (const auto& g : f.groups()) if (g.name == "R copy" && g.entityIds.size() == 4 && g.constraintIds.size() == 8) copyGroup = true;
        ck(copyGroup, "'R copy' has four members and their eight internal constraints");
        ck(f.entities()[4].groupId >= 0 && f.entities()[4].groupId != f.entities()[0].groupId, "clones belong to the copy group, not the source");
    }
    std::printf("=== a stored group pivot is the default center ===\n");
    {
        Fixture f; f.rect(); f.run("group R entities 1,2,3,4 pivot 0,0");
        ck(contains(f.run("groups").output, "pivot 0,0"), "groups lists the pivot");
        ck(f.run("transform rotate 90 group R").exitCode == 0, "rotate accepted");
        bool stored = false; for (const auto& g : f.groups()) if (g.name == "R" && g.hasPivot) stored = true;
        ck(stored, "group R still stores a pivot after the transform");
        // rotating about 0,0 sends corner (100,0) to (0,100)
        bool turned = false; for (const auto& e : f.entities()) if (e.id == 1 && near(e.points[1].x, 0, 1e-3) && near(e.points[1].y, 100, 1e-3)) turned = true;
        ck(turned, "geometry turned about the stored pivot, not the center");
        bool still = false; for (const auto& g : f.groups()) if (g.name == "R" && near(g.pivot.x, 0) && near(g.pivot.y, 0)) still = true;
        ck(still, "the pivot itself did not move");
    }
    std::printf("=== about center overrides the stored pivot and carries it ===\n");
    {
        Fixture f; f.rect(); f.run("group R entities 1,2,3,4 pivot 0,0");
        ck(f.run("transform rotate 180 about center group R").exitCode == 0, "accepted");
        bool carried = false; for (const auto& g : f.groups()) if (g.name == "R" && near(g.pivot.x, 100, 1e-6) && near(g.pivot.y, 50, 1e-6)) carried = true;
        ck(carried, "pivot 0,0 turned about the geometric center to 100,50");
    }
    std::printf("=== mirror across a picked line ===\n");
    {
        Fixture f; f.rect(); f.run("group R entities 1,2,3,4");
        const CliResult r = f.run("transform mirror line 0,0 1,1 group R");
        ck(r.exitCode == 0 && contains(r.output, "is now"), "accepted, H/V swapped (notes mention the change)");
        ck(f.run("transform mirror line 3,3 3,3 group R").exitCode != 0, "a zero-length line is refused");
    }
    std::printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
