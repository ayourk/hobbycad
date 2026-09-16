// =====================================================================
//  tests/document/undo_stack.cpp — document-level undo/redo
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/document_undo.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static FeatureData F(int id, const char* name, FeatureType t = FeatureType::Extrude) {
    FeatureData f; f.id = id; f.name = name; f.type = t; return f;
}
static std::string names(const std::vector<FeatureData>& v) {
    std::string s;
    for (const auto& f : v) { s += f.name; s += " "; }
    return s.empty() ? "(empty)" : s;
}

int main() {
    std::printf("document undo/redo\n");

    // ---- add / undo / redo -------------------------------------------
    {
        std::vector<FeatureData> rec{F(1,"Sketch1",FeatureType::Sketch)};
        DocumentUndoStack st;
        FeatureData ex = F(2,"Extrude1");
        rec.push_back(ex);
        st.push(DocumentCommand::addFeature(ex, 1));

        check(st.canUndo() && !st.canRedo(), "push enables undo, not redo");
        check(st.undoDescription() == "Add Extrude1", "description derived from the feature");

        DocumentCommand c = st.undo();
        check(applyUndo(c, rec), "undo applies");
        check(names(rec) == "Sketch1 ", "the added feature is gone");
        check(!st.canUndo() && st.canRedo(), "stack moved to redo");

        c = st.redo();
        check(applyRedo(c, rec), "redo applies");
        check(names(rec) == "Sketch1 Extrude1 ", "the feature is back");
    }

    // ---- delete restores POSITION, not just existence ----------------
    {
        std::vector<FeatureData> rec{F(1,"A"), F(2,"B"), F(3,"C")};
        DocumentUndoStack st;
        st.push(DocumentCommand::deleteFeature(rec[1], 1));
        rec.erase(rec.begin() + 1);
        check(names(rec) == "A C ", "B removed");

        check(applyUndo(st.undo(), rec), "undo delete applies");
        check(names(rec) == "A B C ", "B restored IN THE MIDDLE, not appended");
    }

    // ---- modify round trip -------------------------------------------
    {
        std::vector<FeatureData> rec{F(1,"Extrude1")};
        FeatureData before = rec[0];
        FeatureData after = before; after.name = "Base"; after.suppressed = true;
        rec[0] = after;
        DocumentUndoStack st;
        st.push(DocumentCommand::modifyFeature(before, after));

        check(applyUndo(st.undo(), rec), "undo modify applies");
        check(rec[0].name == "Extrude1" && !rec[0].suppressed, "name and suppression reverted");
        check(applyRedo(st.redo(), rec), "redo modify applies");
        check(rec[0].name == "Base" && rec[0].suppressed, "re-applied");
    }

    // ---- reorder ------------------------------------------------------
    {
        std::vector<FeatureData> rec{F(1,"A"), F(2,"B"), F(3,"C")};
        DocumentUndoStack st;
        st.push(DocumentCommand::reorderFeature(0, 2));
        FeatureData a = rec[0]; rec.erase(rec.begin()); rec.insert(rec.begin()+2, a);
        check(names(rec) == "B C A ", "A moved to the end");
        check(applyUndo(st.undo(), rec), "undo reorder applies");
        check(names(rec) == "A B C ", "order restored");
    }

    // ---- a new edit clears the redo future ---------------------------
    {
        DocumentUndoStack st;
        st.push(DocumentCommand::addFeature(F(1,"A"), 0));
        st.undo();
        check(st.canRedo(), "redo available after undo");
        st.push(DocumentCommand::addFeature(F(2,"B"), 0));
        check(!st.canRedo(), "a new edit discards the redo stack");
    }

    // ---- compound: one step, all-or-nothing --------------------------
    {
        std::vector<FeatureData> rec{F(1,"A")};
        DocumentUndoStack st;
        st.beginCompound("Edit Sketch1");
        st.push(DocumentCommand::addFeature(F(2,"B"), 1));
        st.push(DocumentCommand::addFeature(F(3,"C"), 2));
        st.endCompound();
        rec.push_back(F(2,"B")); rec.push_back(F(3,"C"));

        check(st.undoLevels() == 1, "two pushes became ONE undo step");
        check(st.undoDescription() == "Edit Sketch1", "compound keeps its description");
        check(applyUndo(st.undo(), rec), "compound undo applies");
        check(names(rec) == "A ", "BOTH features removed by one undo");
    }

    // ---- a single-operation compound is unwrapped --------------------
    {
        DocumentUndoStack st;
        st.beginCompound("Edit Sketch1");
        st.push(DocumentCommand::deleteFeature(F(9,"Extrude1"), 0));
        st.endCompound();
        check(st.undoDescription() == "Delete Extrude1",
              "one-operation compound keeps the real description, not 'Multiple changes'");
    }

    // ---- an empty compound pushes nothing ----------------------------
    {
        DocumentUndoStack st;
        st.beginCompound("Edit Sketch1");
        st.endCompound();
        check(!st.canUndo(), "an edit session that changed nothing adds no undo step");
    }

    // ---- stale commands are REFUSED, not guessed at ------------------
    {
        std::vector<FeatureData> rec{F(1,"A")};
        // Undoing an add for a feature that is not present must fail rather
        // than remove something else.
        const bool ok = applyUndo(DocumentCommand::addFeature(F(99,"Ghost"), 0), rec);
        check(!ok, "undoing an add for a missing feature is refused");
        check(names(rec) == "A ", "and the recipe is untouched");

        const bool ok2 = applyUndo(DocumentCommand::modifyFeature(F(99,"X"), F(99,"Y")), rec);
        check(!ok2, "modifying a missing feature is refused");

        const bool ok3 = applyUndo(DocumentCommand::reorderFeature(0, 7), rec);
        check(!ok3, "an out-of-range reorder is refused");
    }

    // ---- a compound that fails part-way changes NOTHING --------------
    {
        std::vector<FeatureData> rec{F(1,"A"), F(2,"B")};
        std::vector<DocumentCommand> subs{
            DocumentCommand::addFeature(F(2,"B"), 1),      // undoes fine
            DocumentCommand::addFeature(F(98,"Ghost"), 0)  // cannot undo
        };
        const bool ok = applyUndo(DocumentCommand::compound(subs, "Mixed"), rec);
        check(!ok, "a compound with an inapplicable step fails");
        check(names(rec) == "A B ", "and leaves the recipe EXACTLY as it was");
    }

    // ---- depth limit --------------------------------------------------
    {
        DocumentUndoStack st(3);
        for (int i = 0; i < 5; ++i) st.push(DocumentCommand::addFeature(F(i,"X"), i));
        check(st.undoLevels() == 3, "stack trims to maxSize, oldest first");
    }

    // ---- the shapes the GUI now records -----------------------------
    // Extrude carries a dependency on its sketch and its own inputs; undo
    // must restore all of it, not just the name.
    {
        FeatureData sk = F(1, "Sketch1", FeatureType::Sketch);
        FeatureData ex = F(2, "Extrude1", FeatureType::Extrude);
        ex.dependsOn = {1};
        std::vector<FeatureData> rec{sk};
        DocumentUndoStack st;

        rec.push_back(ex);
        st.push(DocumentCommand::addFeature(ex, 1, "Extrude Sketch1"));
        check(st.undoDescription() == "Extrude Sketch1", "extrude keeps its own description");

        check(applyUndo(st.undo(), rec), "undo extrude applies");
        check(names(rec) == "Sketch1 ", "extrude removed, sketch untouched");
        check(applyRedo(st.redo(), rec), "redo extrude applies");
        check(rec.size() == 2 && rec[1].dependsOn.size() == 1 && rec[1].dependsOn[0] == 1,
              "the dependency on the sketch survives undo and redo");
    }

    // Suppression is a recipe change, so it round-trips like any other.
    {
        std::vector<FeatureData> rec{F(1,"Extrude1")};
        FeatureData before = rec[0];
        FeatureData after = before; after.suppressed = true;
        rec[0] = after;
        DocumentUndoStack st;
        st.push(DocumentCommand::modifyFeature(before, after, "Suppress Extrude1"));

        check(applyUndo(st.undo(), rec), "undo suppress applies");
        check(!rec[0].suppressed, "unsuppressed by undo");
        check(applyRedo(st.redo(), rec), "redo suppress applies");
        check(rec[0].suppressed, "suppressed again by redo");
    }

    // ---- ModifySketch -------------------------------------------------
    // A sketch edit changes no feature in the recipe; the geometry payload
    // is carried on the command and applied by the undo host. Both apply
    // functions must therefore succeed WITHOUT touching the feature list.
    {
        auto mk = [](int n) {
            DocumentCommand::SketchContents c;
            for (int i = 0; i < n; ++i) {
                sketch::Entity e;
                e.id = i + 1;
                e.type = sketch::EntityType::Line;
                c.entities.push_back(e);
            }
            return c;
        };
        std::vector<FeatureData> rec{F(1,"Sketch1",FeatureType::Sketch),
                                     F(2,"Extrude1")};
        const std::string before_names = names(rec);

        DocumentUndoStack st;
        st.push(DocumentCommand::modifySketch(1, mk(2), mk(5), "Edit Sketch1"));
        check(st.undoDescription() == "Edit Sketch1", "sketch edit keeps its description");

        DocumentCommand c = st.undo();
        check(c.type == DocumentCommandType::ModifySketch, "undo yields a ModifySketch");
        check(c.sketchFeatureId == 1, "it names the sketch to restore");
        check(c.sketchBefore.entities.size() == 2, "undo carries the pre-edit geometry");
        check(applyUndo(c, rec), "undo applies");
        check(names(rec) == before_names, "the feature list is untouched by a sketch edit");

        c = st.redo();
        check(c.sketchAfter.entities.size() == 5, "redo carries the post-edit geometry");
        check(applyRedo(c, rec), "redo applies");
        check(names(rec) == before_names, "still untouched after redo");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
