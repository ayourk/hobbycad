// =====================================================================
//  tests/project/panel_models.cpp — the transform form, the timeline's
//  rules, and the project folder's files
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//  Panels kept these rules in their widgets. They are library models now
//  (sketch/transform_form.h, project_session.h, project_files.h), and the
//  move fixed:
//    - saving a .gitignore rewrote it, dropping the comments, blank lines
//      and layout a person wrote;
//    - only a .gitignore line equal to a path counted, so "*.log" or a
//      folder ignored nothing in the browser;
//    - the history could be reordered from anywhere but the timeline's own
//      drag with a feature ahead of the sketch it is built on;
//    - the timeline offered Unsuppress for rolled-back features and
//      Suppress for suppressed ones;
//    - a point picked for a relative target set the offset to the point's
//      position.
// =====================================================================
#include <hobbycad/project_files.h>
#include <hobbycad/project_session.h>
#include <hobbycad/sketch/transform_form.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static bool near(const Point2D& a, const Point2D& b)
{
    return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9;
}

static TimelineEntry entry(int id, FeatureType type, std::vector<int> deps = {})
{
    TimelineEntry e;
    e.featureId = id;
    e.type = type;
    e.dependsOn = std::move(deps);
    return e;
}

int main()
{
    std::printf("panel models\n");

    // ---- the transform form -------------------------------------------------------
    {
        TransformForm f;
        GroupTransformParams p;
        f.translate = {3, 4};
        check(f.params(p) == TransformFormNeed::None && p.kind == GroupTransformKind::Translate
                  && near(p.delta, {3, 4}),
              "a translation");
        check(f.shows(TransformFormRow::Translate, false)
                  && !f.shows(TransformFormRow::Pivot, false)
                  && f.shows(TransformFormRow::Pivot, true) && !f.turns(),
              "a translation hides the pivot, unless a group's is at stake");

        f.type = MoveType::Rotate;
        f.angle = 30;
        f.pivot = {1, 1};
        check(f.params(p) == TransformFormNeed::None && p.centerGiven && near(p.center, {1, 1})
                  && p.angleDeg == 30 && f.turns() && f.shows(TransformFormRow::Pivot, false),
              "a turn about the pivot");

        f.type = MoveType::Mirror;
        f.mirrorAxis = MirrorAxis::Vertical;
        check(f.params(p) == TransformFormNeed::None && !p.mirrorAcrossHorizontal
                  && !p.mirrorLineGiven && !f.shows(TransformFormRow::MirrorLine, false),
              "a mirror across the vertical");
        f.mirrorAxis = MirrorAxis::PickedLine;
        check(f.params(p) == TransformFormNeed::MirrorLine
                  && f.shows(TransformFormRow::MirrorLine, false)
                  && !f.shows(TransformFormRow::Pivot, false),
              "a picked mirror line needs its points, and no pivot");
        check(f.picked(TransformPoint::MirrorA, {0, 0}) == TransformPoint::MirrorB,
              "the first point asks for the second");
        f.picked(TransformPoint::MirrorB, {0, 5});
        check(f.params(p) == TransformFormNeed::None && p.mirrorLineGiven
                  && near(p.mirrorB, {0, 5}),
              "then it mirrors across them");

        f.type = MoveType::PointToPoint;
        check(f.params(p) == TransformFormNeed::FromAndTo && !f.step(), "a step needs two points");
        check(f.picked(TransformPoint::From, {1, 2}) == TransformPoint::To,
              "the from-point asks for the to-point");
        check(!f.picked(TransformPoint::To, {4, 6}) && near(*f.step(), {3, 4})
                  && f.params(p) == TransformFormNeed::None && near(p.delta, {3, 4}),
              "then it moves by the step between them");

        f.type = MoveType::PointToPosition;
        check(f.params(p) == TransformFormNeed::PointOnSelection, "a point is needed");
        f.picked(TransformPoint::OnSelection, {2, 2});
        check(near(f.target, {2, 2}), "the target starts at the point");
        f.target = {10, 0};
        check(f.params(p) == TransformFormNeed::None && near(p.delta, {8, -2})
                  && !f.shows(TransformFormRow::Reference, false),
              "then the point lands on it");
        f.relativeTarget = true;
        check(f.params(p) == TransformFormNeed::Reference
                  && f.shows(TransformFormRow::Reference, false),
              "a relative target needs its reference");
        f.picked(TransformPoint::Reference, {1, 0});
        check(near(f.target, {1, 2}), "the offset starts where the point is now");
        f.onSelection.reset();
        f.picked(TransformPoint::OnSelection, {5, 5});
        check(near(f.target, {4, 5}), "a point picked later keeps the offset relative");
        f.target = {0, 1};
        check(f.params(p) == TransformFormNeed::None && near(p.delta, {-4, -4}),
              "the point lands at the reference plus the offset");

        f.type = MoveType::FreeMove;
        f.freeMove = {1, 0};
        f.freeMoveAngle = 90;
        check(f.params(p) == TransformFormNeed::None && p.kind == GroupTransformKind::Rotate
                  && near(p.delta, {1, 0}) && p.angleDeg == 90,
              "a free move is a turn and a drag in one");

        f.reset({7, 7});
        check(f.type == MoveType::FreeMove && !f.relativeTarget && !f.from && !f.reference
                  && f.freeMoveAngle == 0 && near(f.pivot, {7, 7}),
              "reset keeps the type and starts over");
        check(TransformForm::forCommand(TransformType::Copy).copy
                  && TransformForm::forCommand(TransformType::Copy).type == MoveType::Translate
                  && TransformForm::forCommand(TransformType::Scale).type == MoveType::Scale,
              "a menu command opens its form");
        check(std::string(transformFormNeedText(TransformFormNeed::FromAndTo)).find("from-point")
                  != std::string::npos,
              "needs are explained");
    }

    // ---- the timeline -------------------------------------------------------------
    {
        const std::vector<TimelineEntry> history = {
            entry(1, FeatureType::Sketch),
            entry(2, FeatureType::Extrude, {1}),
            entry(3, FeatureType::Sketch),
        };
        check(!timelineMoveAllowed(history, 0, 2), "a sketch cannot pass what is built on it");
        check(!timelineMoveAllowed(history, 1, 0), "nor can a feature pass what it is built on");
        check(timelineMoveAllowed(history, 2, 0) && timelineMoveAllowed(history, 1, 2)
                  && timelineMoveAllowed(history, 1, 1),
              "other moves are allowed");
        check(!timelineMoveAllowed(history, 0, 3), "not past the end");

        const TimelineActions origin = timelineActions(FeatureType::Origin, false);
        const TimelineActions sketch = timelineActions(FeatureType::Sketch, false);
        const TimelineActions hidden = timelineActions(FeatureType::Extrude, true);
        check(!origin.editable && sketch.editable && sketch.exportable && !hidden.exportable,
              "the Origin offers nothing, a sketch exports");
        check(hidden.unsuppress && !sketch.unsuppress,
              "a suppressed feature offers Unsuppress, others Suppress");

        Project project;
        ProjectSession session(project);
        FeatureData base;
        base.type = FeatureType::Extrude;
        base.name = "Base";
        const int baseId = session.addFeature(base);
        FeatureData built;
        built.type = FeatureType::Fillet;
        built.name = "Built";
        built.dependsOn = {baseId};
        const int builtId = session.addFeature(built);
        FeatureData free;
        free.type = FeatureType::Extrude;
        free.name = "Free";
        const int freeId = session.addFeature(free);
        check(!session.moveFeature(builtId, 0) && session.timeline()[1].featureId == builtId,
              "the session refuses to move a feature ahead of what it is built on");
        check(session.moveFeature(freeId, 0) && session.timeline()[0].featureId == freeId,
              "and makes a move the history allows");
    }

    // ---- the project folder ---------------------------------------------------------
    {
        check(gitGlobMatch("*.log", "a.log") && !gitGlobMatch("*.log", "d/a.log")
                  && gitGlobMatch("**/b", "a/x/b") && gitGlobMatch("**/b", "b")
                  && gitGlobMatch("a/**", "a/x/y") && gitGlobMatch("a/**/z", "a/z")
                  && gitGlobMatch("f?le[0-9]", "file7") && !gitGlobMatch("f?le[!0-9]", "file7")
                  && gitGlobMatch("\\*x", "*x") && !gitGlobMatch("\\*x", "ax"),
              "globs match the way git's do");

        const std::string written =
            "# Build output\n"
            "build/\n"
            "\n"
            "*.log\n"
            "!keep.log\n"
            "/docs/draft.txt\n";
        GitIgnore g = GitIgnore::parse(written);
        check(g.text() == written, "a .gitignore reads and writes back unchanged");
        check(g.ignores("build", true) && g.ignores("build/out/x.o", false)
                  && !g.ignores("build", false),
              "a folder pattern takes the folder and what is in it");
        check(g.ignores("sub/run.log", false) && !g.ignores("keep.log", false),
              "a name pattern matches at any depth; ! takes a file back");
        check(g.ignores("docs/draft.txt", false) && !g.ignores("other/docs/draft.txt", false),
              "a pattern with a slash is relative to the project folder");
        check(g.patterns().size() == 4 && g.lists("*.log"), "the patterns, without comments");

        check(g.ignore("parts/big.step", false) && g.ignores("parts/big.step", false)
                  && !g.ignore("x.log", false),
              "ignoring a path adds a line only when git does not already");
        check(g.text().rfind("# Build output\nbuild/\n\n", 0) == 0,
              "and keeps what the person wrote");
        check(g.unignore("parts/big.step", false) && !g.ignores("parts/big.step", false)
                  && g.text().find("big.step") == std::string::npos,
              "unignoring removes its own line");
        check(g.unignore("trace.log", false) && !g.ignores("trace.log", false)
                  && g.ignores("other.log", false),
              "a path a glob ignores is taken back with !");
        const std::string beforeRefusal = g.text();
        check(g.insideIgnoredFolder("build/out/x.o") && !g.insideIgnoredFolder("build")
                  && !g.unignore("build/out/x.o", false) && g.ignores("build/out/x.o", false)
                  && g.text() == beforeRefusal,
              "nothing inside an ignored folder can be taken back, and trying changes nothing");
        check(g.ignore("trace.log", false) && g.ignores("trace.log", false)
                  && g.text().find("!/trace.log") == std::string::npos,
              "ignoring it again drops the ! line");
        check(g.ignore("a[1].txt", false) && g.ignores("a[1].txt", false)
                  && !g.ignores("a1.txt", false),
              "a name with pattern characters is ignored by name");
        check(g.ignore("out", true) && g.ignores("out", true) && !g.ignores("out", false),
              "a folder's line takes only the folder");

        const std::set<std::string> cad = {"part.hcad"};
        const std::vector<std::string> foreign = {"docs/", "README.md"};
        check(projectFileStatus("part.hcad", false, cad, foreign, g) == ProjectFileStatus::CadFile
                  && projectFileStatus("docs/a/b.pdf", false, cad, foreign, g)
                         == ProjectFileStatus::ForeignFile
                  && projectFileStatus("run.log", false, cad, foreign, g)
                         == ProjectFileStatus::GitIgnored
                  && projectFileStatus("notes.txt", false, cad, foreign, g)
                         == ProjectFileStatus::Untracked,
              "a file's status, in order of precedence");
        check(std::string(projectFileStatusText(ProjectFileStatus::Untracked))
                  == "Untracked (not in manifest)",
              "statuses have their text");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
