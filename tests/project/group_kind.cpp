// =====================================================================
//  tests/project/group_kind.cpp — a group's kind survives save and load,
//  and an older file with no kind infers it from the "Sweep Angle" name.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
#include <hobbycad/sketch/group.h>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    std::printf("group kind\n");
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_group_kind";
    fs::remove_all(base);
    fs::create_directories(base);
    const std::string dir = base + "/A";

    // A renamed sweep-angle rig keeps its kind; a user group named like one
    // does not become a rig; both survive a round trip through the files.
    {
        Project p;
        SketchData sk;
        sk.name = "S";
        sketch::Entity line; line.type = sketch::EntityType::Line; line.id = 1;
        line.points = {Point3{0, 0, 0}, Point3{10, 0, 0}};
        sk.entities.push_back(line);
        sketch::Group rig;
        rig.id = 1; rig.name = "My rig (renamed)"; rig.kind = sketch::GroupKind::SweepAngle;
        rig.entityIds = {1};
        sketch::Group plain;
        plain.id = 2; plain.name = "Frame"; plain.entityIds = {1};
        sk.groups = {rig, plain};
        p.addSketch(sk);
        std::string err;
        check(p.save(dir, &err), "save");

        Project q;
        check(q.load(dir, &err), "load");
        check(q.sketches().size() == 1 && q.sketches()[0].groups.size() == 2, "sketch and both groups back");
        if (q.sketches().size() == 1 && q.sketches()[0].groups.size() == 2) {
            const auto& gs = q.sketches()[0].groups;
            check(gs[0].kind == sketch::GroupKind::SweepAngle && gs[0].name == "My rig (renamed)",
                  "the renamed rig is still a SweepAngle group");
            check(gs[1].kind == sketch::GroupKind::User, "the plain group is a User group");
            check(sketch::isSweepAngleGroup(gs[0]) && !sketch::isSweepAngleGroup(gs[1]),
                  "isSweepAngleGroup reads the kind");
        }
    }

    // The helpers behind the legacy rule.
    check(sketch::inferLegacyGroupKind("Sweep Angle 2") == sketch::GroupKind::SweepAngle,
          "legacy name prefix infers SweepAngle");
    check(sketch::inferLegacyGroupKind("Sweeping Frame") == sketch::GroupKind::User,
          "a name that merely starts with Sweep does not");
    sketch::GroupKind k = sketch::GroupKind::User;
    check(sketch::parseGroupKindToken("sweep_angle", k) && k == sketch::GroupKind::SweepAngle,
          "stored token parses");
    check(sketch::parseGroupKindToken("sweep", k) && k == sketch::GroupKind::SweepAngle,
          "script token parses");
    check(!sketch::parseGroupKindToken("bogus", k), "unknown token refused");
    check(sketch::sweepAngleGroupName(3) == "Sweep Angle 3", "display name");

    if (failures == 0) std::printf("project group_kind: ALL PASS\n");
    else std::printf("project group_kind: %d FAIL\n", failures);
    return failures ? 1 : 0;
}
