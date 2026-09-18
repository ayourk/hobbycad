// =====================================================================
//  tests/project/arrangement_file.cpp — the user's own arrangement
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//  HobbyCAD works with no arrangement file at all; one exists only once
//  something is customized, and holds those customizations alone. The
//  file comes from outside, so the reader is checked against what a
//  hostile one can do: too large, too deeply nested, truncated, wrong
//  types, duplicate keys, huge strings, binary junk, and a fuzz run over
//  mutations of a good file.
// =====================================================================
#include <hobbycad/layout/customization.h>

#include <hobbycad/commands.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace hobbycad;
using namespace hobbycad::layout;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// The keys a container shows, in order.
static std::vector<std::string> order(const Arrangement& a, const std::string& container)
{
    std::vector<std::string> keys;
    for (const Element* e : a.children(container, true)) keys.push_back(e->key);
    return keys;
}

static std::string entryFile(const std::string& body)
{
    return "{\"version\": 1, \"elements\": [" + body + "]}";
}

int main()
{
    std::printf("arrangement file\n");
    const Arrangement& base = defaultArrangement();

    // ---- no file, and a file that cannot be read -----------------------------------
    {
        const ReadResult none = readCustomizations("", base);
        check(!none.usable && none.customizations.empty(),
              "an empty file is not an arrangement, and the default stands");
        const Arrangement resolved = resolve(base, none.customizations);
        check(order(resolved, kMenuBar) == order(base, kMenuBar),
              "with no customizations the resolved arrangement is the default");
        check(writeCustomizations(none.customizations).empty(),
              "nothing customized writes no file at all");

        const std::string elf("\x7f\x45\x4c\x46\x02\x01", 6);
        const ReadResult junk = readCustomizations(elf, base);
        check(!junk.usable && !junk.problems.empty(), "binary junk is reported, not read");
        const ReadResult cut = readCustomizations("{\"version\": 1, \"elements\": [{\"key\"", base);
        check(!cut.usable, "a truncated file is reported");
        // Good JSON, only too much of it: the cap is what refuses it.
        std::string padded = "{\"version\": 1, \"elements\": []";
        padded.append(kMaxFileBytes, ' ');
        padded += "}";
        const ReadResult big = readCustomizations(padded, base);
        check(!big.usable && big.problems.size() == 1, "a file past the size cap is not parsed");
        check(readCustomizations(padded.substr(0, 30) + "}", base).usable,
              "and the same file within the cap is read");
        check(!readCustomizations("[]", base).usable
                  && !readCustomizations("\"an arrangement\"", base).usable,
              "an arrangement is an object, not a list or a string");
        check(!readCustomizations("{\"version\": 1} and then some", base).usable,
              "nothing may follow the end of it");
    }

    // ---- an entry customizes one element -------------------------------------------
    const std::string sketchMenu = "menu.sketch";
    const std::vector<std::string> sketchOrder = order(base, sketchMenu);
    check(sketchOrder.size() > 3, "the default Sketch menu has elements to move");
    const std::string first = sketchOrder[0];
    const std::string second = sketchOrder[1];
    {
        // Swapping two neighbors writes exactly one entry, and nothing else
        // is renumbered.
        Customizations c;
        Arrangement resolved = resolve(base, c);
        check(c.move(resolved, second, sketchMenu, 0), "the second element moves to the front");
        check(c.elements().size() == 1 && c.elements()[0].key == second
                  && c.elements()[0].order && !c.elements()[0].container,
              "one entry, holding its number only");
        resolved = resolve(base, c);
        std::vector<std::string> swapped = sketchOrder;
        std::swap(swapped[0], swapped[1]);
        check(order(resolved, sketchMenu) == swapped, "and the menu shows them swapped");
        check(resolved.element(first)->order == base.element(first)->order,
              "the element it passed keeps its own number");

        // Into the middle: the neighbors keep their own numbers.
        Customizations middle;
        Arrangement shown = resolve(base, middle);
        const std::string mover = sketchOrder.back();
        check(middle.move(shown, mover, sketchMenu, 1), "an element moves between two others");
        shown = resolve(base, middle);
        std::vector<std::string> expected = sketchOrder;
        expected.pop_back();
        expected.insert(expected.begin() + 1, mover);
        check(order(shown, sketchMenu) == expected, "and sits where it was put");
        check(shown.element(first)->order == base.element(first)->order
                  && shown.element(second)->order == base.element(second)->order,
              "while the two it came between keep their numbers");

        // Into another container: one entry carries both the number and
        // the move.
        Customizations moved;
        Arrangement there = resolve(base, moved);
        check(moved.move(there, first, "menu.edit", 0), "an element moves to another menu");
        check(moved.elements().size() == 1 && moved.elements()[0].container
                  && *moved.elements()[0].container == "menu.edit",
              "one entry holds the number and the container");
        there = resolve(base, moved);
        check(order(there, "menu.edit").front() == first
                  && order(there, sketchMenu).front() != first,
              "and it is shown in the menu it was moved to, not the one it left");

        // A command HobbyCAD adds later lands in its default place among
        // customized neighbors.
        Arrangement later = base;
        // Its own default number falls between the two the person swapped.
        const std::string added = later.addElement(ElementKind::Command, "view.fit", sketchMenu,
                                                   base.element(first)->order - 50.0);
        Arrangement resolvedLater = resolve(later, c);
        const std::vector<std::string> keys = order(resolvedLater, sketchMenu);
        check(keys.size() == sketchOrder.size() + 1 && keys[0] == second && keys[1] == added
                  && keys[2] == first,
              "a command added later slots in among customized neighbors");
    }

    // ---- hiding, renaming and restoring ---------------------------------------------
    {
        Customizations c;
        c.setHidden(first, true);
        c.setLabel(first, "My own name");
        Arrangement resolved = resolve(base, c);
        check(resolved.element(first)->hidden
                  && resolved.element(first)->userLabel == "My own name",
              "an element can be hidden and renamed");
        check(resolved.children(sketchMenu, false).size() == sketchOrder.size() - 1,
              "a hidden element is not shown");
        check(order(resolve(base, c), sketchMenu) == sketchOrder,
              "hiding does not change the order");

        c.clearHidden(first);
        check(c.elements().size() == 1 && !c.elements()[0].hidden,
              "showing it again drops the hidden flag, and keeps the name");
        c.setHidden(first, true);
        check(c.restoreElement(first) && c.empty(), "restoring the element drops its entry");
        check(resolve(base, c).element(first)->userLabel.empty(),
              "and its name is the translated default again");

        c.setLabel(first, "Name");
        c.setLabel(first, "");
        check(c.empty(), "an empty name restores the default text");

        check(!isUserLabel(std::string("bad\tname")) && !isUserLabel(std::string("\x01")),
              "a name with control characters is not a name");
        check(!isUserLabel(std::string(kMaxLabelLength + 1, 'x')) && isUserLabel("&File"),
              "a name past the cap is refused; a mnemonic ampersand is fine");
        check(!isUserLabel(std::string("\xC3\x28")), "a name that is not UTF-8 is refused");
        check(isUserLabel("Ma\xC3\x9fstab"), "a name in another language is fine");
    }

    // ---- a separator the person added -----------------------------------------------
    {
        Customizations c;
        Arrangement resolved = resolve(base, c);
        const std::string key = c.addSeparator(resolved, sketchMenu, 1);
        check(key.rfind(kAddedPrefix, 0) == 0 && c.added().size() == 1,
              "a separator can be added where the default has none");
        resolved = resolve(base, c);
        const std::vector<std::string> keys = order(resolved, sketchMenu);
        check(keys.size() == sketchOrder.size() + 1 && keys[1] == key
                  && resolved.element(key)->kind == ElementKind::Separator,
              "and it is shown where it was put");
        check(c.addSeparator(resolved, "menu.nowhere", 0).empty(),
              "not in a container that does not exist");

        const ReadResult back = readCustomizations(writeCustomizations(c), base);
        check(back.usable && back.customizations.added().size() == 1
                  && back.customizations.added()[0].key == key,
              "it reads back from the file");
        const ReadResult odd = readCustomizations(
            "{\"added\": [{\"key\": \"sketch.line\", \"container\": \"menu.sketch\","
            " \"order\": 1}]}", base);
        check(odd.usable && odd.customizations.added().empty() && odd.problems.size() == 1,
              "a file cannot add anything but a separator of its own");

        check(c.restoreElement(key) && c.empty(), "restoring it takes it away again");
    }

    // ---- restore per area, and all --------------------------------------------------
    {
        const std::string toolbarKey = order(base, kSketchToolbar).at(1);
        Customizations c;
        c.setHidden(first, true);
        c.setHidden(toolbarKey, true);
        bindings::Slots slots{};
        slots[0] = "Ctrl+Alt+L";
        c.setBinding("sketch.line", slots);
        check(c.elements().size() == 2 && c.bindings().size() == 1, "three entries in three areas");
        check(resolve(base, c).defaultBindings().size() == base.defaultBindings().size(),
              "a binding entry replaces the default rather than adding one");
        bool bound = false;
        for (const auto& b : resolve(base, c).defaultBindings()) {
            if (b.first == "sketch.line" && b.second[0] == "Ctrl+Alt+L") bound = true;
        }
        check(bound, "and the command carries the person's key");

        check(c.restoreArea(RestoreArea::Bindings, base) == 1 && c.bindings().empty()
                  && c.elements().size() == 2,
              "restoring the bindings leaves the menus and toolbars alone");
        check(c.restoreArea(RestoreArea::Menus, base) == 1 && c.elements().size() == 1
                  && c.elements()[0].key == toolbarKey,
              "restoring the menus leaves the toolbars alone");
        c.setHidden(first, true);
        check(c.restoreArea(RestoreArea::All, base) == 2 && c.empty(),
              "restore all leaves nothing, so no file remains");
    }

    // ---- the file reads back what was written ---------------------------------------
    {
        Customizations c;
        c.setOrder(second, 50.5);
        c.setContainer(second, "menu.edit");
        c.setHidden(first, true);
        c.setLabel(first, "Straight line");
        bindings::Slots slots{};
        slots[1] = "Shift+L";
        c.setBinding("sketch.line", slots);
        const std::string text = writeCustomizations(c);
        const ReadResult back = readCustomizations(text, base);
        check(back.usable && back.problems.empty() && back.customizations.elements().size() == 2
                  && back.customizations.bindings().size() == 1,
              "the file reads back as it was written");
        const Customization* moved = back.customizations.find(second);
        check(moved && moved->order && std::fabs(*moved->order - 50.5) < 1e-12
                  && moved->container && *moved->container == "menu.edit",
              "a fractional sort order and a move survive the round trip");
        const Customization* named = back.customizations.find(first);
        check(named && named->label && *named->label == "Straight line" && named->hidden
                  && *named->hidden,
              "so do the name and the hidden flag");
    }

    // ---- entries the reader refuses --------------------------------------------------
    {
        const ReadResult gone = readCustomizations(
            entryFile("{\"key\": \"menu.file/command.that.went\", \"hidden\": true}"), base);
        check(gone.usable && gone.customizations.empty() && gone.problems.size() == 1,
              "an entry for an element HobbyCAD no longer has is dropped with a notice");
        const ReadResult unknownKey = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"tint\": 3}"), base);
        check(unknownKey.usable && unknownKey.customizations.empty()
                  && unknownKey.problems.size() == 1,
              "an entry with an unknown key is skipped");
        const ReadResult wrongType = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"order\": \"first\"}"), base);
        check(wrongType.customizations.empty() && wrongType.problems.size() == 1,
              "a sort order that is not a number is skipped");
        const ReadResult badContainer = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"container\": \"menu.nowhere\"}"), base);
        check(badContainer.customizations.empty() && badContainer.problems.size() == 1,
              "a move into a container that does not exist is skipped");
        const ReadResult oddId = readCustomizations(
            entryFile("{\"key\": \"menu.file/file.new;rm\", \"hidden\": true}"), base);
        check(oddId.customizations.empty() && oddId.problems.size() == 1
                  && oddId.problems[0].find("not one an id may have") != std::string::npos,
              "a key outside the character set ids use is skipped as such");
        const ReadResult oddLabel = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"label\": \"one\\u0001two\"}"), base);
        check(oddLabel.usable && oddLabel.customizations.empty()
                  && oddLabel.problems.size() == 1,
              "a name with a control character in it is skipped");
        const ReadResult notFinite = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"order\": 1e400}"), base);
        check(!notFinite.usable, "a number that is not finite is refused");
        const ReadResult twice = readCustomizations(
            entryFile("{\"key\": \"" + first + "\", \"key\": \"" + second + "\"}"), base);
        check(!twice.usable, "the same key twice in one object is refused");
        const ReadResult hugeString = readCustomizations(
            entryFile("{\"key\": \"" + std::string(5000, 'a') + "\"}"), base);
        check(!hugeString.usable, "a string past the length cap is refused");
        const ReadResult unknownTop =
            readCustomizations("{\"version\": 1, \"layout\": {\"a\": 1}}", base);
        check(unknownTop.usable && unknownTop.problems.size() == 1,
              "an unknown key at the top is skipped, and the rest is read");
        const ReadResult later = readCustomizations("{\"version\": 99, \"elements\": []}", base);
        check(later.usable && later.problems.size() == 1,
              "a file from a later version is read for what is understood, with a notice");
        const ReadResult badBinding = readCustomizations(
            "{\"bindings\": [{\"command\": \"sketch.line\","
            " \"slots\": [\"A\",\"B\",\"C\",\"D\"]}]}",
            base);
        check(badBinding.usable && badBinding.customizations.bindings().empty()
                  && badBinding.problems.size() == 1,
              "more bindings than a command has slots is skipped");

        std::string deep = "{\"version\": 1, \"elements\": ";
        std::string tail;
        for (int i = 0; i < kMaxDepth + 2; ++i) {
            deep += "[";
            tail += "]";
        }
        const ReadResult tooDeep = readCustomizations(deep + tail + "}", base);
        check(!tooDeep.usable, "nesting deeper than the cap is refused");
    }

    // ---- export and import ----------------------------------------------------------
    {
        const std::string exported = exportArrangement(base);
        check(exported.size() > 1000 && exported.find("\"containers\"") != std::string::npos
                  && exported.find("\"bindings\"") != std::string::npos,
              "an export is the whole arrangement, not a delta");
        const ReadResult sameAsDefault = importArrangement(exported, base);
        check(sameAsDefault.usable && sameAsDefault.customizations.empty(),
              "importing the default writes no entries");

        Customizations c;
        Arrangement resolved = resolve(base, c);
        c.move(resolved, second, sketchMenu, 0);
        c.setLabel(first, "Straight line");
        bindings::Slots slots{};
        slots[0] = "Ctrl+Alt+L";
        c.setBinding("sketch.line", slots);
        const Arrangement mine = resolve(base, c);
        const ReadResult imported = importArrangement(exportArrangement(mine), base);
        check(imported.usable && imported.problems.empty(), "a complete arrangement imports");
        const Arrangement again = resolve(base, imported.customizations);
        check(order(again, sketchMenu) == order(mine, sketchMenu),
              "import then export gives the same arrangement back");
        check(again.element(first)->userLabel == "Straight line", "the names come with it");
        bool sameBinding = false;
        for (const auto& b : again.defaultBindings()) {
            if (b.first == "sketch.line" && b.second[0] == "Ctrl+Alt+L") sameBinding = true;
        }
        check(sameBinding, "and so do the bindings");
        Customizations withSeparator;
        Arrangement forSeparator = resolve(base, withSeparator);
        const std::string separator =
            withSeparator.addSeparator(forSeparator, sketchMenu, 1);
        const Arrangement withIt = resolve(base, withSeparator);
        const ReadResult separatorBack = importArrangement(exportArrangement(withIt), base);
        check(separatorBack.usable && separatorBack.customizations.added().size() == 1
                  && separatorBack.customizations.added()[0].key == separator,
              "an import brings the separators a person added across");

        check(imported.customizations.find(second) != nullptr
                  && imported.customizations.elements().size() <= c.elements().size() + 1,
              "an import stores an entry for what differs, not for everything");
    }

    // ---- a fuzz run over mutations of a good file ------------------------------------
    {
        Customizations c;
        c.setOrder(second, 50.5);
        c.setLabel(first, "Name");
        bindings::Slots slots{};
        slots[0] = "Ctrl+Alt+L";
        c.setBinding("sketch.line", slots);
        const std::string good = writeCustomizations(c);

        // A small deterministic generator: every mutation must be read or
        // reported, never crash and never leave an unusable result usable.
        unsigned int seed = 20260917u;
        const auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        int usable = 0;
        for (int run = 0; run < 4000; ++run) {
            std::string text = good;
            const int edits = static_cast<int>(next() % 4) + 1;
            for (int e = 0; e < edits && !text.empty(); ++e) {
                const std::size_t at = next() % text.size();
                switch (next() % 4) {
                case 0: text[at] = static_cast<char>(next() % 256); break;
                case 1: text.erase(at, 1 + next() % 7); break;
                case 2: text.insert(at, std::string(1 + next() % 5,
                                                    static_cast<char>(next() % 256))); break;
                default: text.resize(at); break;
                }
            }
            const ReadResult r = readCustomizations(text, base);
            if (r.usable) ++usable;
            // Whatever came back must be a set the rest of the library can use.
            const Arrangement a = resolve(base, r.customizations);
            if (!a.problems().empty()) {
                check(false, "a fuzzed file resolved to a broken arrangement");
                break;
            }
            const std::string again = writeCustomizations(r.customizations);
            if (!again.empty() && !readCustomizations(again, base).usable) {
                check(false, "what a fuzzed file produced could not be written and read back");
                break;
            }
        }
        check(usable > 0, "some mutations are still readable, and the rest are reported");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
