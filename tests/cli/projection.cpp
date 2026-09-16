// =====================================================================
//  tests/cli/projection.cpp — the `project` command creates a cross-sketch
//  projection that persists, links to its source, and refuses bad refs.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <cstdio>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

namespace {
struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* l) { return engine.execute(l); }
    const Project& proj() const { return *host.hostProject(); }
};
}  // namespace

int main() {
    Fixture f;
    // Sketch A on XY with one line (entity id 1).
    ck(f.run("create sketch XY A").exitCode == 0, "create sketch A");
    ck(f.run("line from 0,0 to 10,0").exitCode == 0, "line in A (entity 1)");
    ck(f.run("finish").exitCode == 0, "finish A");

    // Sketch B on XZ; project A's line into it.
    ck(f.run("create sketch XZ B").exitCode == 0, "create sketch B");
    ck(f.run("projection A 1").exitCode == 0, "projection A 1 succeeds");

    // Bad references are refused, not guessed.
    ck(f.run("projection Nope 1").exitCode != 0, "unknown source sketch refused");
    ck(f.run("projection A 999").exitCode != 0, "unknown source entity refused");

    ck(f.run("finish").exitCode == 0, "finish B");

    // Verify the projection persisted and links back to A's line.
    const SketchData* A = nullptr; const SketchData* B = nullptr;
    for (const auto& sk : f.proj().sketches()) {
        if (sk.name == "A") A = &sk;
        if (sk.name == "B") B = &sk;
    }
    ck(A && B, "both sketches persisted");
    if (A && B) {
        const sketch::Entity* projected = nullptr;
        for (const auto& e : B->entities)
            if (e.projectionSourceId >= 0) { projected = &e; break; }
        ck(projected != nullptr, "B carries a projected entity");
        if (projected) {
            ck(projected->projectionSourceId == 1, "linked to A's entity id 1");
            ck(projected->projectionSourceSketchId == A->id, "linked to A's sketch id");
            ck(projected->type == sketch::EntityType::Line, "projected entity is a line");
        }
    }

    if (fails == 0) std::printf("cli projection: ALL PASS\n");
    else            std::printf("cli projection: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
