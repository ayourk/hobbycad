// =====================================================================
//  tests/project/save_path.cpp — where a project save lands: a typed
//  directory, a new "name.hcad", or an existing manifest.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
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
    std::printf("save path\n");

    const fs::path root = fs::path(argc > 1 ? argv[1] : "save_path.data");
    fs::remove_all(root);
    fs::create_directories(root / "Documents");
    const std::string docs = (root / "Documents").string();

    check(projectDirForSavePath(docs + "/widget") == docs + "/widget", "a directory is used as given");
    check(projectDirForSavePath(docs + "/widget/") == docs + "/widget", "a trailing separator names the same directory");
    check(projectDirForSavePath(docs + "/widget.hcad") == docs + "/widget", "a new name.hcad gets its own directory");
    check(projectDirForSavePath(docs + "/widget/widget.hcad") == docs + "/widget",
          "a new manifest already inside its directory is not nested");
    check(projectDirForSavePath("widget.HCAD") == "widget", "relative, any case");

    {
        // What the unsaved-changes prompt did: a typed name with ".hcad" added.
        Project p;
        p.setName("widget");
        std::string err;
        check(p.save(docs + "/widget.hcad", &err), "save to a new name.hcad");
        check(fs::is_regular_file(docs + "/widget/widget.hcad"), "the manifest is inside widget/");
        check(!fs::exists(docs + "/sketches") && !fs::exists(docs + "/Documents.hcad"),
              "nothing is written loose into the chosen folder");
    }
    {
        // Save As onto an existing project's manifest.
        Project p;
        std::string err;
        check(p.save(docs + "/widget/widget.hcad", &err), "save over the existing manifest");
        check(!fs::exists(docs + "/widget/widget"), "re-saving does not nest the project inside itself");
        Project q;
        check(q.load(docs + "/widget", &err) && q.name() == "widget", "the project loads from its directory");
    }

    fs::remove_all(root);
    std::printf("%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
