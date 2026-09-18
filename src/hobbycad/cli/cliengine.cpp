// =====================================================================
//  src/hobbycad/cli/cliengine.cpp — Shared command dispatch engine
// =====================================================================

#include "cliengine.h"

#include <hobbycad/strutil.h>
#include <hobbycad/translate.h>
#include <hobbycad/naming.h>
#include <hobbycad/brep/mesh_to_step.h>
#include <gp_Dir.hxx>
#include <hobbycad/units.h>
#include <algorithm>
#include <map>
#include <vector>
#include <hobbycad/parameters.h>
#include <hobbycad/project_undo.h>
#include <hobbycad/project_session.h>
#include "clihistory.h"

#include <hobbycad/core.h>
#include <hobbycad/brep_io.h>
#include <hobbycad/document.h>
#include <hobbycad/sketch/parsing.h>
#include <hobbycad/sketch/decomposition.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/transform.h>
#include <hobbycad/sketch/slotpath.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/file_format.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/properties.h>
#include <hobbycad/sketch/property_schema.h>
#include <hobbycad/sketch/undo.h>
#include <hobbycad/sketch/edit_session.h>
#include <hobbycad/commands.h>

#include <cmath>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace hobbycad {

namespace {

/// What QDir::homePath() gave: $HOME, or the Windows profile directory.
/// Empty when the environment says nothing, which leaves "cd" with a
/// path it will report as missing rather than silently going elsewhere.
std::string homeDirectory()
{
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home);
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
        return std::string(profile);
#endif
    return {};
}

/// A constraint's name as it is shown, translated. constraintTypeName()
/// is the keyword a person types and must stay English; the display name
/// is a separate string ("Curvature (G2)" is shown where "Curvature" is
/// typed), so a message never carries an untranslated word.
std::string constraintName(sketch::ConstraintType type)
{
    return hobbycad::translate(sketch::constraintDisplayContext(),
                               sketch::constraintDisplayName(type));
}

/// An entity type's name as it is shown, translated.
std::string entityName(sketch::EntityType type)
{
    return sketch::entityTypeDisplayName(type);
}

}  // namespace

namespace {

// An error result. Every command used to spell this out as three lines
// (exitCode, error, return); the message is the only part that varies.
CliResult failure(const std::string& message)
{
    CliResult r;
    r.exitCode = 1;
    r.error = message;
    return r;
}

/// Start an entity of a given type. Keeps the geometry commands from each
/// repeating the same two lines of construction.
SketchEntityData makeEntity(sketch::EntityType type)
{
    SketchEntityData e;
    e.type = type;
    return e;
}

/// Where a command is valid.
enum class CmdScope {
    Always,   ///< Available at every prompt
    Sketch,   ///< Only while a sketch is open
};

struct CommandSpec {
    const char* name = nullptr;
    CmdScope scope{};
    /// The same command in the front-end registry (hobbycad/commands.h),
    /// whose description "help <command>" shows; null when there is none.
    const char* registryId = nullptr;
};

/// The single list of command names and where each one works.
///
/// One table, three consumers: tab completion, the "did you mean" path, and
/// the message shown for a command that was not recognized. They used to be
/// separate lists, which is how `plane` came to be handled by `select`'s
/// lookup while absent from `select`'s list of valid types: the code was
/// there and unreachable.
const CommandSpec kCommands[] = {
    {"help",       CmdScope::Always},
    {"version",    CmdScope::Always},
    {"open", CmdScope::Always, "file.open"},
    {"save", CmdScope::Always, "file.save"},
    {"convert",    CmdScope::Always},
    {"script",     CmdScope::Always},
    {"info",       CmdScope::Always},
    {"deselect",   CmdScope::Always},
    {"print",      CmdScope::Always},
    {"export",     CmdScope::Always},
    {"sketches",   CmdScope::Always},
    {"bodies",     CmdScope::Always},
    {"planes",     CmdScope::Always},
    {"constraints", CmdScope::Always},
    {"groups",     CmdScope::Always},
    {"parameters", CmdScope::Always, "design.parameters"},
    {"new", CmdScope::Always, "file.new"},
    {"cd",         CmdScope::Always},
    {"pwd",        CmdScope::Always},
    {"history",    CmdScope::Always},
    {"undo", CmdScope::Always, "edit.undo"},
    {"redo", CmdScope::Always, "edit.redo"},
    {"select",     CmdScope::Always},
    {"create",     CmdScope::Always},
    {"delete",     CmdScope::Always},
    {"rename",     CmdScope::Always},
    {"extrude", CmdScope::Always, "design.extrude"},
    {"revolve", CmdScope::Always, "design.revolve"},
    {"zoom",       CmdScope::Always},
    {"panto",      CmdScope::Always},
    {"rotate",     CmdScope::Always},
    {"exit",       CmdScope::Always},
    {"quit",       CmdScope::Always},

    {"point", CmdScope::Sketch, "sketch.point"},
    {"line", CmdScope::Sketch, "sketch.line"},
    {"circle", CmdScope::Sketch, "sketch.circle"},
    {"rectangle", CmdScope::Sketch, "sketch.rectangle"},
    {"arc", CmdScope::Sketch, "sketch.arc"},
    {"polygon", CmdScope::Sketch, "sketch.polygon"},
    {"ellipse", CmdScope::Sketch, "sketch.ellipse"},
    {"slot", CmdScope::Sketch, "sketch.slot"},
    {"spline", CmdScope::Sketch, "sketch.spline.catmullRom"},
    {"bezier", CmdScope::Sketch, "sketch.spline.cubicBezier"},
    {"conic", CmdScope::Sketch, "sketch.spline.conic"},
    {"text", CmdScope::Sketch, "sketch.text"},
    {"constrain",  CmdScope::Sketch},
    {"solve",      CmdScope::Sketch},
    {"group",      CmdScope::Sketch},
    {"transform",  CmdScope::Sketch},
    {"sweep",      CmdScope::Sketch},
    {"projection", CmdScope::Sketch, "sketch.project"},
    {"points",     CmdScope::Sketch},
    {"set",        CmdScope::Sketch},
    {"finish", CmdScope::Sketch, "sketch.finish"},
    {"discard",    CmdScope::Sketch},
};

/// Commands that change the sketch being edited. Inside a sketch each one
/// is recorded in the sketch's history, so "undo" can take it back.
bool editsSketch(const std::string& cmd)
{
    static const char* const kEdits[] = {
        "point", "line", "circle", "rectangle", "arc", "polygon", "ellipse", "slot",
        "spline", "bezier", "conic", "text", "constrain", "solve", "group",
        "transform", "sweep", "projection", "points", "set", "delete",
    };
    for (const char* e : kEdits) {
        if (cmd == e) return true;
    }
    return false;
}

/// Forgets a selected entity that an undo or redo removed.
class SelectionKeeper : public sketch::EditListener {
public:
    explicit SelectionKeeper(int& selectedEntityId) : m_selected(selectedEntityId) {}
    void entityRemoved(int entityId) override
    {
        if (m_selected == entityId) m_selected = -1;
    }

private:
    int& m_selected;
};

/// Find a command by name, or null.
const CommandSpec* findCommand(const std::string& name)
{
    for (const auto& c : kCommands) {
        if (name == std::string(c.name)) return &c;
    }
    return nullptr;
}

/// Levenshtein distance, capped; we only care about "close".
int editDistance(const std::string& a, const std::string& b)
{
    const int n = a.size(), m = b.size();
    if (n == 0) return m;
    if (m == 0) return n;

    std::vector<int> prev(static_cast<size_t>(m) + 1);
    std::vector<int> cur(static_cast<size_t>(m) + 1);
    for (int j = 0; j <= m; ++j) prev[static_cast<size_t>(j)] = j;

    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[static_cast<size_t>(j)] = std::min({
                cur[static_cast<size_t>(j) - 1] + 1,
                prev[static_cast<size_t>(j)] + 1,
                prev[static_cast<size_t>(j) - 1] + cost });
        }
        prev.swap(cur);
    }
    return prev[static_cast<size_t>(m)];
}

/// Commands close enough to a typo to be worth suggesting.
std::vector<std::string> nearestCommands(const std::string& typed)
{
    // A typed word that is the START of a real command (e.g. "project" ->
    // "projection") is the strongest hint, so suggest those first.
    std::vector<std::string> best;
    for (const auto& c : kCommands) {
        const std::string name = c.name;
        if (static_cast<int>(name.size()) > static_cast<int>(typed.size()) && startsWithIgnoreCase(name, typed))
            best.push_back(name);
    }
    if (!best.empty()) return best;

    // Otherwise fall back to edit distance: one edit for a short word, two for a
    // longer one ("arc" should not suggest "cd", but "rectangel" -> "rectangle").
    const int limit = static_cast<int>(typed.size()) <= 4 ? 1 : 2;
    int bestDistance = limit + 1;
    for (const auto& c : kCommands) {
        const std::string name = c.name;
        const int d = editDistance(typed, name);
        if (d > limit) continue;
        if (d < bestDistance) {
            bestDistance = d;
            best.clear();
        }
        if (d == bestDistance) best.push_back(name);
    }
    return best;
}

}  // namespace


CliEngine::CliEngine(CliHistory& history)
    : m_history(history)
{
}

CliEngine::~CliEngine() = default;

std::vector<std::string> CliEngine::commandNames() const
{
    std::vector<std::string> commands;
    for (const auto& c : kCommands) {
        if (c.scope == CmdScope::Sketch && !m_inSketchMode) continue;
        commands.push_back(c.name);
    }
    return commands;
}

std::string CliEngine::buildPrompt() const
{
    // Editing a sketch. The marker goes BEFORE the name, and '*' is an
    // illegal first character for a name (hobbycad::isValidObjectName), so
    // "sketch *Profile>" can only mean "Profile, being edited"; there is
    // no sketch it could be confused with. A trailing marker would have to
    // be read after a name of unknown length, and a name ending in '*'
    // would have been ambiguous.
    if (m_inSketchMode) {
        std::string p = "sketch "
                  + hobbycad::kEditMarker
                  + m_currentSketchName;
        // A selected entity is part of where you are standing, so it belongs
        // in the prompt for the same reason the sketch name does.
        if (m_selectedEntityId > 0) {
            p += subst("/entity %1", m_selectedEntityId);
        }
        return p + "> ";
    }

    // Selected something: the prompt points at it, like a directory.
    if (m_context.isSet()) {
        return m_context.kindName() + ' ' + m_context.name
             + "> ";
    }

    std::error_code cwdEc;
    std::string cwd = std::filesystem::current_path(cwdEc).string();

#if !defined(_WIN32)
    std::string home = homeDirectory();
    if (!home.empty() && startsWith(cwd, home)) {
        cwd = "~" + cwd.substr(home.length());
    }
#endif

    return "hobbycad:" + cwd + "> ";
}

bool CliEngine::inSketchMode() const
{
    return m_inSketchMode;
}

std::string CliEngine::currentSketchName() const
{
    return m_currentSketchName;
}

// Helper to check if prefix looks like start of a parameter name
static bool looksLikeParamStart(const std::string& prefix)
{
    if (prefix.empty()) return false;
    // Starts with letter or open paren
    return std::isalpha(static_cast<unsigned char>(prefix[0])) || prefix[0] == '(';
}

// completeArguments, "create ...": the object type, then a plane or sketch name.
std::vector<std::string> CliEngine::completeCreateArguments(const std::vector<std::string>& tokens, const std::string& prefix,
                                               int argIndex) const
{
    std::string type = static_cast<int>(tokens.size()) > 1 ? toLower(tokens[1]) : std::string();

    if (argIndex == 1) {
        // First argument: object type to create
        std::vector<std::string> types = { "sketch" };

        if (prefix.empty()) {
            return { "?<type>  Object type to create (sketch)" };
        }

        std::vector<std::string> matches;
        for (const auto& t : types) {
            if (startsWithIgnoreCase(t, prefix)) {
                matches.push_back(t);
            }
        }
        return matches.empty()
            ? std::vector<std::string>{ "?<type>  Object type to create (sketch)" }
            : matches;
    }
    else if (argIndex == 2 && type == "sketch") {
        // After "create sketch": plane name, "on", or sketch name
        std::vector<std::string> completions = {
            "XY",
            "XZ",
            "YZ",
            "on"
        };
        if (prefix.empty()) {
            return { "?[XY|XZ|YZ|on <plane>|name]  Plane or sketch name" };
        }
        std::vector<std::string> matches;
        for (const auto& c : completions) {
            if (startsWithIgnoreCase(c, prefix)) {
                matches.push_back(c);
            }
        }
        return matches;  // Empty if typing a sketch name (fine)
    }
    else if (argIndex == 3 && type == "sketch") {
        std::string arg2 = static_cast<int>(tokens.size()) > 2 ? tokens[2] : std::string();
        std::string arg2Lower = toLower(arg2);
        std::string arg2Upper = toUpper(arg2);

        if (arg2Lower == "on") {
            // After "create sketch on": "plane" keyword or plane name
            std::vector<std::string> completions = {
                "plane",
                "XY",
                "XZ",
                "YZ"
            };
            if (prefix.empty()) {
                return { "?[plane] <name>  XY, XZ, YZ, or construction plane name" };
            }
            std::vector<std::string> matches;
            for (const auto& c : completions) {
                if (startsWithIgnoreCase(c, prefix)) {
                    matches.push_back(c);
                }
            }
            // TODO: Also complete named construction planes from document
            return matches.empty()
                ? std::vector<std::string>{ "?[plane] <name>  XY, XZ, YZ, or construction plane name" }
                : matches;
        }
        if (arg2Upper == "XY" || arg2Upper == "XZ"
            || arg2Upper == "YZ") {
            // After "create sketch XZ": optional sketch name
            return { "?[name]  Optional sketch name (default: auto-named)" };
        }
        return {};  // arg2 was a sketch name; nothing more to complete
    }
    else if (argIndex == 4 && type == "sketch") {
        std::string arg2Lower = static_cast<int>(tokens.size()) > 2 ? toLower(tokens[2]) : std::string();
        if (arg2Lower == "on") {
            std::string arg3 = static_cast<int>(tokens.size()) > 3 ? tokens[3] : std::string();
            std::string arg3Lower = toLower(arg3);
            std::string arg3Upper = toUpper(arg3);

            if (arg3Lower == "plane") {
                // After "create sketch on plane": complete plane name
                std::vector<std::string> planes = {
                    "XY",
                    "XZ",
                    "YZ"
                };
                if (prefix.empty()) {
                    return { "?<name>  XY, XZ, YZ, or construction plane name" };
                }
                std::vector<std::string> matches;
                for (const auto& p : planes) {
                    if (startsWithIgnoreCase(p, prefix)) {
                        matches.push_back(p);
                    }
                }
                return matches.empty()
                    ? std::vector<std::string>{ "?<name>  XY, XZ, YZ, or construction plane name" }
                    : matches;
            }
            // After "create sketch on <plane-name>": optional sketch name
            return { "?[name]  Optional sketch name (default: auto-named)" };
        }
    }
    else if (argIndex == 5 && type == "sketch") {
        // After "create sketch on plane <name>": optional sketch name
        std::string arg2Lower = static_cast<int>(tokens.size()) > 2 ? toLower(tokens[2]) : std::string();
        std::string arg3Lower = static_cast<int>(tokens.size()) > 3 ? toLower(tokens[3]) : std::string();
        if (arg2Lower == "on" && arg3Lower == "plane") {
            return { "?[name]  Optional sketch name (default: auto-named)" };
        }
    }
    return {};
}

// completeArguments inside a sketch: point, line, circle, rectangle, arc.
std::vector<std::string> CliEngine::completeSketchArguments(const std::vector<std::string>& tokens, const std::string& prefix,
                                               int argIndex) const
{
    const std::string cmd = toLower(tokens.front());

    // Helper lambda to complete parameters when in a numeric field
    auto completeNumericField = [&](const std::string& hint) -> std::vector<std::string> {
        if (prefix.empty()) {
            return { subst("?%1  (or parameter name, or (expression))", hint)};
        }
        if (startsWith(prefix, '(')) {
            // Inside expression - no completion
            return { "?...)  Complete the expression" };
        }
        if (std::isalpha(static_cast<unsigned char>(prefix[0]))) {
            // Complete parameter names
            std::vector<std::string> matches;
            for (const auto& p : parameterNames()) {
                if (startsWithIgnoreCase(p, prefix)) {
                    matches.push_back(p);
                }
            }
            if (matches.empty()) {
                return { subst("?%1  (no matching parameters)", hint)};
            }
            return matches;
        }
        // Typing a number - no completion needed
        return {};
    };

    // point [at] <x>,<y>
    if (cmd == "point") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?[at] <x>,<y>  Point coordinates" };
            }
            if (startsWith("at", prefix)) {
                return { "at" };
            }
            // Could be coordinates directly
            return { "?<x>,<y>  Point coordinates" };
        }
        else if (argIndex == 2) {
            // Only reached if "at" was used
            return { "?<x>,<y>  Point coordinates" };
        }
    }

    // line [from] <x>,<y> to <x>,<y>
    if (cmd == "line") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?[from] <x>,<y>  Start point" };
            }
            if (startsWith("from", prefix)) {
                return { "from" };
            }
            return { "?<x>,<y>  Start point" };
        }
        else if (argIndex == 2) {
            // Could be coords (if no "from") or "to" keyword
            std::string prev = static_cast<int>(tokens.size()) > 1 ? toLower(tokens[1]) : std::string();
            if (prev == "from") {
                return { "?<x>,<y>  Start point" };
            }
            if (prefix.empty()) {
                return { "?to  End point follows" };
            }
            if (startsWith("to", prefix)) {
                return { "to" };
            }
        }
        else if (argIndex == 3) {
            if (prefix.empty()) {
                return { "?to  End point follows" };
            }
            if (startsWith("to", prefix)) {
                return { "to" };
            }
        }
        else if (argIndex >= 3) {
            return { "?<x>,<y>  End point" };
        }
    }

    // circle [at] <x>,<y> radius|diameter <value>
    if (cmd == "circle") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?[at] <x>,<y>  Center point" };
            }
            if (startsWith("at", prefix)) {
                return { "at" };
            }
            return { "?<x>,<y>  Center point" };
        }
        else if (argIndex == 2) {
            std::string prev = static_cast<int>(tokens.size()) > 1 ? toLower(tokens[1]) : std::string();
            if (prev == "at") {
                return { "?<x>,<y>  Center point" };
            }
            // Otherwise it's the size type
            if (prefix.empty()) {
                return { "?radius|diameter <value>" };
            }
            std::vector<std::string> opts = { "radius", "diameter" };
            std::vector<std::string> matches;
            for (const auto& o : opts) {
                if (startsWithIgnoreCase(o, prefix)) {
                    matches.push_back(o);
                }
            }
            return matches.empty()
                ? std::vector<std::string>{ "?radius|diameter  Size type" }
                : matches;
        }
        else if (argIndex == 3) {
            std::string token1 = static_cast<int>(tokens.size()) > 1 ? toLower(tokens[1]) : std::string();
            if (token1 == "at") {
                // Size type comes next
                if (prefix.empty()) {
                    return { "?radius|diameter <value>" };
                }
                std::vector<std::string> opts = { "radius", "diameter" };
                std::vector<std::string> matches;
                for (const auto& o : opts) {
                    if (startsWithIgnoreCase(o, prefix)) {
                        matches.push_back(o);
                    }
                }
                return matches.empty()
                    ? std::vector<std::string>{ "?radius|diameter  Size type" }
                    : matches;
            }
            // Otherwise it's the value
            std::string sizeType = static_cast<int>(tokens.size()) > 2 ? toLower(tokens[2]) : std::string();
            std::string hint = (sizeType == "diameter")
                ? "<d>  Diameter" : "<r>  Radius";
            return completeNumericField(hint);
        }
        else if (argIndex == 4) {
            std::string sizeType = static_cast<int>(tokens.size()) > 3 ? toLower(tokens[3]) : std::string();
            std::string hint = (sizeType == "diameter")
                ? "<d>  Diameter" : "<r>  Radius";
            return completeNumericField(hint);
        }
    }

    // rectangle [from] <x>,<y> to <x>,<y>
    if (cmd == "rectangle") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?[from] <x>,<y>  First corner" };
            }
            if (startsWith("from", prefix)) {
                return { "from" };
            }
            return { "?<x>,<y>  First corner" };
        }
        else if (argIndex == 2) {
            std::string prev = static_cast<int>(tokens.size()) > 1 ? toLower(tokens[1]) : std::string();
            if (prev == "from") {
                return { "?<x>,<y>  First corner" };
            }
            if (prefix.empty()) {
                return { "?to  Opposite corner follows" };
            }
            if (startsWith("to", prefix)) {
                return { "to" };
            }
        }
        else if (argIndex == 3) {
            if (prefix.empty()) {
                return { "?to  Opposite corner follows" };
            }
            if (startsWith("to", prefix)) {
                return { "to" };
            }
        }
        else if (argIndex >= 3) {
            return { "?<x>,<y>  Opposite corner" };
        }
    }

    // arc [at] <x>,<y> radius <r> [angle] <start> to <end>
    if (cmd == "arc") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?[at] <x>,<y>  Center point" };
            }
            if (startsWith("at", prefix)) {
                return { "at" };
            }
            return { "?<x>,<y>  Center point" };
        }
        // For arc, the structure varies based on optional keywords
        // Just provide contextual hints based on what's been typed
        else {
            // Check if we need radius keyword
            bool hasAt = static_cast<int>(tokens.size()) > 1 && toLower(tokens[1]) == "at";
            int radiusIdx = hasAt ? 3 : 2;

            if (argIndex == radiusIdx) {
                if (prefix.empty()) {
                    return { "?radius  Specify radius" };
                }
                if (startsWith("radius", prefix)) {
                    return { "radius" };
                }
            }
            else if (argIndex == radiusIdx + 1) {
                return completeNumericField("<r>  Radius");
            }
            else if (argIndex == radiusIdx + 2) {
                if (prefix.empty()) {
                    return { "?[angle] <start>  Start angle (degrees)" };
                }
                if (startsWith("angle", prefix)) {
                    return { "angle" };
                }
                // Could be typing a number or parameter for start angle
                return completeNumericField("<start>  Start angle");
            }
            else {
                // Could be start angle, "to", or end angle
                if (prefix.empty()) {
                    return { "?to <end>  End angle follows" };
                }
                if (startsWith("to", prefix)) {
                    return { "to" };
                }
                // Could be typing end angle
                return completeNumericField("<end>  End angle");
            }
        }
    }
    return {};
}

std::vector<std::string> CliEngine::completeArguments(const std::vector<std::string>& tokens,
                                          const std::string& prefix) const
{
    if (tokens.empty()) return {};

    std::string cmd = toLower(tokens.front());
    int argIndex = tokens.size() - 1;  // Which argument we're completing (0 = command)

    // If there's a prefix being typed, we're still on the current argument
    // If prefix is empty, we're starting a new argument
    if (prefix.empty() && static_cast<int>(tokens.size()) > 1) {
        argIndex = tokens.size();  // Starting next argument
    }

    // ---- select command ----
    if (cmd == "select") {
        if (argIndex == 1) {
            // First argument: object type
            std::vector<std::string> types = {
                "sketch",
                "body",
                "face",
                "edge",
                "vertex"
            };

            if (prefix.empty()) {
                // Show hint
                return { "?<type>  Object type (sketch, body, face, edge, vertex)" };
            }

            // Filter by prefix
            std::vector<std::string> matches;
            for (const auto& t : types) {
                if (startsWithIgnoreCase(t, prefix)) {
                    matches.push_back(t);
                }
            }
            return matches.empty()
                ? std::vector<std::string>{ "?<type>  Object type (sketch, body, face, edge, vertex)" }
                : matches;
        }
        else if (argIndex == 2) {
            // Second argument: object name
            // TODO: Return actual object names from document
            return { subst("?<name>  Name of the %1 to select", static_cast<int>(tokens.size()) > 1 ? tokens[1] : "object")};
        }
    }

    // ---- create command ----
    if (cmd == "create") return completeCreateArguments(tokens, prefix, argIndex);

    // ---- open command ----
    if (cmd == "open") {
        if (argIndex == 1 && prefix.empty()) {
            return { "?<path>  Project directory, .hcad manifest or .brep file" };
        }
        // Otherwise, let filename completion handle it
        return {};
    }

    // ---- save command ----
    if (cmd == "save") {
        if (argIndex == 1 && prefix.empty()) {
            return { "?<path>  Project directory, or a .brep file for the bodies" };
        }
        return {};
    }

    // ---- convert command ----
    if (cmd == "convert") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?<input>  Input file (.brep, .hcad, or directory)" };
            }
            if (prefix == "--") {
                return { "--format", "--help" };
            }
        }
        else if (argIndex == 2) {
            // Check if previous arg was --format
            if (static_cast<int>(tokens.size()) > 1 && tokens[tokens.size() - 1] == "--format") {
                return { "brep", "hcad" };
            }
            if (prefix.empty()) {
                return { "?<output>  Output file or directory" };
            }
        }
        return {};
    }

    // ---- script command ----
    if (cmd == "script") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?<file>  Script file to execute (.txt)" };
            }
            if (prefix == "-") {
                return { "--help" };
            }
        }
        return {};
    }

    // ---- cd command ----
    if (cmd == "cd") {
        if (argIndex == 1 && prefix.empty()) {
            return { "?[dir]  Directory to change to (default: home)" };
        }
        return {};
    }

    // ---- history command ----
    if (cmd == "history") {
        if (argIndex == 1) {
            std::vector<std::string> subcmds = {
                "clear",
                "max"
            };

            if (prefix.empty()) {
                return { "?[clear|max]  Subcommand (or no args to show history)" };
            }

            std::vector<std::string> matches;
            for (const auto& s : subcmds) {
                if (startsWithIgnoreCase(s, prefix)) {
                    matches.push_back(s);
                }
            }
            return matches;
        }
        else if (argIndex == 2 && static_cast<int>(tokens.size()) > 1 &&
                 toLower(tokens[1]) == "max") {
            return { "?<n>  Maximum number of history lines" };
        }
    }

    // ---- Viewport commands (zoom, panto, rotate) ----
    if (cmd == "zoom") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?<percent>|home  Zoom percentage or 'home'" };
            }
            if (startsWith("home", prefix)) {
                return { "home" };
            }
        }
    }

    if (cmd == "panto") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?<x>,<y>,<z>|home  Coordinates or 'home'" };
            }
            if (startsWith("home", prefix)) {
                return { "home" };
            }
        }
    }

    if (cmd == "rotate") {
        if (argIndex == 1) {
            if (prefix.empty()) {
                return { "?on <axis> <degrees>|home" };
            }
            std::vector<std::string> matches;
            if (startsWith("on", prefix)) {
                matches.push_back("on");
            }
            if (startsWith("home", prefix)) {
                matches.push_back("home");
            }
            return matches.empty()
                ? std::vector<std::string>{ "?on|home" }
                : matches;
        }
        else if (argIndex == 2 && static_cast<int>(tokens.size()) > 1 &&
                 toLower(tokens[1]) == "on") {
            if (prefix.empty()) {
                return { "?<axis>  x, y, or z" };
            }
            std::vector<std::string> axes = { "x", "y", "z" };
            std::vector<std::string> matches;
            for (const auto& a : axes) {
                if (startsWithIgnoreCase(a, prefix)) {
                    matches.push_back(a);
                }
            }
            return matches;
        }
        else if (argIndex == 3) {
            return { "?<degrees>  Rotation angle" };
        }
    }

    // ---- Sketch mode geometry commands ----
    if (m_inSketchMode) return completeSketchArguments(tokens, prefix, argIndex);

    return {};
}

// Helper to tokenize a command line, keeping parenthesized expressions intact
// e.g., "circle (a + b),(c * d) radius (r * 2)" -> ["circle", "(a + b),(c * d)", "radius", "(r * 2)"]
// ---- Undo / redo ----------------------------------------------------

namespace {

/// Shared by undo and redo: parse an optional repeat count.
/// Returns -1 if the argument was present but not a positive integer.
int parseRepeatCount(const std::vector<std::string>& args)
{
    if (args.empty()) return 1;
    bool ok = false;
    const int n = toInt(args.front(), &ok);
    return (ok && n > 0) ? n : -1;
}

}  // namespace

CliResult CliEngine::sketchUndoRedo(int count, bool redo)
{
    // Inside a sketch, undo and redo walk the sketch's own history, as the
    // canvas does; the document's history waits until the sketch is
    // finished or discarded.
    SelectionKeeper keeper(m_selectedEntityId);
    const std::vector<sketch::UndoCommand> applied = redo
        ? m_sketchEdits.redo(m_pendingSketch.entities, m_pendingSketch.constraints,
                             m_pendingSketch.groups, count, &keeper)
        : m_sketchEdits.undo(m_pendingSketch.entities, m_pendingSketch.constraints,
                             m_pendingSketch.groups, count, &keeper);
    std::vector<std::string> done;
    for (const sketch::UndoCommand& cmd : applied) done.push_back(cmd.description);
    CliResult r;
    if (done.empty()) {
        r.output = redo ? translate("QObject", "Nothing to redo.")
                        : translate("QObject", "Nothing to undo.");
        return r;
    }
    r.output = subst(redo ? translate("QObject", "Redone: %1")
                          : translate("QObject", "Undone: %1"),
                     join(done, ", "));
    if (static_cast<int>(done.size()) < count) {
        r.output += subst(translate("QObject", "\n(only %1 of %2 steps were available)"),
                          done.size(), count);
    }
    const std::string next = redo ? m_sketchEdits.history().redoDescription()
                                  : m_sketchEdits.history().undoDescription();
    if (!next.empty()) {
        r.output += subst(redo ? translate("QObject", "\nNext redo: %1")
                               : translate("QObject", "\nNext undo: %1"),
                          next);
    }
    return r;
}

CliResult CliEngine::cmdUndo(const std::vector<std::string>& args)
{
    CliResult r;
    const int count = parseRepeatCount(args);
    if (count < 0) {
        return failure(translate("QObject", "Usage: undo [count]"));
    }
    if (m_inSketchMode) {
        return sketchUndoRedo(count, false);
    }
    if (!m_undoHost) {
        return failure(translate("QObject", "No document is open, so there is nothing to undo."));
    }

    std::vector<std::string> done;
    for (int i = 0; i < count; ++i) {
        std::string what;
        if (!m_undoHost->undoDocument(&what)) {
            break;   // ran out, or a command no longer fits the document
        }
        done.push_back((what));
    }

    if (done.empty()) {
        r.output = translate("QObject", "Nothing to undo.");
        return r;
    }
    // Report every step, not just a count: after "undo 5" the user needs to
    // know what actually came off, especially if fewer than asked were done.
    r.output = subst(translate("QObject", "Undone: %1"), join(done, ", "));
    if (static_cast<int>(done.size()) < count) {
        r.output += subst(translate("QObject", "\n(only %1 of %2 steps were available)"), done.size(), count);
    }
    const std::string next = (m_undoHost->nextUndoDescription());
    if (!next.empty()) {
        r.output += subst(translate("QObject", "\nNext undo: %1"), next);
    }
    return r;
}

CliResult CliEngine::cmdRedo(const std::vector<std::string>& args)
{
    CliResult r;
    const int count = parseRepeatCount(args);
    if (count < 0) {
        return failure(translate("QObject", "Usage: redo [count]"));
    }
    if (m_inSketchMode) {
        return sketchUndoRedo(count, true);
    }
    if (!m_undoHost) {
        return failure(translate("QObject", "No document is open, so there is nothing to redo."));
    }

    std::vector<std::string> done;
    for (int i = 0; i < count; ++i) {
        std::string what;
        if (!m_undoHost->redoDocument(&what)) {
            break;
        }
        done.push_back((what));
    }

    if (done.empty()) {
        r.output = translate("QObject", "Nothing to redo.");
        return r;
    }
    r.output = subst(translate("QObject", "Redone: %1"), join(done, ", "));
    if (static_cast<int>(done.size()) < count) {
        r.output += subst(translate("QObject", "\n(only %1 of %2 steps were available)"), done.size(), count);
    }
    const std::string next = (m_undoHost->nextRedoDescription());
    if (!next.empty()) {
        r.output += subst(translate("QObject", "\nNext redo: %1"), next);
    }
    return r;
}

// ---- Main dispatch --------------------------------------------------

CliResult CliEngine::execute(const std::string& line)
{
    std::vector<std::string> stdTokens = sketch::tokenizeLine(line);

    // Coordinate fragments split by spaces around commas are rejoined, so
    // every command's coord parser is space-tolerant without per-command code.
    stdTokens = sketch::mergeCoordinateTokens(std::move(stdTokens));

    std::vector<std::string> tokens;
    tokens.reserve(static_cast<int>(stdTokens.size()));
    for (const auto& t : stdTokens)
        tokens.push_back((t));

    if (tokens.empty()) return {};

    std::string cmd = toLower(tokens.front());

    if (cmd == "exit" ||
        cmd == "quit") {
        CliResult r;
        r.requestExit = true;
        return r;
    }

    // Inside a sketch, a command that edits it is run once more under a
    // snapshot, and what it changed goes into the sketch's history.
    if (m_inSketchMode && !m_recordingSketchEdit && editsSketch(cmd)) {
        const SketchData before = m_pendingSketch;
        m_recordingSketchEdit = true;
        CliResult result = execute(line);
        m_recordingSketchEdit = false;
        if (m_inSketchMode) {
            m_sketchEdits.recordChanges(before.entities, before.constraints, before.groups,
                                        m_pendingSketch.entities, m_pendingSketch.constraints,
                                        m_pendingSketch.groups, trim(line));
        }
        return result;
    }

    if (cmd == "help")    return cmdHelp(slice(tokens, 1));
    if (cmd == "version") return cmdVersion();
    if (cmd == "new")     return cmdNew(slice(tokens, 1));
    if (cmd == "open")    return cmdOpen(slice(tokens, 1));
    if (cmd == "save")    return cmdSave(slice(tokens, 1));
    if (cmd == "convert") return cmdConvert(slice(tokens, 1));
    if (cmd == "script")  return cmdScript(slice(tokens, 1));
    if (cmd == "cd")      return cmdCd(slice(tokens, 1));
    if (cmd == "pwd")     return cmdPwd();
    if (cmd == "info")    return cmdInfo();
    if (cmd == "deselect")   return cmdDeselect();
    if (cmd == "print")      return cmdPrint(slice(tokens, 1));
    if (cmd == "export")     return cmdExport(slice(tokens, 1));
    if (cmd == "sketches")   return cmdSketches();
    if (cmd == "bodies")     return cmdBodies();
    if (cmd == "planes")     return cmdPlanes();
    if (cmd == "constraints") return cmdConstraints();
    if (cmd == "groups") return cmdGroups();
    if (cmd == "parameters") return cmdParameters(slice(tokens, 1));
    if (cmd == "coords")     return cmdCoords(slice(tokens, 1));
    if (cmd == "history") return cmdHistory(slice(tokens, 1));
    if (cmd == "undo")    return cmdUndo(slice(tokens, 1));
    if (cmd == "redo")    return cmdRedo(slice(tokens, 1));
    if (cmd == "select")  return cmdSelect(slice(tokens, 1));
    if (cmd == "create")  return cmdCreate(slice(tokens, 1));
    if (cmd == "delete")  return cmdDelete(slice(tokens, 1));
    if (cmd == "rename")  return cmdRename(slice(tokens, 1));
    if (cmd == "extrude") return cmdExtrude(slice(tokens, 1));
    if (cmd == "revolve") return cmdRevolve(slice(tokens, 1));

    // Viewport commands (only work in full mode with a viewport)
    if (cmd == "zoom")    return cmdZoom(slice(tokens, 1));
    if (cmd == "panto")   return cmdPanTo(slice(tokens, 1));
    if (cmd == "rotate")  return cmdRotate(slice(tokens, 1));

    // Sketch mode commands
    if (cmd == "finish")  return cmdFinish();
    if (cmd == "discard") return cmdDiscard();

    // Sketch geometry commands (only in sketch mode)
    if (m_inSketchMode) {
        if (cmd == "point")     return cmdSketchPoint(slice(tokens, 1));
        if (cmd == "line")      return cmdSketchLine(slice(tokens, 1));
        if (cmd == "circle")    return cmdSketchCircle(slice(tokens, 1));
        if (cmd == "rectangle") return cmdSketchRectangle(slice(tokens, 1));
        if (cmd == "arc")       return cmdSketchArc(slice(tokens, 1));
        if (cmd == "polygon")   return cmdSketchPolygon(slice(tokens, 1));
        if (cmd == "ellipse")   return cmdSketchEllipse(slice(tokens, 1));
        if (cmd == "slot")      return cmdSketchSlot(slice(tokens, 1));
        if (cmd == "spline")    return cmdSketchSpline(slice(tokens, 1));
        if (cmd == "bezier")    return cmdSketchBezier(slice(tokens, 1));
        if (cmd == "conic")     return cmdSketchConic(slice(tokens, 1));
        if (cmd == "text")      return cmdSketchText(slice(tokens, 1));
        if (cmd == "constrain") return cmdConstrain(slice(tokens, 1));
        if (cmd == "solve")     return cmdSolve(slice(tokens, 1));
        if (cmd == "group")     return cmdGroup(slice(tokens, 1));
        if (cmd == "transform") return cmdTransform(slice(tokens, 1));
        if (cmd == "sweep")     return cmdSweep(slice(tokens, 1));
        if (cmd == "projection") return cmdProject(slice(tokens, 1));
        if (cmd == "points")    return cmdPoints(slice(tokens, 1));
        if (cmd == "set")       return cmdSet(slice(tokens, 1));
    }

    CliResult r;
    r.exitCode = 1;

    // Three distinct situations, and we can always tell which one it is, so
    // none of them should hedge:
    //
    //   1. a real command, wrong context   -> name the context
    //   2. a near miss                     -> suggest the command
    //   3. nothing like it                 -> unknown
    //
    // Aaron asked whether this should read "Unknown command: circle or wrong
    // context". It should not: "or" would be admitting we did not check,
    // when the command table above says exactly which case applies. A
    // message listing both possibilities makes the reader redo work the
    // program already did.
    if (const CommandSpec* spec = findCommand(cmd)) {
        // Reached here despite being a known command, so it is scoped out.
        if (spec->scope == CmdScope::Sketch) {
            r.error = subst(
                "'%1' works inside a sketch, and no sketch is open.\n"
                "Use 'create sketch <plane> <name>' first, or "
                "'select sketch <name>' for an existing one.", cmd);
        } else {
            // No other scope exists yet. Rather than invent an explanation,
            // say plainly that we do not know why; a wrong reason is worse
            // than an admitted gap.
            r.error = subst(
                "'%1' is a known command but is not available here.", cmd);
        }
        return r;
    }

    const std::vector<std::string> nearby = nearestCommands(cmd);
    if (!nearby.empty()) {
        r.error = subst("Unknown command: %1\nDid you mean: %2?", cmd, join(nearby, ", "));
        return r;
    }

    r.error = "Unknown command: " + cmd +
              "\nType 'help' for available commands.";
    return r;
}

// ---- Individual commands --------------------------------------------

CliResult CliEngine::cmdHelp(const std::vector<std::string>& args) const
{
    if (!args.empty()) {
        // One command: what it does, from the registry the menus and
        // toolbars use, and where it works.
        const std::string name = toLower(args.front());
        const CommandSpec* spec = findCommand(name);
        if (!spec) {
            const std::vector<std::string> nearby = nearestCommands(name);
            return failure(nearby.empty()
                ? subst("Unknown command: %1", name)
                : subst("Unknown command: %1\nDid you mean: %2?", name, join(nearby, ", ")));
        }
        std::string text = spec->name;
        if (const commands::Command* c =
                spec->registryId ? commands::findCommand(spec->registryId) : nullptr) {
            const commands::Text& about = c->tooltip.empty() ? c->label : c->tooltip;
            text += " - " + hobbycad::translate(commands::commandContext(), about.source,
                                                about.disambiguation);
        }
        text += spec->scope == CmdScope::Sketch ? "\nWorks while a sketch is open."
                                                : "\nWorks at any prompt.";
        CliResult r;
        r.output = text;
        return r;
    }

    CliResult r;
    std::string helpText = subst(
        "Available commands:\n"
        "\n"
        "File Operations:\n"
        "  new [discard]           Start an empty project\n"
        "  open <path> [discard]   Open a project (directory or .hcad) or a .brep file\n"
        "  save [<path>]           Save the project; a .brep path writes the bodies\n"
        "  convert <in> <out>      Convert between file formats\n"
        "  script <file>           Execute a script file\n"
        "\n"
        "Information:\n"
        "  help [<command>]        Show this help message, or what one command does\n"
        "  version                 Show HobbyCAD version\n"
        "  info                    Show current document info\n"        "\n"
        "Contents:\n"
        "  sketches                List the sketches in the document\n"
        "  bodies                  List the solid bodies\n"
        "  planes                  List the construction planes\n"
        "  constraints             List the open or selected sketch's\n"
        "                          constraints\n"        "  groups                  List its groups\n"        "  parameters [<name> <expression>]\n"
        "                          List parameters, or set one\n"
        "\n"
        "Selection and scripting:\n"
        "  select <type> <name>    Select an object; the prompt follows it\n"
        "  deselect                Return to the document context\n"
        "  print                   Show what is in the current context\n"
        "  export [all] [file=<path>]\n"
        "                          Emit a replayable script (full precision)\n"
        "\n"
        "Navigation:\n"
        "  cd [dir]                Change working directory (no arg = home)\n"
        "  pwd                     Print working directory\n"
        "Editing:\n"
        "  undo [count]            Undo document changes (default 1)\n"
        "  redo [count]            Redo document changes (default 1)\n"
        "\n"
        "  history                 Show command history\n"
        "  history clear           Clear command history\n"
        "  history max <n>         Set max history lines (current: %1)\n"
        "\n"
        "Selection & Creation:\n"
        "  select <type> <name|id=n>      sketch, body or plane\n"
        "  delete <type> <name|id=n>      Undoable\n"
        "  rename <type> <name|id=n> <new name>\n"
        "  create sketch [plane] [name]   Create a sketch (XY, XZ, YZ, on [plane] <name>)\n"
        "  extrude <sketch> <distance> [reverse|symmetric] [join|cut|intersect|new]\n"
        "  revolve <sketch> [angle <deg>] [about x|y|line <id>] [join|cut|intersect|new]\n"
        "                                 3D from a finished sketch; no viewport needed\n"
        "\n"
        "Sketch geometry (only while a sketch is open):\n"
        "  point [at] <x>,<y>\n"
        "  line [from] <x>,<y> to <x>,<y>\n"
        "  rectangle [from] <x>,<y> to <x>,<y>\n"
        "  circle [at] <x>,<y> radius <r>\n"
        "  arc [at] <x>,<y> radius <r> [angle] <start> to <end>\n"
        "  polygon [at] <x>,<y> radius <r> sides <n>\n"
        "  ellipse [at] <x>,<y> major <a> minor <b>\n"
        "          [rotation <deg>] [angle <start> to <end>]\n"
        "  slot [from] <x>,<y> to <x>,<y> radius <r>\n"
        "  spline [through] <x>,<y> <x>,<y> [<x>,<y> ...]\n"
        "  bezier <x>,<y> [in|out|tan <ang> <len>] [weight <w>] <x>,<y> ...\n"
        "  conic [from] <x>,<y> to <x>,<y> apex <x>,<y> rho <r>\n"
        "  text <string> [at] <x>,<y> [size <n>] [rotation <deg>]\n"
        "                          Every one of these takes an optional\n"
        "                          trailing 'construction' keyword.\n"
        "  constrain <type> <id>... [<value>] [reference]\n"
        "                          Apply a constraint; \"constrain\" alone\n"
        "                          with no type lists every type\n"
        "  solve                   Solve the sketch and report its state\n"        "  group <name> [id=<n>] [entities <ids>] [constraints <ids>]\n"
        "        [groups <ids>] [locked] [pivot <x>,<y>|center]\n"
        "                          Gather members under a name\n"
        "  transform move <dx>,<dy> | rotate <deg> | scale <f> | mirror x|y\n"
        "            | mirror line <x1>,<y1> <x2>,<y2> | point-to-point <x1>,<y1> <x2>,<y2>\n"
        "            [about <x>,<y>|center] [copy] [group <name>|id=<n>]\n"
        "                          Move the selected entity, or a whole\n"
        "                          group, as one unit; then re-solve\n"
        "  select [entity] <id>    Select an entity by id\n"
        "  set [<id>] <property> <value>\n"
        "                          Change one property of an entity (the\n"
        "                          selected one without an id); \"set <id>\"\n"
        "                          alone lists its properties\n"
        "  delete [entity] <id>    Remove an entity from the sketch\n"
        "  delete constraint <id>  Remove a constraint\n"
        "  finish                  Commit the sketch to the document\n"
        "  discard                 Throw the sketch away\n"
        "\n"
        "Viewport (full mode only):\n"
        "  zoom <percent>          Set zoom level (e.g., zoom 200)\n"
        "  zoom home               Reset zoom to fit all objects\n"
        "  panto <x>,<y>,<z>       Pan camera to center on coordinates\n"
        "  panto home              Pan to origin (0,0,0)\n"
        "  rotate on <axis> <deg>  Rotate view (e.g., rotate on z 45)\n"
        "  rotate home             Reset to isometric view\n", m_history.maxLines());

    if (m_inSketchMode) {
        helpText += 
            "\n"
            "Sketch Geometry:\n"
            "  point [at] <x>,<y>\n"
            "  line [from] <x>,<y> to <x>,<y>\n"
            "  circle [at] <x>,<y> radius|diameter <value>\n"
            "  rectangle [from] <x>,<y> to <x>,<y>\n"
            "  arc [at] <x>,<y> radius <r> [angle] <start> to <end>\n"
            "\n"
            "  Values can be numbers, parameters, or (expressions):\n"
            "    circle 0,0 radius 25\n"
            "    circle 0,0 radius myRadius\n"
            "    circle (width/2),(height/2) radius (size*0.5)\n"
            "\n"
            "Sketch Mode:\n"
            "  finish                  Save and exit sketch mode\n"
            "  discard                 Discard changes and exit sketch mode\n";
    }

    helpText += 
        "\n"
        "  exit / quit             Exit HobbyCAD\n";

    r.output = helpText;
    return r;
}

CliResult CliEngine::cmdVersion() const
{
    CliResult r;
    r.output = std::string("HobbyCAD ") +
               hobbycad::version();
    return r;
}

static CliResult noDocument();   // defined with the listing commands below

std::string CliEngine::replaceProjectBlocker(bool discard) const
{
    if (m_inSketchMode) {
        return "A sketch is open at this prompt. Finish or discard it first.";
    }
    if (discard) return {};
    const hobbycad::Project* proj = project();
    if (proj && proj->isModified()) {
        return "The project has unsaved changes. Save it, or add \"discard\" to drop them.";
    }
    if (m_docHost && m_docHost->hostHasOpenWork()) {
        return "A sketch is open in the window. Finish it, or add \"discard\" to drop it.";
    }
    return {};
}

void CliEngine::adoptReplacedProject()
{
    // Selections, entity ids and sketch numbering named things in the old project.
    m_context = CliContext{};
    m_selectedEntityId = -1;
    m_sketchCounter = 0;
    if (hobbycad::ProjectSession* s = session()) {
        s->clearHistory();
        s->adoptOrphanSketches();
    }
    if (m_docHost) m_docHost->hostProjectReplaced();
}

CliResult CliEngine::cmdNew(const std::vector<std::string>& args)
{
    // An empty project in place of the open one. This used to build a test
    // box in a throwaway document, report it, and change nothing.
    hobbycad::Project* proj = project();
    if (!proj || !session()) return noDocument();

    const bool discard = !args.empty()
        && equalsIgnoreCase(args[0], "discard");
    if (const std::string why = replaceProjectBlocker(discard); !why.empty()) {
        return failure(why);
    }

    proj->close();
    adoptReplacedProject();

    CliResult r;
    r.output = "New project. \"save <directory>\" writes it.";
    return r;
}

CliResult CliEngine::cmdOpen(const std::vector<std::string>& args)
{
    // It used to read a BREP file and throw the shapes away.
    if (args.empty()) {
        return failure(
            "Usage: open <project directory | name.hcad | file.brep> [discard]");
    }
    hobbycad::Project* proj = project();
    if (!proj || !session()) return noDocument();

    std::vector<std::string> parts = args;
    bool discard = false;
    if (static_cast<int>(parts.size()) > 1
        && equalsIgnoreCase(parts.back(), "discard")) {
        discard = true;
        parts.pop_back();
    }
    if (const std::string why = replaceProjectBlocker(discard); !why.empty()) {
        return failure(why);
    }

    std::string path = join(parts, " ");
    namespace fs = std::filesystem;
    std::error_code ec;
    // A bare name that is not there may be a BREP file without its extension.
    if (!fs::exists(path, ec) && fs::path(path).extension().empty()
        && fs::exists(path + ".brep", ec)) {
        path += ".brep";
    }
    if (!fs::exists(path, ec)) {
        return failure(subst("No such file or directory: %1", path));
    }

    CliResult r;
    const hobbycad::CadFileFormat format =
        hobbycad::fileFormatFromPath(path, std::filesystem::is_directory(path, ec));

    if (format == hobbycad::CadFileFormat::Project) {
        std::string err;
        if (!proj->load(path, &err)) {
            if (m_docHost) m_docHost->hostDocumentChanged();
            return failure(subst("Could not open project %1: %2", path, (err)));
        }
        adoptReplacedProject();
        r.output = subst("Opened project '%1': %2 sketch(es), %3 body/bodies.", (proj->name()), proj->sketches().size(), proj->bodies().size());
        return r;
    }

    if (format == hobbycad::CadFileFormat::Brep) {
        std::string err;
        const auto shapes = brep_io::readBrep(path, &err);
        if (shapes.empty()) {
            return failure(subst("Could not read %1: %2", path, (err)));
        }
        // Raw geometry: a new, unsaved project holding the shapes as bodies.
        proj->close();
        for (const TopoDS_Shape& shape : shapes) {
            proj->addBody(shape);
        }
        proj->setModified(false);
        adoptReplacedProject();
        r.output = subst("Opened %1 as a new, unsaved project with %2 body/bodies.", path, shapes.size());
        return r;
    }

    return failure(subst(
        "Cannot open %1: give a project directory, a .hcad manifest or a .brep file.", path));
}

CliResult CliEngine::cmdSave(const std::vector<std::string>& args)
{
    // Writes the real project. This used to build a test box and save that,
    // whatever the document held.
    hobbycad::Project* proj = project();
    hobbycad::ProjectSession* s = session();
    if (!proj || !s) return noDocument();

    const std::string path = join(args, " ");
    CliResult r;

    // A .brep or .brp path exports the bodies; the project itself is unchanged.
    if (!path.empty()
        && hobbycad::fileFormatFromPath(path) == hobbycad::CadFileFormat::Brep) {
        std::vector<TopoDS_Shape> shapes;
        for (const auto& b : proj->bodies()) {
            shapes.push_back(b.shape);
        }
        if (shapes.empty()) {
            return failure("There are no bodies to write.");
        }
        std::string err;
        if (!brep_io::writeBrep(path, shapes, &err)) {
            return failure(subst("Could not write %1: %2", path, (err)));
        }
        r.output = subst("Wrote %1 body/bodies to %2.", shapes.size(), path);
        return r;
    }

    if (path.empty() && proj->isNew()) {
        return failure(
            "Usage: save <directory>  (this project has not been saved yet)");
    }

    std::string target;
    if (!path.empty()) {
        target = hobbycad::projectDirForSavePath(path);
        proj->setName(std::filesystem::path(target).filename().string());
    }

    // Save is a checkpoint: a sketch open at this prompt goes in as it stands
    // and stays open.
    hobbycad::SketchDraft draft;
    if (m_inSketchMode) {
        draft.sketch = m_pendingSketch;
        draft.sketch.id = -1;
        draft.sketch.name = m_currentSketchName;
        draft.sketch.plane = m_currentSketchPlane;
        draft.sketch.constructionPlaneId = m_currentConstructionPlaneId;
    }
    std::string err;
    if (!s->save(target, &err, m_inSketchMode ? &draft : nullptr)) {
        return failure(subst("Could not save: %1", (err)));
    }
    if (m_docHost) m_docHost->hostDocumentChanged();

    r.output = subst("Saved project '%1' to %2.", (proj->name()), (proj->projectPath()));
    return r;
}

CliResult CliEngine::cmdConvert(const std::vector<std::string>& args)
{
    CliResult r;

    // Check for help flag
    if (!args.empty() && (args[0] == "--help" ||
                            args[0] == "-h")) {
        r.output = 
            "Usage: convert [options] <input> <output>\n"
            "\n"
            "Convert between CAD file formats.\n"
            "\n"
            "Arguments:\n"
            "  <input>                  Input file path\n"
            "  <output>                 Output file path\n"
            "\n"
            "Options:\n"
            "  -h, --help               Show this help message\n"
            "  --format <fmt>           Force output format (auto-detected from extension)\n"
            "\n"
            "Supported Formats:\n"
            "  .hcad                    HobbyCAD project\n"
            "  .brep, .brp              OpenCASCADE BREP\n"
            "\n"
            "Examples:\n"
            "  convert model.brep project/\n"
            "  convert myproject/ export.brep";
        return r;
    }

    // Parse arguments
    std::string inputPath;
    std::string outputPath;
    std::string format;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        if (args[i] == "--format" && i + 1 < static_cast<int>(args.size())) {
            format = args[++i];
        } else if (!startsWith(args[i], '-')) {
            if (inputPath.empty()) {
                inputPath = args[i];
            } else if (outputPath.empty()) {
                outputPath = args[i];
            }
        }
    }

    if (inputPath.empty() || outputPath.empty()) {
        r.exitCode = 1;
        r.error = 
            "Usage: convert <input> <output>\n"
            "\n"
            "Run 'convert --help' for more options.";
        return r;
    }

    // Check if input exists
    std::error_code convertEc;
    if (!std::filesystem::exists(inputPath, convertEc)) {
        r.exitCode = 1;
        r.error = "Input file not found: " + inputPath;
        return r;
    }

    // STL -> STEP: the HobbyMesh slice + loft reverse-engineering pipeline.
    const hobbycad::CadFileFormat inFmt =
        hobbycad::fileFormatFromPath(inputPath,
        std::filesystem::is_directory(inputPath, convertEc));
    const hobbycad::CadFileFormat outFmt = format.empty()
        ? hobbycad::fileFormatFromPath(outputPath)
        : hobbycad::fileFormatFromName(format);
    if (inFmt == hobbycad::CadFileFormat::Stl && outFmt == hobbycad::CadFileFormat::Step) {
        const int sections = hobbycad::brep::kDefaultStlSections;
        std::string cerr;
        if (!hobbycad::brep::stlToStep(inputPath, outputPath,
                                       gp_Dir(0, 0, 1), sections, &cerr)) {
            r.exitCode = 1;
            r.error = "STL->STEP failed: " + (cerr);
            return r;
        }
        r.output = subst("Converted %1 -> %2 (%3 sections sliced and lofted)", inputPath, outputPath, sections);
        return r;
    }

    const bool inputIsProject = inFmt == hobbycad::CadFileFormat::Project;
    const bool inputIsBrep = inFmt == hobbycad::CadFileFormat::Brep;
    bool outputIsProject = outFmt == hobbycad::CadFileFormat::Project;
    bool outputIsBrep = outFmt == hobbycad::CadFileFormat::Brep;

    // Default to BREP if no format detected
    if (!outputIsProject && !outputIsBrep) {
        outputIsBrep = true;
        if (!contains(outputPath, '.')) {
            outputPath += ".brep";
        }
    }

    // Read input
    std::vector<TopoDS_Shape> shapes;
    std::string err;

    if (inputIsBrep) {
        shapes = brep_io::readBrep(inputPath, &err);
        if (shapes.empty() && !err.empty()) {
            r.exitCode = 1;
            r.error = "Failed to read input: " + (err);
            return r;
        }
    } else if (inputIsProject) {
        hobbycad::Project source;
        if (!source.load(inputPath, &err)) {
            r.exitCode = 1;
            r.error = "Failed to read input: " + (err);
            return r;
        }
        for (const auto& b : source.bodies()) {
            shapes.push_back(b.shape);
        }
    } else {
        r.exitCode = 1;
        r.error = "Unknown input format: " + inputPath;
        return r;
    }

    // Write output
    if (outputIsBrep) {
        if (!brep_io::writeBrep(outputPath, shapes, &err)) {
            r.exitCode = 1;
            r.error = "Failed to write output: " + (err);
            return r;
        }
    } else if (outputIsProject) {
        hobbycad::Project written;
        for (const TopoDS_Shape& shape : shapes) {
            written.addBody(shape);
        }
        if (!written.save(outputPath, &err)) {
            r.exitCode = 1;
            r.error = "Failed to write output: " + (err);
            return r;
        }
    }

    r.output = subst("Converted: %1 -> %2 (%3 shape(s))", inputPath, outputPath, static_cast<int>(shapes.size()));
    return r;
}

CliResult CliEngine::cmdScript(const std::vector<std::string>& args)
{
    CliResult r;

    // Check for help flag
    if (!args.empty() && (args[0] == "--help" ||
                            args[0] == "-h")) {
        r.output = 
            "Usage: script [options] [file]\n"
            "\n"
            "Execute a HobbyCAD script file.\n"
            "\n"
            "Arguments:\n"
            "  <file>                   Script file to execute\n"
            "  -                        Read script from stdin (for piping)\n"
            "\n"
            "Options:\n"
            "  -h, --help               Show this help message\n"
            "  --dry-run                Check syntax without executing\n"
            "\n"
            "Script files contain CLI commands, one per line.\n"
            "Lines starting with '#' are treated as comments.\n"
            "\n"
            "Example script (egg.txt):\n"
            "  # Create an egg shape from a cube\n"
            "  new\n"
            "  box 10 10 10\n"
            "  fillet 2\n"
            "  scale 1 1 1.5\n"
            "  save myegg/\n"
            "\n"
            "Run with:\n"
            "  script egg.txt\n"
            "  script --dry-run egg.txt   # Validate without running\n"
            "  cat egg.txt | hobbycad script -";
        return r;
    }

    // Parse options
    bool checkOnly = false;
    std::string scriptPath;

    for (const std::string& arg : args) {
        if (arg == "--check" || arg == "--dry-run") {
            checkOnly = true;
        } else if (!startsWith(arg, '-')) {
            scriptPath = arg;
        }
    }

    bool readFromStdin = scriptPath.empty() || scriptPath == "-";

    std::ifstream fileStream;
    std::istream* in = &std::cin;

    if (!readFromStdin) {
        std::error_code scriptEc;
        if (!std::filesystem::exists(scriptPath, scriptEc)) {
            r.exitCode = 1;
            r.error = "Script file not found: " + scriptPath;
            return r;
        }

        fileStream.open(scriptPath);
        if (!fileStream) {
            r.exitCode = 1;
            r.error = std::string("Could not open script file: ")
                      + std::strerror(errno);
            return r;
        }
        in = &fileStream;
    }

    int lineNum = 0;
    int commandCount = 0;
    int errorCount = 0;
    std::string output;

    std::string rawLine;
    while (std::getline(*in, rawLine)) {
        // trim also drops the carriage return a script written on Windows
        // carries, which QTextStream used to strip.
        std::string line = trim(rawLine);
        lineNum++;

        // Skip empty lines and comments
        if (line.empty() || startsWith(line, '#')) {
            continue;
        }

        commandCount++;

        if (checkOnly) {
            // Syntax check only - validate command name exists
            std::vector<std::string> tokens = splitWhitespace(line);
            if (tokens.empty()) continue;

            std::string cmd = toLower(tokens.front());
            std::vector<std::string> validCmds = commandNames();

            // Also add sketch commands if we might be in sketch mode
            validCmds.push_back("point");
            validCmds.push_back("line");
            validCmds.push_back("circle");
            validCmds.push_back("rectangle");
            validCmds.push_back("arc");
            validCmds.push_back("finish");
            validCmds.push_back("discard");

            if (!contains(validCmds, cmd)) {
                output += subst("[%1] ERROR: Unknown command '%2'\n", lineNum, cmd);
                errorCount++;
            } else {
                output += subst("[%1] OK: %2\n", lineNum, line);
            }
        } else {
            // Execute the command
            CliResult cmdResult = execute(line);

            if (!cmdResult.output.empty()) {
                output += subst("[%1] %2\n", lineNum, cmdResult.output);
            }

            if (cmdResult.exitCode != 0) {
                r.exitCode = 1;
                r.error = subst("Error at line %1: %2", lineNum, cmdResult.error);
                if (!output.empty()) {
                    r.output = output;
                }
                return r;
            }

            if (cmdResult.requestExit) {
                // Script requested exit
                break;
            }
        }
    }

    if (checkOnly) {
        if (errorCount > 0) {
            r.exitCode = 1;
            r.output = output;
            r.error = subst("Syntax check failed: %1 error(s) in %2 command(s)", errorCount, commandCount);
        } else {
            r.output = output + subst("\nSyntax check passed: %1 command(s) OK", commandCount);
        }
    } else {
        r.output = output + subst("\nScript completed: %1 command(s) executed.", commandCount);
    }

    return r;
}

CliResult CliEngine::cmdCd(const std::vector<std::string>& args)
{
    CliResult r;
    std::string target;

    if (args.empty()) {
        target = homeDirectory();
    } else {
        target = join(args, " ");
#if !defined(_WIN32)
        if (startsWith(target, "~/")) {
            target = homeDirectory() + target.substr(1);
        } else if (target == "~") {
            target = homeDirectory();
        }
#endif
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(target, ec)) {
        return failure("cd: no such directory: " + target);
    }
    fs::current_path(fs::absolute(target, ec), ec);
    if (ec) {
        return failure("cd: failed to change to: " + target);
    }

    return r;
}

CliResult CliEngine::cmdPwd() const
{
    CliResult r;
    std::error_code ec;
    r.output = std::filesystem::current_path(ec).string();
    return r;
}

hobbycad::Project* CliEngine::project()
{
    return m_docHost ? m_docHost->hostProject() : nullptr;
}

const hobbycad::Project* CliEngine::project() const
{
    return m_docHost ? m_docHost->hostProject() : nullptr;
}

std::map<std::string, double> CliEngine::parameterValues() const
{
    std::map<std::string, double> values;
    if (const hobbycad::Project* p = project()) {
        for (const auto& param : p->parameters()) {
            values[param.name] = param.value;
        }
    }
    return values;
}

std::map<std::string, std::array<double, 3>> CliEngine::namedPointValues() const
{
    static const std::vector<hobbycad::NamedPointData> kNone;
    const hobbycad::Project* p = project();
    return hobbycad::evaluateNamedPoints(p ? p->namedPoints() : kNone, parameterValues());
}

std::vector<std::string> CliEngine::parameterNames() const
{
    if (const hobbycad::Project* p = project()) {
        std::vector<std::string> names;
        for (const auto& param : p->parameters()) {
            names.push_back((param.name));
        }
        if (!names.empty()) {
            return names;
        }
    }

    // No document, or one with no parameters yet: offer the common names
    // so completion is not empty before a project exists.
    return {"width",  "height",
            "depth",  "radius",
            "diameter", "thickness",
            "offset"};
}

/// Shared "nothing is open" answer, so every listing command says the same
/// thing rather than each inventing its own wording.
static CliResult noDocument()
{
    CliResult r;
    r.exitCode = 1;
    r.error = "No document is open. Use \"new\" or \"open <file>\".";
    return r;
}

CliResult CliEngine::cmdSketches() const
{
    const hobbycad::Project* p = project();
    if (!p) return noDocument();

    CliResult r;
    const auto& sketches = p->sketches();
    if (sketches.empty()) {
        r.output = "No sketches.";
        return r;
    }
    std::vector<std::string> lines;
    lines.push_back(subst("%1  %2  %3", pad("id", -6), pad("entities", -9), "name"));
    for (const auto& sk : sketches) {
        lines.push_back(subst("%1  %2  %3", pad(numToString(sk.id), -6), pad(numToString(sk.entities.size()), -9), (sk.name)));
    }
    r.output = join(lines, '\n');
    return r;
}

CliResult CliEngine::cmdBodies() const
{
    const hobbycad::Project* p = project();
    if (!p) return noDocument();

    CliResult r;
    const auto& bodies = p->bodies();
    if (bodies.empty()) {
        r.output = "No bodies.";
        return r;
    }
    std::vector<std::string> lines;
    lines.push_back("id      name");
    for (const auto& b : bodies) {
        lines.push_back(subst("%1  %2", pad(numToString(b.id), -6), (b.name)));
    }
    r.output = join(lines, '\n');
    return r;
}

static bool parseCoord3(const std::string& str, double& x, double& y, double& z,
                        const std::map<std::string, double>& params,
                        const std::map<std::string, std::array<double, 3>>& named = {});

CliResult CliEngine::cmdCreatePlane(const std::vector<std::string>& args)
{
    CliResult r;
    hobbycad::Project* p = project();
    if (!p) { return failure("No project loaded."); }

    double ox = 0, oy = 0, oz = 0, offset = 0, rx = 0, ry = 0, rz = 0;
    bool relative = false;
    std::string refName, name;

    // Parse a comma coordinate starting at index i, tolerant of spaces around
    // the commas: "3,4,5", "3, 4, 5" and "3 , 4 , 5" all work (the tokenizer
    // splits on spaces, so we re-join and split on commas). Returns the number
    // of tokens consumed, or 0 if the tokens there are not a coordinate.
    // Each component may be a number, a formula, or a user parameter name,
    // evaluated with the project's parameters, the same machinery the sketch
    // coordinate commands use. The parse is tolerant of spaces around the
    // commas: "3,4,5", "3, 4, 5" and "3 , 4 , 5" all work (the tokenizer splits
    // on spaces, so we re-join and split on commas).
    const std::map<std::string, double> params = parameterValues();
    auto evalPart = [&params](const std::string& part, double& v) -> bool {
        return hobbycad::evaluateExpression(trim(part), v, params);
    };

    // Coordinates arrive as single tokens (the tokenizer merged any spaces
    // around the commas), so each "x,y,z" parses with the shared, bracket-aware
    // parseCoord3; each component may be a number, formula or parameter.
    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const std::string w = toLower(args[i]);
        if (w == "at" && i + 1 < static_cast<int>(args.size())
            && parseCoord3(args[i + 1], ox, oy, oz, params, namedPointValues())) {
            ++i;
        } else if (w == "offset" && i + 1 < static_cast<int>(args.size())
                   && toLower(args[i + 1]) == "from") {
            ++i;  // consume "from"; the origin (coord) or "relative to" follows
        } else if (w == "relative" && i + 1 < static_cast<int>(args.size())
                   && toLower(args[i + 1]) == "to" && i + 2 < static_cast<int>(args.size())) {
            refName = args[i + 2]; relative = true; i += 2;
        } else if (w == "offset" && i + 1 < static_cast<int>(args.size())) {
            double v; if (evalPart(args[i + 1], v)) { offset = v; ++i; }
        } else if (w == "with" && i + 1 < static_cast<int>(args.size())
                   && toLower(args[i + 1]) == "rotation") {
            ++i;  // consume "rotation"
            // rotation may be a comma triple ("45,0,0") or space-separated
            // ("45 0 0"); each value is a number, formula or parameter.
            if (i + 1 < static_cast<int>(args.size()) && parseCoord3(args[i + 1], rx, ry, rz, params, namedPointValues())) {
                ++i;
            } else {
                double v;
                if (i + 1 < static_cast<int>(args.size()) && evalPart(args[i + 1], v)) { rx = v; ++i; }
                if (i + 1 < static_cast<int>(args.size()) && evalPart(args[i + 1], v)) { ry = v; ++i; }
                if (i + 1 < static_cast<int>(args.size()) && evalPart(args[i + 1], v)) { rz = v; ++i; }
            }
        } else if (w == "name" && i + 1 < static_cast<int>(args.size())) {
            name = args[i + 1]; ++i;
        } else if (parseCoord3(args[i], ox, oy, oz, params, namedPointValues())) {
            // a bare "x,y,z" is the origin
        } else if (name.empty() && w != "rotation") {
            name = args[i];  // a bare trailing token is taken as the name
        }
    }

    hobbycad::ConstructionPlaneSpec spec;
    spec.name = name;
    spec.originX = ox; spec.originY = oy; spec.originZ = oz;
    spec.offset = offset;
    spec.rotX = rx; spec.rotY = ry; spec.rotZ = rz;
    if (relative) {
        const auto* ref = hobbycad::findConstructionPlaneByName(p->constructionPlanes(),
                                                                refName);
        if (!ref) { return failure(subst("Unknown reference plane: %1", refName)); }
        spec.refPlaneId = ref->id;
    }
    const hobbycad::ConstructionPlaneData plane =
        hobbycad::makeConstructionPlane(spec, p->constructionPlanes().size());
    name = (plane.name);

    const int id = p->addConstructionPlane(plane);
    r.output = subst("Created plane '%1' (id %2)", name, id);
    return r;
}

CliResult CliEngine::cmdPlanes() const
{
    const hobbycad::Project* p = project();
    if (!p) return noDocument();

    CliResult r;
    const auto& planes = p->constructionPlanes();
    if (planes.empty()) {
        r.output = "No construction planes.";
        return r;
    }
    std::vector<std::string> lines;
    lines.push_back("id      name                center");
    for (const auto& pl : planes) {
        std::string center = "absolute";
        if (pl.centerRelative) {
            const ConstructionPlaneData* rp = pl.centerRefPlaneId >= 0 ? project()->constructionPlaneById(pl.centerRefPlaneId) : nullptr;
            center = subst("relative to %1", rp ? (rp->name) : "base plane");
        }
        lines.push_back(subst("%1  %2  %3", pad(numToString(pl.id), -6), pad((pl.name), -18), center));
    }
    r.output = join(lines, '\n');
    return r;
}

CliResult CliEngine::cmdCoords(const std::vector<std::string>& args)
{
    CliResult r;
    hobbycad::Project* p = project();
    if (!p) { return failure("No document is open."); }

    if (args.empty()) {                       // list
        const auto& nps = p->namedPoints();
        if (nps.empty()) { r.output = "No named coordinates."; return r; }
        std::vector<std::string> out;
        out.push_back("  origin = 0, 0, 0   (built-in)");
        for (const auto& np : nps) {
            out.push_back(subst("  %1 = %2, %3, %4", (np.name), (np.xExpr), (np.yExpr), (np.zExpr.empty() ? std::string("0") : np.zExpr)));
        }
        r.output = join(out, '\n');
        return r;
    }

    // define / update:  coords <name> <x,y[,z]>
    if (static_cast<int>(args.size()) < 2) {
        r.exitCode = 1;
        r.error = "Usage: coords <name> <x,y[,z]>   (each part a number, formula or parameter)";
        return r;
    }
    const std::string name = args[0];
    hobbycad::NamedPointData np;
    std::array<double, 3> v{};
    switch (hobbycad::defineNamedPoint(name, args[1],
                                       parameterValues(), np, &v)) {
    case hobbycad::NamedPointProblem::ReservedName:
        return failure(
            "'origin' is the built-in coordinate (0,0,0) and cannot be redefined.");
    case hobbycad::NamedPointProblem::BadComponentCount:
        return failure(subst("Invalid coordinate. Use: coords %1 x,y[,z]", name));
    case hobbycad::NamedPointProblem::DoesNotEvaluate:
        return failure(
            "Coordinate expression does not evaluate (unknown parameter?).");
    case hobbycad::NamedPointProblem::None:
        break;
    }

    p->addNamedPoint(np);
    r.output = subst("Named coordinate '%1' = %2, %3, %4", name, v[0], v[1], v[2]);
    return r;
}

bool CliEngine::measureReferenceParam(const std::string& source, double& value) const
{
    // The parsing of the source string and the distance math live once in the
    // library (sketch::measureReferenceSource); the CLI only supplies how an
    // (entityId, pointIndex) resolves to a solved point in the pending sketch,
    // so the GUI can share the same measurement with a resolver of its own.
    return hobbycad::sketch::measureReferenceSource(
        source,
        [this](int id, int idx, hobbycad::Point2D& out) -> bool {
            const hobbycad::SketchEntityData* ent = pendingEntity(id);
            if (!ent || idx < 0 || idx >= static_cast<int>(ent->points.size()))
                return false;
            out = ent->points[static_cast<size_t>(idx)];
            return true;
        },
        value);
}

void CliEngine::recomputeReferenceParameters()
{
    hobbycad::Project* p = project();
    if (!p) return;
    const auto before = p->parameters();
    bool anyRef = false;
    for (const auto& pd : before) if (pd.isReference) { anyRef = true; break; }
    if (!anyRef) return;

    hobbycad::ParameterEngine engine = hobbycad::parameterEngineFrom(before);
    for (const auto& pd : before) {
        if (!pd.isReference) continue;
        double v = 0.0;
        if (measureReferenceParam(pd.referenceSource, v)) engine.setReferenceValue(pd.name, v);
    }
    engine.evaluate();

    p->setParameters(hobbycad::parametersFromEngine(engine, before, p->activeDesignId()));
}

CliResult CliEngine::cmdParameters(const std::vector<std::string>& args)
{
    hobbycad::Project* p = project();
    if (!p) return noDocument();

    CliResult r;

    // "parameters" alone lists; "parameters <name> <expression>" sets.
    if (args.empty()) {
        const auto& params = p->parameters();
        if (params.empty()) {
            r.output = "No parameters.";
            return r;
        }
        std::vector<std::string> lines;
        lines.push_back("name            value       expression");
        for (const auto& param : params) {
            lines.push_back(subst("%1  %2  %3", pad((param.name), -14), pad(numToString(param.value), -10), (param.expression)));
        }
        r.output = join(lines, '\n');
        return r;
    }

    // "parameters reference|ref <name> distance <ptA> <ptB>": a measured,
    // read-only parameter whose value comes from the solved geometry and can be
    // used in other parameters' formulas. Value is (re)measured on each solve.
    if (equalsIgnoreCase(args[0], "reference") ||
        equalsIgnoreCase(args[0], "ref")) {
        if (static_cast<int>(args.size()) < 3) {
            r.exitCode = 1;
            r.error = 
                "Usage: parameters reference <name> distance <ptA> <ptB>\n"
                "  a point is <entityId> or <entityId>.<pointIndex>";
            return r;
        }
        const std::string rname = args[1];
        if (!hobbycad::ParameterEngine::isValidName(rname)) {
            r.exitCode = 1;
            r.error = subst("'%1' is not a valid parameter name.", args[1]);
            return r;
        }
        const std::string source = join(slice(args, 2), ' ');
        double measured = 0.0;
        const bool measuredOk = measureReferenceParam(source, measured);

        const std::vector<hobbycad::ParameterData> before = p->parameters();
        hobbycad::ParameterEngine engine = hobbycad::parameterEngineFrom(before);
        const bool updated = engine.hasParameter(rname);
        engine.setReferenceParameter(rname, source);
        if (measuredOk) engine.setReferenceValue(rname, measured);
        engine.evaluate();
        if (engine.hasCircularDependencies()) {
            r.exitCode = 1;
            r.error = "That would create a circular definition.";
            return r;
        }
        p->setParameters(hobbycad::parametersFromEngine(engine, before, p->activeDesignId()));
        r.output = subst("%1 reference %2 = %3   (%4%5)", updated ? "Updated" : "Added", args[1], measured, (source), measuredOk ? std::string() : "; not measured yet");
        return r;
    }

    if (static_cast<int>(args.size()) < 2) {
        r.exitCode = 1;
        r.error = "Usage: parameters [<name> <expression>]";
        return r;
    }

    const std::string name = args[0];
    const std::string expr = join(slice(args, 1), ' ');

    // A parameter name has to be a usable identifier: expressions resolve by
    // name. The GUI dialog already refuses bad names; enforce the same here so
    // the two front ends cannot diverge.
    if (!hobbycad::ParameterEngine::isValidName(name)) {
        r.exitCode = 1;
        r.error = subst(
            "'%1' is not a valid parameter name (letters, digits and "
            "underscore; must not start with a digit).", args[0]);
        return r;
    }

    // All parameter evaluation goes through ONE authority, the ParameterEngine:
    // it resolves in dependency order (so a change propagates to whatever uses
    // it) and refuses circular definitions. Load the current set, apply the
    // edit, and evaluate the whole thing.
    const auto paramsBefore = p->parameters();

    hobbycad::ParameterEngine engine;
    {
        std::vector<hobbycad::Parameter> cur;
        cur.reserve(paramsBefore.size());
        for (const auto& pd : paramsBefore) {
            hobbycad::Parameter pp;
            pp.name = pd.name;   pp.expression = pd.expression; pp.value = pd.value;
            pp.unit = pd.unit;   pp.comment = pd.comment;       pp.isUserParam = pd.isUserParam;
            pp.isReference = pd.isReference; pp.referenceSource = pd.referenceSource;
            cur.push_back(pp);
        }
        engine.setParameters(cur);
    }

    const bool updated = engine.hasParameter(name);
    // Preserve unit/comment on an edit; the command carries only name+expr.
    std::string keepUnit, keepComment;
    if (const hobbycad::Parameter* ex = engine.parameter(name)) {
        keepUnit = ex->unit; keepComment = ex->comment;
    }
    engine.setParameter(name, expr, keepUnit, keepComment);

    const hobbycad::EvaluationResult eval = engine.evaluate();

    if (engine.hasCircularDependencies()) {
        const auto chain = engine.circularDependencyChain();
        std::vector<std::string> parts;
        for (const auto& c : chain) parts.push_back((c));
        r.exitCode = 1;
        r.error = subst("That would create a circular definition: %1", join(parts, " -> "));
        return r;   // reject; the project is untouched
    }
    const hobbycad::Parameter* np = engine.parameter(name);
    if (!np || !np->isValid) {
        r.exitCode = 1;
        r.error = subst("Cannot evaluate '%1': %2", join(slice(args, 1), ' '), (np ? np->errorMessage
                                                     : std::string("unknown error")));
        return r;
    }
    (void)eval;

    // Write the evaluated set back to the project, preserving list order and
    // each parameter's design id; dependents carry their re-evaluated values.
    p->setParameters(hobbycad::parametersFromEngine(engine, paramsBefore, p->activeDesignId()));

    r.output = subst((updated ? "Updated %1 = %2."
                        : "Added %1 = %2."), args[0], np->value);

    recordParameterListChange(paramsBefore,
        updated ? subst("Edit parameter '%1'", args[0]): subst("Add parameter '%1'", args[0]));

    if (m_docHost) m_docHost->hostDocumentChanged();
    return r;
}

CliResult CliEngine::cmdInfo() const
{
    CliResult r;
    r.output = std::string("HobbyCAD ") +
               hobbycad::version() +
               "\nPhase 0: Foundation\n"
                              "Supported formats: BREP (.brep, .brp)";
    return r;
}

CliResult CliEngine::cmdHistory(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.empty()) {
        const auto& entries = m_history.entries();
        if (entries.empty()) {
            r.output = "History is empty.";
            return r;
        }

        std::string text;
        int width = numToString(entries.size()).length();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            text += "  " +
                    padLeft(numToString(i + 1), width, ' ') +
                    "  " + entries[i] +
                    "\n";
        }
        text += subst("(%1 of %2 max)", entries.size(), m_history.maxLines());
        r.output = text;
        return r;
    }

    std::string subcmd = toLower(args.front());

    if (subcmd == "clear") {
        m_history.clear();
        r.output = "History cleared.";
        return r;
    }

    if (subcmd == "max" && static_cast<int>(args.size()) >= 2) {
        bool ok = false;
        int newMax = toInt(args[1], &ok);
        if (!ok || newMax < 1) {
            r.exitCode = 1;
            r.error = "Error: max must be a positive integer.";
            return r;
        }
        m_history.setMaxLines(newMax);
        r.output = subst("History max set to %1 lines.", newMax);
        return r;
    }

    r.exitCode = 1;
    r.error = "Usage: history [clear | max <n>]";
    return r;
}

CliResult CliEngine::cmdSelect(const std::vector<std::string>& args)
{
    CliResult r;

    // Inside a sketch, selection means an entity. Entities have no names,
    // so this form takes an id.
    if (m_inSketchMode) {
        std::vector<std::string> a = args;
        if (!a.empty() && toLower(a[0]) == "entity") {
            a.erase(a.begin());
        }
        if (a.size() != 1) {
            return failure(
                "Usage: select [entity] <id>\n"
                "\n"
                "Inside a sketch, select picks an entity by id. Ids are "
                "shown as each entity is created.");
        }

        bool ok = false;
        const int wanted = toInt(a[0], &ok);
        if (!ok) {
            return failure(subst(
                "'%1' is not an entity id. Entities do not have names.", a[0]));
        }

        for (const auto& e : m_pendingSketch.entities) {
            if (e.id != wanted) continue;
            m_selectedEntityId = wanted;
            r.output = subst("Selected entity %1. "
                                      "Use \"deselect\" to clear.", wanted);
            return r;
        }

        r.exitCode = 1;
        r.error = subst("No entity with id %1 in this sketch.", wanted);
        return r;
    }

    if (static_cast<int>(args.size()) < 2) {
        r.exitCode = 1;
        r.error = 
            "Usage: select <type> <name>\n"
            "\n"
            "Types: sketch, body, plane\n"
            "\n"
            "Examples:\n"
            "  select sketch Sketch1\n"
            "  select body Body1\n"
            "  select plane \"Top offset\"";
        return r;
    }

    std::string type = toLower(args[0]);
    std::string name = join(slice(args, 1), " ");

    // The types this command can actually resolve. "plane" was missing from
    // this list while the lookup below handled it, so every "select plane"
    // was rejected here and that branch was unreachable.
    static const std::vector<std::string> kSelectable = {
        "sketch",
        "body",
        "plane",
    };

    // Recognized, but there is nothing to select yet. Worth distinguishing
    // from a typo: "not supported yet" and "no such type" send a reader to
    // very different places.
    static const std::vector<std::string> kPlanned = {
        "face",
        "edge",
        "vertex",
    };

    if (contains(kPlanned, type)) {
        r.exitCode = 1;
        r.error = subst(
            "Selecting a %1 is not supported yet. "
            "Selectable types: sketch, body, plane.", type);
        return r;
    }

    if (!contains(kSelectable, type)) {
        r.exitCode = 1;
        r.error = "Unknown type: " + type +
                  "\nValid types: sketch, body, plane";
        return r;
    }

    const hobbycad::Project* proj = project();
    if (!proj) return noDocument();

    // Look it up for real (this used to acknowledge any name at all). The
    // same resolver as delete and rename, so "select sketch id=3" and a bare
    // id work here too.
    hobbycad::ObjectKind kind = hobbycad::ObjectKind::Sketch;
    hobbycad::parseObjectKind(type, kind);   // kSelectable guarantees it parses
    std::string why;
    const int at = resolveObjectIndex(static_cast<int>(kind), name, &why);
    if (at < 0) return failure(why);
    const CliContext::Kind contextKind =
        kind == hobbycad::ObjectKind::Sketch ? CliContext::Kind::Sketch
        : kind == hobbycad::ObjectKind::Body ? CliContext::Kind::Body
                                             : CliContext::Kind::Plane;
    const CliContext found{contextKind, hobbycad::objectIdAt(*proj, kind, at),
                           (hobbycad::objectNameAt(*proj, kind, at))};
    name = found.name;

    m_context = found;
    r.output = subst("Selected %1 '%2' (id %3). "
                              "Use \"deselect\" to leave.", type, name, found.id);
    return r;
}

// "<entity>" or "<entity>.<point>" for each operand of a constraint, the way
// "constrain" reads them back.
static std::vector<std::string> constraintRefs(const hobbycad::ConstraintData& c)
{
    std::vector<std::string> refs;
    for (size_t i = 0; i < c.entityIds.size(); ++i) {
        refs.push_back((i < c.pointIndices.size()
                     ? subst("%1.%2", c.entityIds[i], c.pointIndices[i]): numToString(c.entityIds[i])));
    }
    return refs;
}

std::vector<std::string> CliEngine::printSketch(const hobbycad::SketchData& sketch,
                                   int precision) const
{
    // The script form is the library's (sketch::sketchToScript): what
    // replays how, and the order that lets ids resolve. Only the string
    // type is the CLI's.
    std::vector<std::string> out;
    for (const std::string& line : sketch::sketchToScript(
             sketch.name, sketch.entities, sketch.constraints, sketch.groups, precision)) {
        out.push_back((line));
    }
    return out;
}

std::vector<std::string> CliEngine::describeEntities(const hobbycad::SketchData& sk) const
{
    const hobbycad::LengthUnit unit = hobbycad::LengthUnit::Millimeters;
    auto len = [unit](double mm) {
        std::string t = (
            hobbycad::formatValueWithUnit(mm, unit));
        // "-0 mm" reads as a different number from "0 mm". It is not
        // negative zero that causes it but a tiny negative that ROUNDS to
        // zero at display precision, so the check has to be on the
        // formatted text rather than on the value.
        if (startsWith(t, '-')) {
            const std::string rest = t.substr(1);
            // What the "^0(\\.0*)?(\\s|$)" pattern matched: a zero, an
            // optional run of zero decimals, then the end or a space.
            size_t k = 0;
            bool allZero = k < rest.size() && rest[k] == '0';
            if (allZero) {
                ++k;
                if (k < rest.size() && rest[k] == '.') {
                    ++k;
                    while (k < rest.size() && rest[k] == '0') ++k;
                }
                allZero = (k == rest.size())
                          || std::isspace(static_cast<unsigned char>(rest[k]));
            }
            if (allZero) t = rest;
        }
        return t;
    };

    std::vector<std::string> out;
    out.push_back(subst("  %1 entity/entities, %2 constraint(s)", sk.entities.size(), sk.constraints.size()));

    // A point, formatted for display.
    auto pt = [&len](const hobbycad::Point2D& p) {
        return subst("%1, %2", len(p.x), len(p.y));
    };
    // Point n if the entity has it, else empty.
    auto ptAt = [&pt](const hobbycad::SketchEntityData& e, size_t i) {
        return i < e.points.size() ? pt(e.points[i]) : std::string();
    };

    for (const auto& e : sk.entities) {
        // Show what DEFINES the entity, which for most types is more than
        // one number. This used to append "at <first point>" to everything,
        // so a line showed where it started and nothing about where it went.
        // That was useless for reading the result of a solve, which is the main
        // reason to look.
        std::string desc;
        switch (e.type) {
        case sketch::EntityType::Line:
            desc = subst("line       %1  ->  %2", ptAt(e, 0), ptAt(e, 1));
            break;
        case sketch::EntityType::Rectangle:
            desc = subst("rectangle  %1  ->  %2", ptAt(e, 0), ptAt(e, 1));
            break;
        case sketch::EntityType::Circle:
            desc = subst("circle     at %1  r=%2", ptAt(e, 0), len(e.radius));
            break;
        case sketch::EntityType::Arc:
            desc = subst("arc        at %1  r=%2  %3%5 to %4%5", ptAt(e, 0), len(e.radius), e.startAngle, e.startAngle + e.sweepAngle, ("\xc2\xb0"));
            break;
        case sketch::EntityType::Point:
            desc = subst("point      at %1", ptAt(e, 0));
            break;
        case sketch::EntityType::Polygon:
            desc = subst("polygon    at %1  r=%2  %3 sides", ptAt(e, 0), len(e.radius), e.sides);
            break;
        case sketch::EntityType::Ellipse:
            desc = subst("ellipse    at %1  %2 x %3", ptAt(e, 0), len(e.majorRadius), len(e.minorRadius));
            break;
        case sketch::EntityType::Slot:
            // Two points is a linear slot, three an arc one, and showing
            // an arc slot in the linear layout hid its third point and its
            // direction entirely. Width, not the stored half-width: the
            // command takes a width, so reporting a radius means the
            // number that comes back is not the number that went in.
            if (static_cast<int>(e.points.size()) >= 3) {
                const double pathR = geometry::lineLength(e.points[0], e.points[1]);
                desc = subst("arc slot   at %1  r=%2  %3  ->  %4  width=%5%6", ptAt(e, 0), len(pathR), ptAt(e, 1), ptAt(e, 2), len(e.radius * 2.0), e.arcFlipped ? "  [long way]"
                                             : std::string());
            } else {
                desc = subst("slot       %1  ->  %2  width=%3", ptAt(e, 0), ptAt(e, 1), len(e.radius * 2.0));
            }
            break;
        case sketch::EntityType::Spline: {
            hobbycad::Point2D apex;
            if (e.conicRho > 0.0 && sketch::conicApex(e, apex)) {
                desc = subst("conic      %1 to %2  apex %3  rho %4 (%5)", ptAt(e, 0),
                             ptAt(e, e.points.size() - 1), pt(apex), e.conicRho,
                             translate("QObject", sketch::conicKindName(e.conicRho)));
                break;
            }
            std::vector<std::string> pts;
            for (const auto& p : e.points) pts.push_back(pt(p));
            desc = subst("%1 %2", e.splineBezier ? "bezier    "
                                           : "spline    ", join(pts, "  "));
            break;
        }
        case sketch::EntityType::Text:
            desc = subst("text       at %1  \"%2\"", ptAt(e, 0), (e.text));
            break;
        case sketch::EntityType::Parallelogram: {
            std::vector<std::string> pts;
            for (const auto& p : e.points) pts.push_back(pt(p));
            desc = subst("parallelogram  %1", join(pts, "  "));
            break;
        }
        case sketch::EntityType::Dimension:
            desc = subst("dimension  at %1", ptAt(e, 0));
            break;
        }
        // Every type is named above and the switch has no default, so a new
        // EntityType is a compiler warning here rather than silently
        // printing as "entity", which is what the five newest types did.

        if (e.isConstruction) desc += "  [construction]";

        // The entity's OWN id, not its position in the list. "delete 3" and
        // "select 3" take an id, so a position here would name a different
        // entity the moment one was deleted.
        out.push_back(subst("  %1  %2", pad(numToString(e.id), 3), desc));
    }

    for (const auto& c : sk.constraints) {
        const std::vector<std::string> ids = constraintRefs(c);
        std::string line = subst("%1 on %2", constraintName(c.type), join(ids, " and "));
        if (sketch::isDimensionalConstraint(c.type)) {
            line += subst(" = %1", sketch::isAngularConstraint(c.type)
                    ? subst("%1%2", c.value,
                          (sketch::constraintUnit(c.type)))
                    : (hobbycad::formatValueWithUnit(
                          c.value, hobbycad::LengthUnit::Millimeters)));
        }
        if (!c.isDriving) line += "  [reference]";
        // "c" prefixes the id because entities and constraints number
        // separately: without it, "3" here and "3" above look like the
        // same thing.
        out.push_back(subst("  c%1  %2", pad(numToString(c.id), 2), line));
    }
    return out;
}

CliResult CliEngine::cmdPrint(const std::vector<std::string>& args) const
{
    (void)args;

    // Editing a sketch: print THAT, not a document summary that does not
    // yet contain it. It previously reported "sketches 0" while a sketch
    // with geometry in it was open.
    if (m_inSketchMode) {
        CliResult r;
        std::vector<std::string> out;
        out.push_back(subst("sketch %1  (editing, not yet saved)", m_currentSketchName));
        { const auto rows = describeEntities(m_pendingSketch);
        out.insert(out.end(), rows.begin(), rows.end()); }
        if (m_selectedEntityId > 0) {
            out.push_back(std::string());
            out.push_back(subst("selected: entity %1", m_selectedEntityId));
        }
        out.push_back(std::string());
        out.push_back("Use \"finish\" to save it, or \"discard\" to "
                              "throw it away.");
        r.paginate = true;
        r.output = join(out, '\n');
        return r;
    }

    const hobbycad::Project* proj = project();
    if (!proj) return noDocument();

    CliResult r;
    std::vector<std::string> out;

    if (m_context.kind == CliContext::Kind::Sketch) {
        for (const auto& sk : proj->sketches()) {
            if (sk.id != m_context.id) continue;
            out.push_back(subst("sketch %1  (id %2)", (sk.name), sk.id));
            { const auto rows = describeEntities(sk);
        out.insert(out.end(), rows.begin(), rows.end()); }
            r.paginate = true;
            r.output = join(out, '\n');
            return r;
        }
        r.exitCode = 1;
        r.error = "The selected sketch no longer exists.";
        return r;
    }

    if (m_context.isSet()) {
        out.push_back(subst("%1 %2  (id %3)", m_context.kindName(), m_context.name, m_context.id));
        r.output = join(out, '\n');
        return r;
    }

    r.paginate = true;

    // No context: summarize the document.
    out.push_back(subst("project   %1", proj->name().empty() ? "(unnamed)"
                                         : (proj->name())));
    out.push_back(subst("designs   %1", proj->designs().size()));
    out.push_back(subst("sketches  %1", proj->sketches().size()));
    out.push_back(subst("bodies    %1", proj->bodies().size()));
    out.push_back(subst("planes    %1", proj->constructionPlanes().size()));
    out.push_back(subst("params    %1", proj->parameters().size()));
    out.push_back(std::string());
    out.push_back("Use \"select <type> <name>\" to look inside one, "
                          "or \"export\" for a script.");
    r.output = join(out, '\n');
    return r;
}

CliResult CliEngine::cmdExport(const std::vector<std::string>& args) const
{
    const hobbycad::Project* proj = project();
    if (!proj) return noDocument();

    CliResult r;
    std::vector<std::string> out;

    // file=<path> writes the script instead of printing it. RouterOS spells
    // it the same way. Nothing is written until the whole script has been
    // built, so a failure part way through leaves no half-file behind.
    std::string filePath;
    for (const std::string& a : args) {
        if (startsWith(a, "file=")) {
            filePath = a.substr(5);
            if (filePath.empty()) {
                return failure("file= needs a path.");
            }
        }
    }

    // Always full precision, with no option to lower it.
    //
    // Aaron, 2026-08-27: *"this is where I see export and print diverge.
    // export is always full precision because it is making a script."*
    //
    // That is the cleaner split: the precision difference IS the difference
    // between the two commands, rather than a flag on one of them. A script
    // has to reproduce the model, so a rounded export has no legitimate use,
    // and an option nobody should choose is better not offered.
    const int precision = hobbycad::StoragePrecision;

    // Scope: the selection if there is one, the whole document otherwise.
    // Aaron: "the 'print' command being context based too if the user wants
    // only a script for the current selected object or sketch."
    const bool wholeDoc = contains(args, "all") || !m_context.isSet();

    out.push_back("# HobbyCAD script");
    out.push_back(subst("# project: %1", (proj->name())));
    out.push_back((wholeDoc
                ? "# scope  : whole document"
                : subst("# scope  : %1 '%2'", m_context.kindName(), m_context.name)));
    out.push_back("# replay : hobbycad --no-gui, then \"script <file>\"");

    out.push_back("# values : full precision (exact)");
    out.push_back(std::string());

    if (wholeDoc && !proj->parameters().empty()) {
        for (const auto& p : proj->parameters()) {
            out.push_back(subst("parameters %1 %2", (p.name), (p.expression)));
        }
        out.push_back(std::string());
    }

    int printed = 0;
    for (const auto& sk : proj->sketches()) {
        if (!wholeDoc && !(m_context.kind == CliContext::Kind::Sketch
                           && sk.id == m_context.id)) {
            continue;
        }
        { const auto rows = printSketch(sk, precision);
        out.insert(out.end(), rows.begin(), rows.end()); }
        out.push_back(std::string());
        ++printed;
    }

    if (!wholeDoc && m_context.kind != CliContext::Kind::Sketch) {
        out.push_back("# Nothing to emit: only sketches can be printed "
                              "as commands so far.");
    } else if (printed == 0 && wholeDoc) {
        out.push_back("# (no sketches)");
    }

    if (wholeDoc && !proj->bodies().empty()) {
        out.push_back(subst("# %1 body/bodies exist but are NOT in this "
                              "script: there are no 3D CLI commands yet, so "
                              "extrude and revolve cannot be replayed.", proj->bodies().size()));
    }

    const std::string script = join(out, '\n') + '\n';

    if (filePath.empty()) {
        r.output = script;
        return r;
    }

    std::ofstream f(filePath, std::ios::trunc);
    if (!f) {
        r.exitCode = 1;
        r.error = subst("Could not write %1: %2", filePath,
                        std::string(std::strerror(errno)));
        return r;
    }
    f << script;
    f.close();
    if (!f) {
        r.exitCode = 1;
        r.error = subst("Could not write all of %1.", filePath);
        return r;
    }

    r.output = subst("Wrote %1 line(s) to %2.", out.size(), filePath);
    return r;
}

CliResult CliEngine::cmdDeselect()
{
    CliResult r;

    // Inside a sketch the selection is an entity, so that is what clears.
    if (m_inSketchMode) {
        if (m_selectedEntityId <= 0) {
            r.output = "No entity is selected.";
            return r;
        }
        r.output = subst("Deselected entity %1.", m_selectedEntityId);
        m_selectedEntityId = -1;
        return r;
    }

    if (!m_context.isSet()) {
        r.output = "Nothing is selected.";
        return r;
    }
    r.output = subst("Deselected %1 '%2'.", m_context.kindName(), m_context.name);
    m_context = CliContext{};
    return r;
}

/// Resolve a name-or-id token to a POSITION in the project's vector.
///
/// Position, not id, because Project's mutation API is index-based
/// (removeSketch(int index), setSketch(int index, ...)). Resolving at the
/// moment of use is what keeps that from becoming the positional-identity
/// defect all over again: nothing holds an index across an edit.
///
/// A bare token is matched by NAME first, then by id. That order matters
/// because a sketch may legitimately be named "12". When a name match and a
/// different object's id both fit, the command refuses rather than guessing;
/// "id=12" says which was meant.
int CliEngine::resolveObjectIndex(int kindValue,
                                  const std::string& token,
                                  std::string* error) const
{
    const auto kind = static_cast<hobbycad::ObjectKind>(kindValue);
    const hobbycad::Project* proj = project();
    if (!proj) { *error = "No document is open."; return -1; }

    // The rule (name first, then id; "id=<n>" explicit; ambiguity refused)
    // is the library's; the wording is this prompt's.
    const hobbycad::ObjectRef ref = hobbycad::resolveObjectRef(*proj, kind, token);
    const std::string kindName = std::string(hobbycad::objectKindName(kind));
    switch (ref.problem) {
    case hobbycad::ObjectRefProblem::NoneOfKind:
        *error = subst("There are no %1s in this document.", kindName);
        return -1;
    case hobbycad::ObjectRefProblem::BadIdNumber:
        *error = subst("'%1' is not a number.", token.substr(3));
        return -1;
    case hobbycad::ObjectRefProblem::NoSuchId:
        *error = subst("No %1 with id %2.", kindName, ref.id);
        return -1;
    case hobbycad::ObjectRefProblem::Ambiguous:
        *error = subst(
            "'%1' is both the name of one %2 and the id of another. "
            "Use \"id=%3\" for the one with that id.", token, kindName, ref.id);
        return -1;
    case hobbycad::ObjectRefProblem::NoSuchName:
        *error = subst("No %1 called '%2'.", kindName, token);
        return -1;
    case hobbycad::ObjectRefProblem::None:
        break;
    }
    return ref.index;
}

void CliEngine::recordBodyListChange(
    const std::vector<hobbycad::BodyData>& before, const std::string& description)
{
    if (!m_undoHost) return;
    const hobbycad::Project* p = project();
    if (!p) return;
    m_undoHost->pushDocumentCommand(
        hobbycad::makeBodyListCommand(
            before, p->bodies(), description));
}

void CliEngine::recordPlaneListChange(
    const std::vector<hobbycad::ConstructionPlaneData>& before,
    const std::string& description)
{
    if (!m_undoHost) return;
    const hobbycad::Project* p = project();
    if (!p) return;
    m_undoHost->pushDocumentCommand(
        hobbycad::makePlaneListCommand(
            before, p->constructionPlanes(), description));
}

void CliEngine::recordParameterListChange(
    const std::vector<hobbycad::ParameterData>& before,
    const std::string& description)
{
    if (!m_undoHost) return;
    const hobbycad::Project* p = project();
    if (!p) return;
    m_undoHost->pushDocumentCommand(
        hobbycad::makeParameterListCommand(
            before, p->parameters(), description));
}

// Defined further down, next to the geometry commands that share it.
static bool parseValue(const std::string& str, double& value, std::string& expr,
                       const std::map<std::string, double>& params);

namespace {

/// The usage text for "constrain", listing every type from the enum so it
/// cannot fall behind it.
std::string constrainUsageText()
{
    std::vector<std::string> geometric, dimensional;
    for (auto t : hobbycad::sketch::allConstraintTypes()) {
        std::string name = 
            removeAll(toLower(hobbycad::sketch::constraintTypeName(t)), ' ');
        (hobbycad::sketch::isDimensionalConstraint(t) ? dimensional : geometric)
            .push_back(name);
    }
    return subst(
        "Usage: constrain <type> <entity id>... [<value>] [reference]\n"
        "\n"
        "Geometric (no value):\n  %1\n"
        "\n"
        "Dimensional (take a value):\n  %2\n"
        "\n"
        "Examples:\n"
        "  constrain horizontal 1\n"
        "  constrain parallel 1 2\n"
        "  constrain midpoint 2.0 1   (point 0 of entity 2 at the middle of line 1)\n"
        "  constrain radius 3 25mm\n"
        "  constrain distance 1 2 (width/2)\n"
        "  constrain radius 3 25mm reference\n"
        "\n"
        "\"reference\" records the value as a measurement instead of "
        "driving the geometry.", join(geometric, ", "), join(dimensional, ", "));
}

}  // namespace

std::string CliEngine::constrainUsage() const { return constrainUsageText(); }

/// Parse a constraint's value, honoring and CHECKING its unit.
///
/// Angles and lengths are both "a number with a unit", and nothing else
/// stops "constrain angle 1 2 45mm" from storing 45 degrees or
/// "constrain radius 1 25deg" from storing 25 millimeters. Both are
/// mistakes that produce a wrong drawing rather than an error, so the unit
/// is checked against what the constraint actually measures.
///
/// Parameters and expressions fall through to the ordinary value parser;
/// they carry no suffix to check.
///
/// @return false with `error` set.
static bool parseConstraintValue(const std::string& token,
                                 hobbycad::sketch::ConstraintType type,
                                 const std::map<std::string, double>& params,
                                 double* value, std::string* text, std::string* error)
{
    namespace sk = hobbycad::sketch;
    double v = 0.0;
    std::string t, badPart;
    switch (sk::resolveConstraintValue(token, type, params, v, t, &badPart)) {
    case sk::ConstraintValueProblem::AngleForLength:
        *error = subst("'%1' is an angle, but %2 measures a length.", token, constraintName(type));
        return false;
    case sk::ConstraintValueProblem::LengthForAngle:
        *error = subst(
            "'%1' is a length, but %2 measures an angle. Use degrees, "
            "for example \"45\" or \"45deg\".", token, constraintName(type));
        return false;
    case sk::ConstraintValueProblem::NotANumber:
        *error = subst("'%1' is not a number.", (badPart));
        return false;
    case sk::ConstraintValueProblem::Invalid:
        *error = subst(
            "Invalid value '%1'. Use a number, a parameter, or "
            "(an expression).", token);
        return false;
    case sk::ConstraintValueProblem::None:
        break;
    }
    *value = v;
    *text = (t);
    return true;
}

const hobbycad::SketchEntityData* CliEngine::pendingEntity(int id) const
{
    return sketch::findEntityById(m_pendingSketch.entities, id);
}

const hobbycad::SketchData* CliEngine::currentSketchForReading() const
{
    // The sketch being edited wins over a selected one: it is where any
    // "constrain" just landed, and reporting the saved copy instead would
    // show a person the state before their own last command.
    if (m_inSketchMode) return &m_pendingSketch;

    if (m_context.kind == CliContext::Kind::Sketch) {
        const hobbycad::Project* p = project();
        if (!p) return nullptr;
        for (const auto& sk : p->sketches()) {
            if (sk.id == m_context.id) return &sk;
        }
    }
    return nullptr;
}

int CliEngine::nextPendingConstraintId() const
{
    return sketch::nextFreeConstraintId(m_pendingSketch.constraints);
}

int CliEngine::addPendingConstraint(hobbycad::ConstraintData c)
{
    if (c.id <= 0) c.id = nextPendingConstraintId();
    const int id = c.id;
    m_pendingSketch.constraints.push_back(std::move(c));
    return id;
}

int CliEngine::nextPendingGroupId() const
{
    return sketch::nextFreeGroupId(m_pendingSketch.groups);
}

std::string CliEngine::sweepUsage() const
{
    return 
        "Usage: sweep [along] <entity id> width <w> [flat|round]\n"
        "\n"
        "Examples:\n"
        "  sweep along 1 width 10\n"
        "  sweep 1 width 10 flat\n"
        "\n"
        "Sweeps a line or arc into a slot and DECOMPOSES it: two sides,\n"
        "the end caps, and the constraints that hold them together, all in\n"
        "one group. The path becomes the construction centerline.\n"
        "\n"
        "Ends are round by default. \"flat\" squares them off.\n"
        "\n"
        "Both styles are built from two half-width segments per end, which\n"
        "is what keeps each end centered on the path and lets one Equal\n"
        "chain state the width across every end at once.";
}

CliResult CliEngine::cmdPoints(std::vector<std::string> args)
{
    CliResult r;
    if (args.size() != 1) {
        return failure(
            "Usage: points <entityId>\n"
            "  List an entity's points and their <id>.<index> reference names.");
    }
    bool ok = false;
    const int id = toInt(args[0], &ok);
    if (!ok) {
        return failure(subst("Entity id must be a number: '%1'", args[0]));
    }
    const hobbycad::SketchEntityData* e = pendingEntity(id);
    if (!e) {
        return failure(subst("No entity %1 in the current sketch.", id));
    }
    // A conic authored by rho reads as what it is: ends, apex and rho (its
    // stored property), with the edit form beside it. The inner control
    // points are derived from those and are not shown as handles.
    if (e->type == sketch::EntityType::Spline && e->conicRho > 0.0) {
        hobbycad::Point2D apex;
        if (sketch::conicApex(*e, apex)) {
            std::vector<std::string> cl;
            cl.push_back(subst(translate("QObject", "entity %1 (conic arc, %2, rho %3):"), id,
                               translate("QObject", sketch::conicKindName(e->conicRho)),
                               e->conicRho));
            cl.push_back(subst(translate("QObject", "  start (%1, %2)"),
                               e->points.front().x, e->points.front().y));
            cl.push_back(subst(translate("QObject", "  end   (%1, %2)"),
                               e->points.back().x, e->points.back().y));
            cl.push_back(subst(translate("QObject", "  apex  (%1, %2)   (where the end tangents "
                                                    "meet; not a stored point)"),
                               apex.x, apex.y));
            cl.push_back(subst(translate("QObject", "Change rho: conic %1 rho <r>. Editing a "
                                                    "handle makes it a plain bezier."),
                               id));
            r.exitCode = 0;
            r.output = join(cl, '\n');
            return r;
        }
    }
    // A Bezier spline reads best as anchors + handles (angle/length), the same
    // form the `bezier` create/edit grammar uses, not a raw control-point dump.
    if (e->type == sketch::EntityType::Spline && e->splineBezier) {
        std::vector<hobbycad::Point2D> poly(e->points.begin(), e->points.end());
        const auto anchors = sketch::bezierAnchorsFromControlPolygon(poly);
        if (!anchors.empty()) {
            const int N = static_cast<int>(anchors.size()) - 1;
            std::vector<std::string> bl;
            bl.push_back(subst("entity %1 (bezier, %2 segment%3):", id, N, N == 1 ? std::string() : "s"));
            auto fmtHandle = [](const char* kind, const hobbycad::sketch::BezierAnchor& a,
                                sketch::BezierHandleSide side) -> std::string {
                double ang = 0.0, len = 0.0;
                if (!sketch::anchorHandlePolar(a, side, ang, len)) return std::string();  // coincident: a corner
                return subst(" %1 %2deg %3", std::string(kind), numToStringFixed(normalizeAngle360(ang), 1), numToStringFixed(len, 3));
            };
            for (int k = 0; k < static_cast<int>(anchors.size()); ++k) {
                const auto& a = anchors[k];
                std::string row = subst("  P%1 (%2, %3)", k, a.pos.x, a.pos.y);
                row += fmtHandle("in",  a, sketch::BezierHandleSide::In);
                row += fmtHandle("out", a, sketch::BezierHandleSide::Out);
                if (a.hasIn && a.hasOut) {                       // classify interior anchors
                    switch (sketch::anchorContinuity(a)) {
                    case sketch::AnchorContinuity::Smooth:     row += "   [smooth]"; break;
                    case sketch::AnchorContinuity::Asymmetric: row += "   [asymmetric]"; break;
                    case sketch::AnchorContinuity::Corner:     row += "   [corner]"; break;
                    }
                }
                bl.push_back(row);
            }
            bl.push_back(subst("Edit a handle: bezier %1 handle <anchor> in|out|tan <ang> <len>.", id));
            r.exitCode = 0;
            r.output = join(bl, '\n');
            return r;
        }
    }

    std::vector<std::string> lines;
    lines.push_back(subst("entity %1 has %2 point(s):", id, static_cast<int>(e->points.size())));
    for (int i = 0; i < static_cast<int>(e->points.size()); ++i) {
        const auto& p = e->points[i];
        const std::string coord = (p.z != 0.0)
            ? subst("(%1, %2, %3)", p.x, p.y, p.z): subst("(%1, %2)", p.x, p.y);
        lines.push_back(subst("  %1.%2 = %3", id, i, coord));
    }
    lines.push_back(subst("Reference a point as <id>.<index> "
                            "(e.g. constrain coincident %1.0 %1.1).", id));
    r.exitCode = 0;
    r.output = join(lines, '\n');
    return r;
}

CliResult CliEngine::cmdProject(std::vector<std::string> args)
{
    CliResult r;
    if (args.size() != 2) {
        return failure(
            "Usage: projection <sourceSketch> <entityId>\n"
            "  Bring an entity from another sketch into this one as reference\n"
            "  (a projection: driven by its source, zero degrees of freedom).\n"
            "  <sourceSketch> is a sketch name or id=<n>.");
    }
    auto* proj = project();
    if (!proj) { return failure("No document open."); }

    // Resolve the source sketch by name or id=<n>.
    const std::string ref = args[0];
    const hobbycad::SketchData* srcSketch = nullptr;
    if (startsWith(ref, "id=")) {
        bool ok = false; const int wantId = toInt(ref.substr(3), &ok);
        if (ok) for (const auto& sk : proj->sketches())
            if (sk.id == wantId) { srcSketch = &sk; break; }
    } else {
        for (const auto& sk : proj->sketches())
            if ((sk.name) == ref) { srcSketch = &sk; break; }
    }
    if (!srcSketch) {
        return failure(subst("No sketch '%1' in this document.", ref));
    }

    // Resolve the source entity id within that sketch.
    bool ok = false; const int entId = toInt(args[1], &ok);
    if (!ok) {
        return failure(subst("Entity id must be a number: '%1'", args[1]));
    }
    const sketch::Entity* source = nullptr;
    source = sketch::findEntityById(srcSketch->entities, entId);
    if (!source) {
        return failure(subst("Sketch '%1' has no entity %2.", (srcSketch->name), entId));
    }

    // Project the source (through its plane) onto this sketch's plane.
    const hobbycad::PlaneBasis srcBasis = proj
        ? hobbycad::planeBasisFor(*srcSketch, *proj)
        : hobbycad::planeBasisFor(srcSketch->plane, srcSketch->planeOffset);
    const hobbycad::PlaneBasis tgtBasis = proj
        ? hobbycad::planeBasisFor(m_pendingSketch, *proj)
        : hobbycad::planeBasisFor(m_pendingSketch.plane, m_pendingSketch.planeOffset);

    const int newId = sketch::nextFreeEntityId(m_pendingSketch.entities);

    sketch::Entity child;
    if (!sketch::makeProjectionChild(child, *source, srcSketch->id, newId,
                                     srcBasis, tgtBasis)) {
        return failure(subst(
            "Entity %1 of '%2' cannot be projected yet (circles and arcs "
            "project to ellipses, which is not supported).", entId, (srcSketch->name)));
    }
    m_pendingSketch.entities.push_back(child);

    r.exitCode = 0;
    r.output = subst("Projected %1.%2 into '%3' as entity %4 (reference).", (srcSketch->name), entId, m_currentSketchName, newId);
    return r;
}

CliResult CliEngine::cmdSweep(std::vector<std::string> args)
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure(
            "'sweep' works inside a sketch, and no sketch is open.");
    }

    if (args.empty()) {
        return failure(sweepUsage());
    }

    if (toLower(args[0]) == "along") args.erase(args.begin());

    if (args.empty()) {
        return failure(sweepUsage());
    }

    bool ok = false;
    const int pathId = toInt(args[0], &ok);
    if (!ok) {
        return failure(subst(
            "'%1' is not an entity id. \"sweep\" takes the id of the line or "
            "arc to sweep along.", args[0]));
    }
    args.erase(args.begin());

    const hobbycad::SketchEntityData* src = pendingEntity(pathId);
    if (!src) {
        return failure(subst("No entity with id %1 in this sketch.", pathId));
    }
    if (src->type != sketch::EntityType::Line && src->type != sketch::EntityType::Arc) {
        r.exitCode = 1;
        r.error = subst(
            "Entity %1 is not a line or an arc. A sweep follows one or the "
            "other; longer paths need the joins trimmed and are not handled "
            "yet.", pathId);
        return r;
    }

    if (args.empty() ||
        (toLower(args[0]) != "width" &&
         toLower(args[0]) != "thickness")) {
        return failure("Expected 'width' (or 'thickness') and a value.");
    }
    args.erase(args.begin());

    double width = 0.0;
    std::string widthExpr;
    if (args.empty() ||
        !parseValue(args[0], width, widthExpr, parameterValues())) {
        return failure("Invalid width.");
    }
    args.erase(args.begin());

    // Round is the default; flat is the checkbox.
    sketch::SweepEndStyle ends = sketch::SweepEndStyle::Round;
    while (!args.empty()) {
        const std::string w = toLower(args[0]);
        if (w == "flat") {
            ends = sketch::SweepEndStyle::Flat;
        } else if (w == "round" || w == "curved") {
            ends = sketch::SweepEndStyle::Round;
        } else {
            return failure(subst(
                "Unexpected '%1'. Ends are 'flat' or 'round'.", args[0]));
        }
        args.erase(args.begin());
    }

    if (!sketch::slotWidthIsPositive(width)) {   // zero at the length precision
        return failure("Width must be greater than zero.");
    }
    const double halfWidth = width / 2.0;

    // Equality is the limit case and is allowed (the inner side becomes the
    // center); see the typed slot forms and decomposeSweep.
    if (src->type == sketch::EntityType::Arc && halfWidth > src->radius) {
        return failure(subst(
            "A width of %1 does not fit on an arc of radius %2: the inner "
            "edge would pass through the center. Width can be at most %3.", width, src->radius, src->radius * 2.0));
    }

    // The commit (pieces, constraints, the path turned construction, the
    // group and its back-links) is the library's, shared with the canvas;
    // the ids come from this sketch's own sequences.
    int nextE = sketch::nextFreeEntityId(m_pendingSketch.entities);
    int nextC = nextPendingConstraintId();
    const sketch::SweepApplied result = sketch::applySweep(
        m_pendingSketch.entities, m_pendingSketch.constraints, m_pendingSketch.groups,
        pathId, halfWidth, ends,
        [&nextE] { return nextE++; },
        [&nextC] { return nextC++; },
        nextPendingGroupId());
    if (!result.success) {
        return failure("That sweep could not be built.");
    }
    const hobbycad::sketch::Group& g = result.group;

    int constructionCount = 0;
    for (const auto& e : result.entities) if (e.isConstruction) ++constructionCount;

    r.output = subst(
        "Swept entity %1 into %2 entities and %3 constraints, %4 ends, "
        "width %5", pathId, static_cast<int>(result.entities.size()), static_cast<int>(result.constraints.size()), ends == sketch::SweepEndStyle::Flat ? "flat"
                                                 : "round", widthExpr);
    r.output += subst("\n(grouped as \"%1\" [group %2]; entity %3 is "
                               "now the construction centerline)", (g.name), g.id, pathId);
    if (constructionCount > 0) {
        r.output += subst(
            "\n(%1 of the new entities are construction: the half-width "
            "guides that carry the Equal chain)", constructionCount);
    }
    return r;
}

CliResult CliEngine::cmdGroup(std::vector<std::string> args)
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure(
            "'group' works inside a sketch, and no sketch is open.");
    }

    if (args.empty()) {
        return failure(
            "Usage: group <name> [id=<n>] [entities <ids>] "
            "[constraints <ids>] [groups <ids>] [locked] [sweep]\n"
            "\n"
            "Examples:\n"
            "  group \"Sweep Angle 1\" entities 2,3 constraints 1 sweep\n"
            "  group Centerline entities 1,2 locked\n"
            "  group Outer id=4 groups 1,2\n"
            "\n"
            "Ids are comma-separated. \"sweep\" marks the group as an arc's\n"
            "sweep-angle rig (two construction lines and their Angle), which\n"
            "the canvas looks up by that kind; a name starting \"Sweep Angle\"\n"
            "is taken the same way for older scripts.");
    }

    hobbycad::sketch::Group g;
    g.name = args[0];
    g.kind = hobbycad::sketch::inferLegacyGroupKind(g.name);   // "sweep" below overrides
    args.erase(args.begin());

    auto readIds = [&](std::vector<int>& out, const std::string& what) -> bool {
        if (args.empty()) {
            r.exitCode = 1;
            r.error = subst("Expected ids after '%1'.", what);
            return false;
        }
        const std::vector<std::string> parts =
            split(args[0], ',');
        args.erase(args.begin());
        for (const std::string& p : parts) {
            bool ok = false;
            const int v = toInt(trim(p), &ok);
            if (!ok) {
                r.exitCode = 1;
                r.error = subst("'%1' is not an id.", trim(p));
                return false;
            }
            out.push_back(v);
        }
        return true;
    };

    while (!args.empty()) {
        const std::string word = toLower(args[0]);
        if (startsWith(word, "id=")) {
            bool ok = false;
            g.id = toInt(args[0].substr(3), &ok);   // > 0 asks addGroup for that id
            if (!ok || g.id <= 0) {
                return failure(subst("'%1' is not a group id.", args[0].substr(3)));
            }
            args.erase(args.begin());
        } else if (word == "entities") {
            args.erase(args.begin());
            if (!readIds(g.entityIds, "entities")) return r;
        } else if (word == "constraints") {
            args.erase(args.begin());
            if (!readIds(g.constraintIds, "constraints")) return r;
        } else if (word == "groups") {
            args.erase(args.begin());
            if (!readIds(g.childGroupIds, "groups")) return r;
        } else if (word == "locked") {
            g.locked = true;
            args.erase(args.begin());
        } else if (word == "sweep") {
            g.kind = hobbycad::sketch::GroupKind::SweepAngle;
            args.erase(args.begin());
        } else if (word == "pivot") {
            // "pivot x,y" stores a transform pivot on the group; "pivot center"
            // leaves it unset, which means the geometric center.
            args.erase(args.begin());
            if (args.empty()) { return failure("'pivot' needs x,y or 'center'."); }
            const std::string pv = args[0];
            args.erase(args.begin());
            if (equalsIgnoreCase(pv, "center")) {
                g.hasPivot = false;
            } else {
                const std::vector<std::string> xy = split(pv, ',');
                bool okx = false, oky = false;
                if (static_cast<int>(xy.size()) == 2) { g.pivot.x = toDouble(xy[0], &okx); g.pivot.y = toDouble(xy[1], &oky); }
                if (!(okx && oky)) { return failure(subst("'%1' is not a pivot: use x,y or 'center'.", pv)); }
                g.hasPivot = true;
            }
        } else {
            return failure(subst("Unexpected '%1'.", args[0]));
        }
    }

    // Validation and the back-link wiring are the library's (addGroup); the
    // messages are this prompt's.
    const sketch::AddGroupResult added = sketch::addGroup(
        g, m_pendingSketch.entities, m_pendingSketch.constraints, m_pendingSketch.groups);
    switch (added.problem) {
    case sketch::AddGroupProblem::Empty:
        return failure(
            "A group needs at least one member. An empty group is a name "
            "with nothing under it.");
    case sketch::AddGroupProblem::MissingEntity:
        return failure(subst("No entity with id %1 in this sketch.", added.id));
    case sketch::AddGroupProblem::MissingConstraint:
        return failure(subst("No constraint with id %1 in this sketch.", added.id));
    case sketch::AddGroupProblem::MissingChildGroup:
        return failure(subst(
            "No group with id %1 in this sketch. A child group has to "
            "exist before the group that holds it.", added.id));
    case sketch::AddGroupProblem::NameInUse:
        return failure(subst(
            "Group not created, name already in use: '%1' is group %2.", (g.name), added.id));
    case sketch::AddGroupProblem::IdInUse:
        return failure(subst("A group with id %1 already exists.", added.id));
    case sketch::AddGroupProblem::None:
        break;
    }
    g.id = added.id;

    r.output = subst("Created group '%1' [id %2] with %3 entity/ies, "
                              "%4 constraint(s), %5 child group(s)%6", (g.name), g.id, g.entityIds.size(), g.constraintIds.size(), g.childGroupIds.size(), g.locked ? ", locked" : std::string());
    return r;
}

// ---------------------------------------------------------------------------
//  transform: move/rotate/scale/mirror a set of entities as one unit
// ---------------------------------------------------------------------------
CliResult CliEngine::cmdTransform(std::vector<std::string> args)
{
    CliResult r;
    if (!m_inSketchMode) {
        return failure("'transform' works inside a sketch, and no sketch is open.");
    }
    const std::string usage = 
        "Usage: transform move <dx>,<dy> | rotate <deg> | scale <f> | mirror x|y\n"
        "                 | mirror line <x1>,<y1> <x2>,<y2> | point-to-point <x1>,<y1> <x2>,<y2>\n"
        "                 [about <x>,<y>|center] [copy] [group <name>|id=<n>]\n"
        "Without \"group\", the selected entity is transformed; if it belongs to a\n"
        "group, the whole group moves with it.";
    if (args.empty()) { return failure(usage); }

    sketch::GroupTransformParams p;
    const std::string kind = toLower(args[0]);
    int i = 1;
    auto parsePair = [&](const std::string& s, hobbycad::Point2D& out) -> bool {
        const std::vector<std::string> xy = split(s, ',');
        if (xy.size() != 2) return false;
        bool okx = false, oky = false;
        out.x = toDouble(xy[0], &okx); out.y = toDouble(xy[1], &oky);
        return okx && oky;
    };
    if (kind == "move") {
        p.kind = sketch::GroupTransformKind::Translate;
        if (static_cast<int>(args.size()) < 2 || !parsePair(args[1], p.delta)) { return failure(usage); }
        i = 2;
    } else if (kind == "rotate") {
        p.kind = sketch::GroupTransformKind::Rotate;
        bool ok = false;
        if (static_cast<int>(args.size()) >= 2) p.angleDeg = toDouble(args[1], &ok);
        if (!ok) { return failure(usage); }
        i = 2;
    } else if (kind == "scale") {
        p.kind = sketch::GroupTransformKind::Scale;
        bool ok = false;
        if (static_cast<int>(args.size()) >= 2) p.factor = toDouble(args[1], &ok);
        if (!ok) { return failure(usage); }
        i = 2;
    } else if (kind == "mirror") {
        p.kind = sketch::GroupTransformKind::Mirror;
        const std::string ax = static_cast<int>(args.size()) >= 2 ? toLower(args[1]) : std::string();
        if (ax == "x") { p.mirrorAcrossHorizontal = true; i = 2; }         // flip across the horizontal line
        else if (ax == "y") { p.mirrorAcrossHorizontal = false; i = 2; }   // flip across the vertical line
        else if (ax == "line" && static_cast<int>(args.size()) >= 4
                 && parsePair(args[2], p.mirrorA) && parsePair(args[3], p.mirrorB)) { p.mirrorLineGiven = true; i = 4; }
        else { return failure(usage); }
    } else if (kind == "point-to-point") {
        // A translate defined by two picked points: the from-point lands on the to-point.
        p.kind = sketch::GroupTransformKind::Translate;
        hobbycad::Point2D from, to;
        if (static_cast<int>(args.size()) < 3 || !parsePair(args[1], from) || !parsePair(args[2], to)) { return failure(usage); }
        p.delta = to - from;
        i = 3;
    } else { return failure(usage); }

    const hobbycad::sketch::Group* group = nullptr;
    bool aboutCenter = false, makeCopy = false;
    while (i < static_cast<int>(args.size())) {
        const std::string opt = toLower(args[i]);
        if (opt == "about" && i + 1 < static_cast<int>(args.size())) {
            if (equalsIgnoreCase(args[i + 1], "center")) {
                aboutCenter = true;   // the geometric center, even when the group stores a pivot
            } else {
                if (!parsePair(args[i + 1], p.center)) { return failure(usage); }
                p.centerGiven = true;
            }
            i += 2;
        } else if (opt == "copy") {
            makeCopy = true; i += 1;
        } else if (opt == "group" && i + 1 < static_cast<int>(args.size())) {
            const std::string token = args[i + 1];
            group = sketch::findGroupByRef(m_pendingSketch.groups, token);
            if (!group) {
                return failure(subst("No group called '%1' in this sketch. \"groups\" lists them.", token));
            }
            i += 2;
        } else { return failure(usage); }
    }

    // The set: a named group, else the selected entity's group, else the entity.
    std::vector<int> members;
    int groupIdForAdds = -1;
    if (group) {
        members = group->entityIds; groupIdForAdds = group->id;
    } else {
        if (m_selectedEntityId < 0) {
            return failure("Nothing selected. \"select <id>\" an entity first, or name a group.");
        }
        int gid = -1;
        if (const auto* sel = sketch::findEntityById(m_pendingSketch.entities, m_selectedEntityId)) gid = sel->groupId;
        if (gid >= 0) {
            for (const auto& e : m_pendingSketch.entities) if (e.groupId == gid) members.push_back(e.id);
            groupIdForAdds = gid;
        } else {
            members.push_back(m_selectedEntityId);
        }
    }

    // A group's stored pivot is the center unless "about" says otherwise.
    const hobbycad::sketch::Group* pivotGroup = nullptr;
    if (groupIdForAdds >= 0)
        for (const auto& g : m_pendingSketch.groups) if (g.id == groupIdForAdds) pivotGroup = &g;
    if (!p.centerGiven && !aboutCenter && pivotGroup && pivotGroup->hasPivot) {
        p.centerGiven = true;
        p.center = pivotGroup->pivot;
    }

    // Worked out on copies (the library's scratch run, shared with the
    // canvas): with "copy" the members and their internal constraints are
    // cloned and the clones move, the originals stay. Kept only if the
    // solver accepts the result.
    sketch::TransformScratch scratch = sketch::transformSet(
        m_pendingSketch.entities, m_pendingSketch.constraints, members, p, makeCopy,
        sketch::nextFreeEntityId(m_pendingSketch.entities),
        sketch::nextFreeConstraintId(m_pendingSketch.constraints));
    const sketch::GroupTransformResult& res = scratch.result;
    if (!res.applied) {
        return failure(subst("Not transformed: %1.", (res.refusal)));
    }
    // The commit is the library's, shared with the canvas: it solves the
    // scratch, refuses whole when a constraint cannot be kept, and otherwise
    // writes the geometry, homes the clones and any reference geometry,
    // makes the copy group and moves the stored pivot.
    sketch::TransformCommitOptions opts;
    opts.homeGroupId = groupIdForAdds;
    opts.wholeGroup = (pivotGroup != nullptr);
    const sketch::TransformCommit commit = sketch::commitTransform(
        m_pendingSketch.entities, m_pendingSketch.constraints, m_pendingSketch.groups,
        scratch, makeCopy, p, opts, [this]() { return nextPendingGroupId(); });
    if (!commit.applied) {
        return failure(subst("Not transformed: %1. Nothing changed.", (commit.refusal)));
    }
    const sketch::SolveResult& sr = commit.solve;
    const int copyGroupId = commit.copyGroupMade ? commit.copyGroup.id : -1;
    const hobbycad::sketch::Group& copyGroup = commit.copyGroup;

    std::vector<std::string> out;
    out.push_back(subst("%1: %2 entity/ies %3 as one unit%4", kind, static_cast<int>(res.changedEntityIds.size()), makeCopy ? "copied and moved" : "moved", copyGroupId >= 0 ? subst(" (group '%1' [id %2])", (copyGroup.name), copyGroupId) : std::string()));
    for (const auto& n : res.notes) out.push_back(subst("  %1", (n)));
    out.push_back(subst("state   %1", std::string(sketch::sketchStateName(sr.state))));
    if (sr.dofIsKnown())
        out.push_back(subst("dof     %1%2", sr.dof, sr.dof == 0 ? "   (fully constrained)" : std::string()));
    r.output = join(out, '\n');
    return r;
}

CliResult CliEngine::cmdGroups() const
{
    CliResult r;

    const hobbycad::SketchData* sk = currentSketchForReading();
    if (!sk) {
        return failure(
            "No sketch is open or selected. Use 'create sketch' or "
            "'select sketch <name>'.");
    }

    if (sk->groups.empty()) {
        r.output = "No groups.";
        return r;
    }

    // Named for what it makes, and not "join": the library's join() is
    // what it calls, and an auto lambda cannot refer to itself anyway.
    auto idList = [](const std::vector<int>& v) {
        std::vector<std::string> s;
        for (int i : v) s.push_back(numToString(i));
        return s.empty() ? std::string("-") : join(s, ',');
    };

    std::vector<std::string> out;
    out.push_back("id      name                 entities     "
                          "constraints  groups");
    for (const auto& g : sk->groups) {
        out.push_back(subst("%1 %2 %3 %4 %5%6", pad(numToString(g.id), -7), pad((g.name), -20), pad(idList(g.entityIds), -12), pad(idList(g.constraintIds), -12), idList(g.childGroupIds), (g.locked ? "  [locked]" : std::string())
                        + (g.hasPivot ? subst("  [pivot %1,%2]", g.pivot.x, g.pivot.y) : std::string())));
    }
    r.paginate = true;
    r.output = join(out, '\n');
    return r;
}

// "constrain edit <id> value <expr> | driving | reference".
CliResult CliEngine::cmdConstrainEdit(const std::vector<std::string>& args)
{
    CliResult r;
    if (static_cast<int>(args.size()) < 3) {
        return failure(
            "Usage: constrain edit <id> value <expr>\n"
            "       constrain edit <id> driving | reference");
    }
    bool ok = false;
    const int cid = toInt(args[1], &ok);
    if (!ok) {
        return failure(subst("Constraint id must be a number: '%1'", args[1]));
    }
    hobbycad::ConstraintData* target = sketch::findConstraintById(m_pendingSketch.constraints, cid);
    if (!target) {
        return failure(subst("No constraint %1 in the current sketch.", cid));
    }
    const std::string field = toLower(args[2]);
    if (field == "value") {
        if (!sketch::isDimensionalConstraint(target->type)) {
            return failure(subst("Constraint %1 (%2) has no value to edit.", cid,
                                 constraintName(target->type)));
        }
        if (static_cast<int>(args.size()) < 4) {
            return failure("Usage: constrain edit <id> value <expr>");
        }
        const std::string expr = join(slice(args, 3), ' ');
        double v = 0.0; std::string err;
        if (!hobbycad::evaluateExpression(expr, v, parameterValues(), &err)) {
            return failure(subst("Cannot evaluate '%1': %2", expr, (err)));
        }
        if (!sketch::isValidConstraintValue(target->type, v)) {
            return failure(subst("%1 must be greater than zero.", constraintName(target->type)));
        }
        target->value = v;
        r.exitCode = 0;
        r.output = subst("Constraint %1 value set to %2. Run 'solve' to apply.", cid, v);
        return r;
    }
    if (field == "driving") {
        target->isDriving = true;
        r.output = subst("Constraint %1 is now driving. Run 'solve' to apply.", cid);
        return r;
    }
    if (field == "reference") {
        target->isDriving = false;
        r.output = subst("Constraint %1 is now reference (driven).", cid);
        return r;
    }
    r.exitCode = 1;
    r.error = subst("Unknown edit field '%1'. Use value, driving, or reference.", field);
    return r;
}

CliResult CliEngine::cmdConstrain(std::vector<std::string> args)
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure(
            "'constrain' works inside a sketch, and no sketch is open.\n"
            "Use 'create sketch <plane> <name>' first.");
    }

    // Edit an existing constraint (handled before the trailing-"reference"
    // stripping below, so "constrain edit <id> reference" is not misread).
    if (!args.empty() && equalsIgnoreCase(args[0], "edit")) {
        return cmdConstrainEdit(args);
    }

    // "reference" makes the constraint a driven (measurement-only) one,
    // the same idea the Dimension tool offers when a driving dimension
    // would over-constrain the sketch.
    bool driving = true;
    if (!args.empty() &&
        toLower(args.back()) == "reference") {
        args.pop_back();
        driving = false;
    }

    if (args.empty()) {
        return failure(constrainUsage());
    }

    sketch::ConstraintType type;
    if (!hobbycad::sketch::parseConstraintTypeName(args[0], &type)) {
        return failure(subst("Unknown constraint '%1'.\n\n%2", args[0], constrainUsage()));
    }
    args.erase(args.begin());

    const int wantEntities = sketch::requiredEntityCount(type);
    const bool wantsValue  = sketch::isDimensionalConstraint(type);

    if (static_cast<int>(args.size()) < wantEntities) {
        return failure(subst(
            "%1 needs %2 entity id%3%4, and %5 %6 given.", constraintName(type),
            wantEntities, wantEntities == 1 ? std::string() : "s",
            wantsValue ? " and a value" : std::string(), static_cast<int>(args.size()),
            static_cast<int>(args.size()) == 1 ? "was" : "were"));
    }

    // Entity ids first, then the value if this type takes one.
    //
    // "<id>.<point>" names a particular point of an entity: "1.0" is the
    // start of line 1, "1.1" its end. Constraints like Coincident and
    // point-to-point Distance are ABOUT specific points, and without a way
    // to say which, they can only be expressed for whole entities.
    std::vector<int> ids;
    std::vector<int> pointIndices;
    bool anyPointGiven = false;
    for (int i = 0; i < wantEntities; ++i) {
        const std::string& token = args[i];
        int id = 0, pointIndex = 0;
        bool hadPoint = false;
        switch (sketch::parsePointRef(token, id, pointIndex, &hadPoint)) {
        case sketch::PointRefProblem::BadPointIndex:
            return failure(subst(
                "'%1' is not a point index. Use <id>.<point>, for "
                "example \"1.0\" for the start of entity 1.", token.substr(token.find('.') + 1)));
        case sketch::PointRefProblem::BadEntityId:
            return failure(subst("'%1' is not an entity id.", token));
        case sketch::PointRefProblem::None:
            break;
        }
        if (hadPoint) anyPointGiven = true;
        pointIndices.push_back(pointIndex);
        ids.push_back(id);
    }

    // The operands' checks come first, before the value is read, and are
    // the library's, shared with the canvas.
    const auto refusal = [&](const sketch::ConstraintCheck& check) -> std::string {
        switch (check.problem) {
        case sketch::ConstraintProblem::None:
            break;
        case sketch::ConstraintProblem::UnknownEntity:
            return subst("No entity with id %1 in this sketch.", check.entityId);
        case sketch::ConstraintProblem::RepeatedOperand:
            // Naming the same entity twice is a typo unless different
            // points of it were meant: "coincident 1.0 1.1" closes a line
            // onto itself, which is a real thing to ask for.
            return subst("Entity %1 is listed twice; a %2 constraint relates "
                         "different entities or different points.",
                         check.entityId, constraintName(type));
        case sketch::ConstraintProblem::WrongOperands:
            return check.reason;
        case sketch::ConstraintProblem::BadValue:
            // A radius or a distance of zero collapses geometry rather than
            // constraining it; an angle of zero is legitimate.
            return subst("%1 must be greater than zero.",
                         constraintName(type));
        case sketch::ConstraintProblem::Redundant:
            return subst("That %1 is already implied by the constraints in "
                         "place, so it would add nothing.\n"
                         "Add \"reference\" to record it as a measurement "
                         "instead.", constraintName(type));
        case sketch::ConstraintProblem::OverConstrains:
            return subst("That %1 would over-constrain the sketch: %2\n"
                         "Add \"reference\" to record it as a measurement "
                         "instead.", constraintName(type),
                         check.reason);
        }
        return std::string();
    };
    {
        hobbycad::ConstraintData operands;
        operands.type = type;
        operands.entityIds = ids;
        operands.pointIndices = pointIndices;
        sketch::ConstraintCheckOptions only;
        only.operands = false;
        only.value = false;
        only.solver = false;
        const sketch::ConstraintCheck check = sketch::checkNewConstraint(
            m_pendingSketch.entities, m_pendingSketch.constraints, operands, only);
        if (!check.ok()) return failure(refusal(check));
    }

    double value = 0.0;
    std::string valueText;
    if (wantsValue) {
        if (static_cast<int>(args.size()) <= wantEntities) {
            return failure(subst("%1 needs a value, for example \"%2\".",
                                 constraintName(type), sketch::isAngularConstraint(type)
                                   ? "90deg"
                                   : "25mm"));
        }
        std::string why;
        if (!parseConstraintValue(args[wantEntities], type, parameterValues(),
                                  &value, &valueText, &why)) {
            return failure(why);
        }
    } else if (static_cast<int>(args.size()) > wantEntities) {
        return failure(subst(
            "%1 is a geometric constraint and takes no value, but '%2' was "
            "given.", constraintName(type), args[wantEntities]));
    }

    hobbycad::ConstraintData c;
    c.type = type;
    c.entityIds = ids;
    // Only carry point indices when some were actually given: an all-zeros
    // list is not the same as "no preference", and the solver reads them.
    if (anyPointGiven) c.pointIndices = pointIndices;
    c.value = value;
    // Keep the source expression when it references a parameter, so the
    // dimension re-evaluates if that parameter later changes. Detected by
    // comparing evaluation with the parameters against evaluation without:
    // if they differ (or the bare form fails), a parameter was used.
    if (wantsValue && !valueText.empty()) {
        const std::string expr = valueText;
        if (hobbycad::expressionUsesParameters(expr, parameterValues())) c.expression = expr;
    }
    c.isDriving = driving;

    // Give it its real id BEFORE the over-constraint check. libslvs treats
    // handle 0 as invalid and silently drops such a constraint, so checking
    // an id-less constraint tested a system that did not contain it; a
    // second Horizontal on the same line came back clean.
    c.id = nextPendingConstraintId();

    // The operand kinds (libslvs aborts on some wrong pairings), the value,
    // and redundancy, caught at insert time: with a redundant set already in
    // place, libslvs cannot say which member is the extra one.
    {
        const sketch::ConstraintCheck check = sketch::checkNewConstraint(
            m_pendingSketch.entities, m_pendingSketch.constraints, c);
        if (!check.ok()) return failure(refusal(check));
    }

    const int id = addPendingConstraint(c);

    std::vector<std::string> idText;
    for (int i : ids) idText.push_back(numToString(i));

    r.output = subst("Added %1 on entity %2", constraintName(type), join(idText, " and "));
    if (wantsValue) {
        // Report the PARSED value in canonical form rather than echoing the
        // typed text with a unit appended: "12mm" plus "mm" read as
        // "12mmmm", and an expression like "(width/2)" says nothing about
        // what it came to. The typed form is shown alongside when it was
        // not already a plain number.
        const std::string canonical =
            sketch::isAngularConstraint(type)
                ? subst("%1%2", value, (sketch::constraintUnit(type))): (hobbycad::formatValueWithUnit(
                      value, hobbycad::LengthUnit::Millimeters));

        r.output += subst(" = %1", canonical);

        // Echo what was typed only when it actually says something the
        // canonical form does not: a parameter or an expression. "10mm"
        // against "10 mm" differs by a space and is just noise.
        const std::string squashedTyped =
            removeAll(simplify(valueText), ' ');
        const std::string squashedCanon =
            removeAll(simplify(canonical), ' ');
        if (squashedTyped != squashedCanon &&
            squashedTyped != numToString(value)) {
            r.output += subst(" (from %1)", valueText);
        }
    }
    if (!driving) r.output += " (reference)";
    r.output += subst(" [id %1]", id);
    return r;
}

CliResult CliEngine::cmdConstraints() const
{
    CliResult r;

    const hobbycad::SketchData* sk = currentSketchForReading();
    if (!sk) {
        return failure(
            "No sketch is open or selected. Use 'create sketch' or "
            "'select sketch <name>'.");
    }

    if (sk->constraints.empty()) {
        r.output = "No constraints.";
        return r;
    }

    std::vector<std::string> out;
    out.push_back("id      type            entities     value");
    for (const auto& c : sk->constraints) {
        std::vector<std::string> ids;
        for (int i : c.entityIds) ids.push_back(numToString(i));

        std::string value;
        if (sketch::isDimensionalConstraint(c.type)) {
            value = sketch::isAngularConstraint(c.type)
                ? subst("%1%2", c.value, (sketch::constraintUnit(c.type))): (hobbycad::formatValueWithUnit(
                      c.value, hobbycad::LengthUnit::Millimeters));
            if (!c.isDriving) value += "  (reference)";
        }

        out.push_back(subst("%1 %2 %3 %4", pad(numToString(c.id), -7),
                            pad(constraintName(c.type), -15),
                            pad(numToString(join(ids, ',')), -12), value));
    }
    r.paginate = true;
    r.output = join(out, '\n');
    return r;
}

// cmdSolve, after a successful solve: re-derive the entities that follow
// others (slots from their path, offsets from their parent, projections from
// their source). Returns how many slots followed their centerlines.
int CliEngine::rederiveDependentEntities(std::vector<hobbycad::SketchEntityData>& scratch)
{
    // The pass itself is the library's (shared with the GUI's intent); this
    // only supplies the sketch's plane. Same-sketch projection sources only:
    // cross-sketch resolution needs the other sketch's plane, the next step.
    const hobbycad::Project* proj = project();
    const hobbycad::PlaneBasis basis = proj
        ? hobbycad::planeBasisFor(m_pendingSketch, *proj)
        : hobbycad::planeBasisFor(m_pendingSketch.plane);
    return sketch::rederiveDependents(scratch, basis);
}

CliResult CliEngine::cmdSolve(std::vector<std::string> args)
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure(
            "'solve' works inside a sketch, and no sketch is open.");
    }

    if (m_pendingSketch.entities.empty()) {
        r.output = "Nothing to solve: the sketch is empty.";
        return r;
    }

    // "solve <group>" narrows the system to one group's members. Useful
    // once a sketch has several independent assemblies in it: a failure in
    // one no longer hides behind the others, and the dof reported is that
    // group's rather than the whole sketch's.
    const hobbycad::sketch::Group* only = nullptr;
    if (!args.empty()) {
        const std::string token = args[0];
        if (static_cast<int>(args.size()) > 1) {
            r.exitCode = 1;
            r.error = "Usage: solve [<group name>|id=<n>]";
            return r;
        }
        only = sketch::findGroupByRef(m_pendingSketch.groups, token);
        if (!only) {
            r.exitCode = 1;
            r.error = subst(
                "No group called '%1' in this sketch. \"groups\" lists them.", token);
            return r;
        }
    }

    // Solve a COPY, and only keep it if the solver succeeded. A failed
    // solve that had already moved half the geometry would leave the
    // sketch in a state the person never drew.
    std::vector<hobbycad::SketchEntityData> scratch;
    std::vector<hobbycad::ConstraintData> using_;

    if (only) {
        for (const auto& e : m_pendingSketch.entities) {
            if (hobbycad::contains(only->entityIds, e.id)) scratch.push_back(e);
        }
        // A constraint comes along when every entity it names is inside the
        // group. One reaching outside would be solved against geometry that
        // is not in the system, which libslvs cannot do and which would
        // silently drop the constraint rather than report it.
        for (const auto& c : m_pendingSketch.constraints) {
            if (sketch::constraintInsideSet(c, only->entityIds)) using_.push_back(c);
        }
        if (scratch.empty()) {
            r.output = subst("Nothing to solve: group '%1' has no "
                                      "entities.", (only->name));
            return r;
        }
    } else {
        scratch = m_pendingSketch.entities;
        using_ = m_pendingSketch.constraints;
    }

    // Parameters are the source of truth for any dimension that was entered as
    // an expression: re-evaluate those against the current parameter values
    // right before solving, so a parameter edit flows through to the geometry
    // on the next solve (and undoing the parameter flows back the same way).
    {
        const auto params = parameterValues();
        for (auto& c : using_) {
            if (c.expression.empty()) continue;
            double r = 0.0;
            if (hobbycad::evaluateExpression(c.expression, r, params)) c.value = r;
        }
        // Persist the re-evaluated values back onto the sketch being edited so
        // "print" and a later save show the current numbers.
        if (!only) {
            for (auto& live : m_pendingSketch.constraints) {
                if (live.expression.empty()) continue;
                double r = 0.0;
                if (hobbycad::evaluateExpression(live.expression, r, params)) live.value = r;
            }
        }
    }

    sketch::Solver solver;
    const sketch::SolveResult res =
        solver.solve(scratch, using_);

    // The solver models lines, arcs, and circles, not compound entities. A
    // rectangle (or parallelogram/polygon) it does not model at all, so a
    // floating rectangle came back "dof 0 (fully constrained)", the
    // opposite of the truth. For the read-out ONLY, count the dof of the
    // decomposition the GUI stores: the four edges and the constraints that
    // hold the shape square. The geometry write-back below stays on the
    // stored compound; closing the storage-parity gap is a separate item
    // still on TODO. Non-compound entities pass through unchanged.
    const sketch::SolveResult report =
        sketch::solveReportWithDecomposedCompounds(scratch, using_, res);

    std::vector<std::string> out;
    if (only) {
        out.push_back(subst("group   %1 (%2 entity/ies, %3 constraint(s))", (only->name), static_cast<int>(scratch.size()), static_cast<int>(using_.size())));
    }
    out.push_back(subst("state   %1", std::string(sketch::sketchStateName(report.state))));

    if (report.dofIsKnown()) {
        out.push_back(subst("dof     %1%2", report.dof, report.dof == 0
                            ? "   (fully constrained)"
                            : std::string()));
    } else {
        // Saying "dof 0" when the solver could not produce a count would
        // read as "fully constrained", which is the opposite of the truth.
        out.push_back("dof     not available in this state");
    }

    // [HobbyCAD] Surface which points the solver reports as still free
    // (under-constrained), from the free-parameter report (patch 0007), when
    // the linked libslvs supports it. Shown as entity.point handles so the user
    // can see exactly where the remaining degrees of freedom live.
    if (report.freePointsValid && !report.freePoints.empty()) {
        std::vector<std::string> fp;
        for (const auto& pt : report.freePoints)
            fp.push_back(subst("%1.%2", pt.first, pt.second));
        out.push_back(subst("free    %1", join(fp, ' ')));
    }

    if (res.success) {
        const int followed = rederiveDependentEntities(scratch);

        if (only) {
            // Write the solved subset back by id: `scratch` holds only the
            // group's entities, so assigning the whole vector would delete
            // everything outside it.
            for (const auto& solved : scratch) {
                for (auto& live : m_pendingSketch.entities) {
                    if (live.id == solved.id) { live = solved; break; }
                }
            }
        } else {
            m_pendingSketch.entities = scratch;
        }
        out.push_back("Geometry updated to satisfy the constraints.");
        if (followed > 0) {
            out.push_back(subst("%1 slot(s) followed their centerlines.", followed));
        }

        // [HobbyCAD] Re-measure reference parameters from the solved geometry
        // and re-evaluate the parameter set so dependents update.
        recomputeReferenceParameters();
    } else {
        out.push_back("Geometry left unchanged.");
        if (!res.errorMessage.empty()) {
            out.push_back((res.errorMessage));
        }
    }

    r.output = join(out, '\n');
    return r;
}

CliResult CliEngine::cmdDelete(const std::vector<std::string>& args)
{
    CliResult r;

    // Inside a sketch, delete works on the geometry being drawn.
    if (m_inSketchMode) {
        std::vector<std::string> a = args;
        bool constraintTarget = false;
        if (!a.empty() && toLower(a[0]) == "entity") {
            a.erase(a.begin());
        } else if (!a.empty() &&
                   toLower(a[0]) == "constraint") {
            a.erase(a.begin());
            constraintTarget = true;
        }
        if (a.size() != 1) {
            return failure(
                "Usage: delete [entity] <id>\n"
                "       delete constraint <id>\n"
                "\n"
                "Inside a sketch, delete removes an entity or a constraint.\n"
                "Entities and constraints have SEPARATE id sequences, so\n"
                "\"constraint\" is required to name one; a bare id is an\n"
                "entity. Ids are shown as each is created, and by \"print\"\n"
                "and \"constraints\".");
        }

        bool ok = false;
        const int wanted = toInt(a[0], &ok);
        if (!ok) {
            return failure(subst(
                "'%1' is not an entity id. Inside a sketch, delete takes an "
                "id, not a name; entities do not have names.", a[0]));
        }

        if (constraintTarget) {
            auto& cs = m_pendingSketch.constraints;
            for (auto it = cs.begin(); it != cs.end(); ++it) {
                if (it->id != wanted) continue;
                cs.erase(it);
                r.output = subst("Deleted constraint %1. %2 remaining.", wanted, cs.size());
                return r;
            }
            r.exitCode = 1;
            r.error = subst("No constraint with id %1 in this sketch.", wanted);
            return r;
        }

        if (!pendingEntity(wanted)) {
            return failure(subst("No entity with id %1 in this sketch.", wanted));
        }
        // The cascade is the library's, shared with the canvas: constraints
        // naming the entity go, a slot following it keeps its shape but
        // loses the link, the id leaves every group, and a group emptied by
        // that is removed.
        const sketch::DeleteReport dropped = sketch::deleteEntities(
            m_pendingSketch.entities, m_pendingSketch.constraints, m_pendingSketch.groups,
            std::vector<int>{wanted});
        if (m_selectedEntityId == wanted) m_selectedEntityId = -1;

        r.output = subst("Deleted entity %1. %2 remaining.", wanted, m_pendingSketch.entities.size());
        if (dropped.constraintsDropped > 0) {
            r.output += subst(
                "\n%1 constraint(s) referring to it were removed too.", dropped.constraintsDropped);
        }
        if (dropped.slotsUnlinked > 0) {
            r.output += subst(
                "\n%1 slot(s) no longer follow a centerline; their "
                "shape is kept as it stands.", dropped.slotsUnlinked);
        }
        if (dropped.groupsRemoved > 0) {
            r.output += subst("\n%1 group(s) left empty were removed.", dropped.groupsRemoved);
        }
        return r;
    }

    if (args.size() != 2) {
        r.exitCode = 1;
        r.error = 
            "Usage: delete <sketch|body|plane> <name>\n"
            "       delete <sketch|body|plane> id=<n>\n"
            "\n"
            "Examples:\n"
            "  delete sketch Sketch1\n"
            "  delete plane id=2\n"
            "\n"
            "Undoable with \"undo\" when an undo history is attached, which\n"
            "it is in the GUI terminal and under --no-gui. The command says\n"
            "which case applied when it ran.";
        return r;
    }

    hobbycad::ObjectKind kind;
    if (!hobbycad::parseObjectKind(args[0], kind)) {
        r.exitCode = 1;
        r.error = subst("Cannot delete a '%1'. Try sketch, body or plane.", args[0]);
        return r;
    }

    hobbycad::Project* proj = project();
    if (!proj) return noDocument();

    std::string why;
    const int at = resolveObjectIndex(static_cast<int>(kind), args[1], &why);
    if (at < 0) {
        r.exitCode = 1;
        r.error = why;
        return r;
    }

    std::string name;
    switch (kind) {
    case hobbycad::ObjectKind::Sketch: {
        const hobbycad::SketchData& sk = proj->sketches()[static_cast<size_t>(at)];
        name = (sk.name);
        const int id = sk.id;
        // Through the session: the sketch and its feature record go together,
        // into the history every front end shares.
        if (hobbycad::ProjectSession* s = session()) {
            s->deleteFeature(id, subst("Delete sketch '%1'", name));
        } else {
            proj->removeSketch(at);
        }
        break;
    }
    case hobbycad::ObjectKind::Plane: {
        const auto before = proj->constructionPlanes();
        name = (before[static_cast<size_t>(at)].name);
        proj->removeConstructionPlane(at);
        recordPlaneListChange(before,
                              subst("Delete plane '%1'", name));
        break;
    }
    case hobbycad::ObjectKind::Body: {
        // No removeBody(): rebuild the list without it, which keeps every
        // other body's id and name rather than renumbering them.
        auto bodies = proj->bodies();
        const auto before = bodies;
        name = (bodies[static_cast<size_t>(at)].name);
        bodies.erase(bodies.begin() + at);
        proj->setBodies(bodies);
        recordBodyListChange(before,
                             subst("Delete body '%1'", name));
        break;
    }
    }

    // A selection pointing at what was just deleted would keep naming it in
    // the prompt.
    if (m_context.isSet()) {
        const bool sameKind =
            (kind == hobbycad::ObjectKind::Sketch && m_context.kind == CliContext::Kind::Sketch) ||
            (kind == hobbycad::ObjectKind::Body   && m_context.kind == CliContext::Kind::Body) ||
            (kind == hobbycad::ObjectKind::Plane  && m_context.kind == CliContext::Kind::Plane);
        if (sameKind && m_context.name == name) m_context = CliContext{};
    }

    if (m_docHost) m_docHost->hostDocumentChanged();

    r.output = m_undoHost
        ? subst("Deleted %1 '%2'. Use \"undo\" to bring it back.", std::string(hobbycad::objectKindName(kind)), name): subst("Deleted %1 '%2'. This CANNOT be undone: no undo "
                         "history is attached.", std::string(hobbycad::objectKindName(kind)), name);
    return r;
}

CliResult CliEngine::cmdRename(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.size() != 3) {
        return failure(
            "Usage: rename <sketch|body|plane> <name> <new name>\n"
            "       rename <sketch|body|plane> id=<n> <new name>\n"
            "\n"
            "Examples:\n"
            "  rename sketch Sketch1 Profile\n"
            "  rename plane id=2 \"Top offset\"\n"
            "\n"
            "Quote a new name containing spaces.");
    }

    hobbycad::ObjectKind kind;
    if (!hobbycad::parseObjectKind(args[0], kind)) {
        return failure(subst("Cannot rename a '%1'. Try sketch, body or plane.", args[0]));
    }

    const std::string newName = args[2];

    // Same rule the GUI applies, so a name accepted here is accepted there.
    std::string reason;
    if (!hobbycad::isValidObjectName(newName, &reason)) {
        return failure(subst("'%1' is not a usable name: %2", newName, (reason)));
    }

    hobbycad::Project* proj = project();
    if (!proj) return noDocument();

    std::string why;
    const int at = resolveObjectIndex(static_cast<int>(kind), args[1], &why);
    if (at < 0) {
        return failure(why);
    }

    // Names are how a person refers to these, and how "select" finds them,
    // so two objects of a kind sharing one is a trap rather than a nicety.
    if (hobbycad::objectNameTaken(*proj, kind, newName, at)) {
        return failure(subst("Another %1 is already called '%2'.", std::string(hobbycad::objectKindName(kind)), newName));
    }

    std::string oldName;
    switch (kind) {
    case hobbycad::ObjectKind::Sketch: {
        oldName = (proj->sketches()[static_cast<size_t>(at)].name);
        const int id = proj->sketches()[static_cast<size_t>(at)].id;
        const std::string desc =
            subst("Rename sketch '%1' to '%2'", oldName, newName);
        if (hobbycad::ProjectSession* s = session()) {
            s->renameFeature(id, newName, desc);   // the record follows the sketch
        } else {
            auto sk = proj->sketches()[static_cast<size_t>(at)];
            sk.name = newName;
            proj->setSketch(at, sk);
        }
        break;
    }
    case hobbycad::ObjectKind::Plane: {
        const auto before = proj->constructionPlanes();
        auto pl = before[static_cast<size_t>(at)];
        oldName = (pl.name);
        pl.name = newName;
        proj->setConstructionPlane(at, pl);
        recordPlaneListChange(before,
            subst("Rename plane '%1' to '%2'", oldName, newName));
        break;
    }
    case hobbycad::ObjectKind::Body: {
        oldName = (proj->bodies()[static_cast<size_t>(at)].name);
        const int id = proj->bodies()[static_cast<size_t>(at)].id;
        const std::string desc =
            subst("Rename body '%1' to '%2'", oldName, newName);
        if (hobbycad::ProjectSession* s = session()) {
            s->renameBody(id, newName, desc);
        } else {
            auto bodies = proj->bodies();
            bodies[static_cast<size_t>(at)].name = newName;
            proj->setBodies(bodies);
        }
        break;
    }
    }

    // The prompt shows the selected object's name; leaving the old one there
    // would be showing a name nothing answers to any more.
    if (m_context.isSet() && m_context.name == oldName) {
        m_context.name = newName;
    }

    if (m_docHost) m_docHost->hostDocumentChanged();

    r.output = subst("Renamed %1 '%2' to '%3'.", std::string(hobbycad::objectKindName(kind)), oldName, newName);
    return r;
}

CliResult CliEngine::cmdCreate(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.empty()) {
        return failure(
            "Usage: create sketch [<plane>] [name]\n"
            "       create sketch on [plane] <name> [sketch-name]\n"
            "       create sketch \"name\" on [plane] <plane-name>\n"
            "\n"
            "Built-in planes: XY (default), XZ, YZ\n"
            "Named planes:    use 'on [plane] <name>' for construction planes\n"
            "\n"
            "Examples:\n"
            "  create sketch                             (XY, auto-named)\n"
            "  create sketch XZ                          (XZ, auto-named)\n"
            "  create sketch XZ MySketch                 (XZ, named)\n"
            "  create sketch on plane Front              (construction plane)\n"
            "  create sketch on Front                    (same, 'plane' optional)\n"
            "  create sketch \"MySketch\" on plane XZ      (name first)\n"
            "  create sketch MySketch                    (XY, named)\n"
            "\n"
            "  create plane at X,Y,Z [with rotation RX RY RZ]\n"
            "  create plane offset from X,Y,Z [with rotation RX RY RZ]\n"
            "  create plane relative to <plane> [offset N] [with rotation RX RY RZ]\n"
            "    rotation angles are degrees about global X Y Z (space-separated)");
    }

    std::string type = toLower(args[0]);

    if (type == "plane") return cmdCreatePlane(slice(args, 1));

    if (type == "sketch") {
        // Accepted forms (args after "sketch"):
        //
        //   (nothing)                        → XY, auto-named
        //   XZ [name]                        → built-in plane, optional name
        //   on [plane] Front [name]          → named plane, optional name
        //   "name" on [plane] XZ             → name first, then plane
        //   name                             → XY, named (if not a plane keyword)

        SketchPlane plane = SketchPlane::XY;
        int constructionPlaneId = -1;
        std::string planeName = "XY";
        std::string sketchName;
        std::string planeError;

        // Helper: resolve a plane argument (XY/XZ/YZ or a construction
        // plane's name) to plane + constructionPlaneId + display name. An
        // unknown name is an error, not a silent XY sketch.
        auto resolvePlane = [&](const std::string& arg) {
            static const std::vector<hobbycad::ConstructionPlaneData> kNoPlanes;
            const hobbycad::Project* proj = project();
            const auto& planes = proj ? proj->constructionPlanes() : kNoPlanes;
            std::string shown;
            if (!hobbycad::resolveSketchPlaneRef(arg, planes, plane,
                                                 constructionPlaneId, &shown)) {
                planeError = subst(
                    "Unknown plane '%1'. Use XY, XZ, YZ, or the name of an "
                    "existing construction plane.", arg);
                return;
            }
            planeName = (shown);
        };

        // Helper: find "on [plane] <name>" starting at args[idx], returns
        // the index after the plane specification (for any trailing name)
        auto parseOnPlane = [&](int idx) -> int {
            // args[idx] == "on"
            int planeIdx = idx + 1;
            if (planeIdx < static_cast<int>(args.size())
                && toLower(args[planeIdx]) == "plane") {
                planeIdx++;  // Skip optional "plane" keyword
            }
            if (planeIdx < static_cast<int>(args.size())) {
                resolvePlane(args[planeIdx]);
                return planeIdx + 1;
            }
            return idx + 1;  // "on" with nothing after: ignore
        };

        if (static_cast<int>(args.size()) >= 2) {
            std::string arg1 = args[1];
            std::string arg1Upper = toUpper(arg1);
            std::string arg1Lower = toLower(arg1);

            if (arg1Upper == "XY" || arg1Upper == "XZ"
                || arg1Upper == "YZ") {
                // Built-in plane: create sketch XZ [name]
                resolvePlane(arg1);
                if (static_cast<int>(args.size()) > 2) {
                    sketchName = join(slice(args, 2), " ");
                }
            } else if (arg1Lower == "on" && static_cast<int>(args.size()) >= 3) {
                // Plane first: create sketch on [plane] <name> [sketch-name]
                int afterPlane = parseOnPlane(1);
                if (static_cast<int>(args.size()) > afterPlane) {
                    sketchName = join(slice(args, afterPlane), " ");
                }
            } else {
                // arg1 is not a plane keyword; it's either a sketch name
                // or could be "name" followed by "on [plane] ..."
                //
                // Check if "on" follows: create sketch "name" on [plane] XZ
                int onIdx = -1;
                for (int i = 2; i < static_cast<int>(args.size()); ++i) {
                    if (toLower(args[i]) == "on") {
                        onIdx = i;
                        break;
                    }
                }

                if (onIdx >= 0 && onIdx + 1 < static_cast<int>(args.size())) {
                    // Name is everything from arg1 up to "on"
                    sketchName = join(slice(args, 1, onIdx - 1), " ");
                    parseOnPlane(onIdx);
                } else {
                    // No "on": everything is the sketch name, default XY
                    sketchName = join(slice(args, 1), " ");
                }
            }
        }

        if (!planeError.empty()) return failure(planeError);

        // Auto-name if none was provided
        if (sketchName.empty()) {
            m_sketchCounter++;
            sketchName = subst("Sketch%1", m_sketchCounter);
        }

        // Enter sketch mode
        // Refuse a bad name HERE, before any geometry is entered. Checking
        // at finish meant the user drew a sketch and was then told the
        // name was never acceptable.
        std::string nameWhy;
        if (!hobbycad::isValidObjectName(sketchName, &nameWhy)) {
            return failure((nameWhy));
        }

        // Already editing one: starting another would silently abandon it.
        if (m_inSketchMode) {
            return failure(subst(
                "Already editing sketch '%1'. Use \"finish\" to keep it or "
                "\"discard\" to abandon it first.", m_currentSketchName));
        }

        m_inSketchMode = true;
    m_pendingSketch = SketchData{};
    m_sketchEdits.clear();   // a new sketch starts a new history
    m_selectedEntityId = -1;   // entities die with their sketch   // start collecting geometry
        m_currentSketchName = sketchName;
        m_currentSketchPlane = plane;
        m_currentConstructionPlaneId = constructionPlaneId;
        m_currentSketchPlaneName = planeName;
        // The pending sketch carries its plane from the start, so project,
        // solve and everything else that asks for its basis while it is
        // being drawn sees the real plane, not SketchData's XY default.
        m_pendingSketch.plane = plane;
        m_pendingSketch.constructionPlaneId = constructionPlaneId;

        r.output = subst("Created sketch '%1' on %2 plane. Entering sketch mode.\n"
                                  "Use 'finish' to save or 'discard' to cancel.", sketchName, planeName);
        return r;
    }

    r.exitCode = 1;
    r.error = "Unknown type: " + type +
              "\nCurrently supported: sketch";
    return r;
}

CliResult CliEngine::cmdFinish()
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure("Not in sketch mode. Use 'create sketch' first.");
    }

    std::string sketchName = m_currentSketchName;

    hobbycad::Project* proj = project();
    if (!proj) {
        // Without a document there is nowhere to put it. Say so instead of
        // reporting a save that cannot happen.
        m_inSketchMode = false;
        m_currentSketchName.clear();
        m_pendingSketch = SketchData{};
        m_selectedEntityId = -1;   // entities die with their sketch
        return failure(subst(
            "Sketch '%1' was discarded: no document is open to save it to.", sketchName));
    }

    m_pendingSketch.name = sketchName;
    m_pendingSketch.plane = m_currentSketchPlane;
    m_pendingSketch.constructionPlaneId = m_currentConstructionPlaneId;
    const int entities = static_cast<int>(m_pendingSketch.entities.size());

    // Through the session, so the sketch gets its feature record and lands in
    // the history the GUI records into. It used to go straight into the
    // sketch list, which Full mode's own copy never saw.
    if (hobbycad::ProjectSession* s = session()) {
        hobbycad::SketchDraft draft;
        draft.sketch = m_pendingSketch;
        draft.sketch.id = -1;
        s->finishSketch(draft,
            subst("Create sketch '%1'", sketchName));
    } else {
        proj->addSketch(m_pendingSketch);
    }

    m_inSketchMode = false;
    m_currentSketchName.clear();
    m_pendingSketch = SketchData{};
    m_selectedEntityId = -1;   // entities die with their sketch

    if (m_docHost) m_docHost->hostDocumentChanged();

    r.output = subst("Saved sketch '%1' with %2 entity/entities.", sketchName, entities);
    return r;
}

CliResult CliEngine::cmdDiscard()
{
    CliResult r;

    if (!m_inSketchMode) {
        return failure("Not in sketch mode. Nothing to discard.");
    }

    std::string sketchName = m_currentSketchName;
    m_inSketchMode = false;
    m_currentSketchName.clear();

    // Decrement counter since we're discarding
    if (startsWith(sketchName, "Sketch") && m_sketchCounter > 0) {
        // Only decrement if it was an auto-named sketch
        bool ok = false;
        int num = toInt(sketchName.substr(6), &ok);
        if (ok && num == m_sketchCounter) {
            m_sketchCounter--;
        }
    }

    r.output = subst("Discarded sketch '%1'. Exiting sketch mode.", sketchName);
    return r;
}

// Thin std::string wrappers over the library's resolvers (sketch::resolveValue,
// resolveCoordinate, resolveCoordinate3): the parsing and the evaluation
// against the document's parameters live there; only the string type is the
// CLI's.
static bool parseValue(const std::string& str, double& value, std::string& expr,
                       const std::map<std::string, double>& params)
{
    std::string e;
    if (!sketch::resolveValue(str, params, value, &e)) return false;
    expr = (e);
    return true;
}

static bool parseCoord(const std::string& str, double& x, double& y,
                       const std::map<std::string, double>& params,
                       const std::map<std::string, std::array<double, 3>>& named = {},
                       std::string* xExpr = nullptr, std::string* yExpr = nullptr)
{
    std::string xe, ye;
    if (!sketch::resolveCoordinate(str, params, named, x, y,
                                   xExpr ? &xe : nullptr, yExpr ? &ye : nullptr)) return false;
    if (xExpr) *xExpr = (xe);
    if (yExpr) *yExpr = (ye);
    return true;
}

static bool parseCoord3(const std::string& str, double& x, double& y, double& z,
                        const std::map<std::string, double>& params,
                        const std::map<std::string, std::array<double, 3>>& named)
{
    return sketch::resolveCoordinate3(str, params, named, x, y, z);
}

namespace {

// A walk over one sketch command's arguments. Every token test used to be the
// same four lines in every cmdSketch* (test, exitCode, error, return); with
// the cursor a command reads as its grammar, and each error string stays the
// one the tests and the docs know.
class ArgCursor {
public:
    ArgCursor(const std::vector<std::string>& args,
              std::map<std::string, double> params,
              std::map<std::string, std::array<double, 3>> points)
        : m_args(args), m_params(std::move(params)), m_points(std::move(points)) {}

    int  index() const { return m_idx; }
    bool atEnd() const { return m_idx >= static_cast<int>(m_args.size()); }
    const std::string& current() const { return m_args[m_idx]; }
    void skip() { ++m_idx; }

    /// Consume `word` (case-insensitive) when it is the next token.
    bool accept(const char* word)
    {
        if (atEnd() || toLower(m_args[m_idx]) != std::string(word)) return false;
        ++m_idx;
        return true;
    }
    /// Require `word` next; otherwise record `error`.
    bool expect(const char* word, const std::string& error)
    {
        return accept(word) || fail(error);
    }
    /// An x,y coordinate; missing or unparsable records `error`.
    bool coord(double& x, double& y, const std::string& error)
    {
        if (atEnd() || !parseCoord(m_args[m_idx], x, y, m_params, m_points)) return fail(error);
        ++m_idx;
        return true;
    }
    /// A number, parameter or (expression); missing or unparsable records `error`.
    bool value(double& v, std::string& expr, const std::string& error)
    {
        if (atEnd() || !parseValue(m_args[m_idx], v, expr, m_params)) return fail(error);
        ++m_idx;
        return true;
    }
    bool fail(const std::string& error) { m_error = error; return false; }
    /// The recorded error as a command result.
    CliResult result() const { return failure(m_error); }

private:
    const std::vector<std::string>& m_args;
    std::map<std::string, double> m_params;
    std::map<std::string, std::array<double, 3>> m_points;
    int m_idx = 0;
    std::string m_error;
};

}  // namespace

hobbycad::ProjectSession* CliEngine::session()
{
    return m_docHost ? m_docHost->hostSession() : nullptr;
}

namespace {

/// The body operation named by `word`, when it names one.
bool bodyOperationWord(const std::string& word, hobbycad::BodyOperation& op)
{
    const std::string w = toLower(word);
    if (w == "new")       { op = hobbycad::BodyOperation::NewBody;   return true; }
    if (w == "join")      { op = hobbycad::BodyOperation::Join;      return true; }
    if (w == "cut")       { op = hobbycad::BodyOperation::Cut;       return true; }
    if (w == "intersect") { op = hobbycad::BodyOperation::Intersect; return true; }
    return false;
}

}  // namespace

// 3D from the command line. Aaron, 2026-09-15: "In reduced mode, The GUI CLI
// window should be able to manipulate the 3D Stuff that reduced mode can't."
// The work is the session's, so a headless run, Reduced mode's terminal and
// Full mode's dialog all build the same body.
CliResult CliEngine::cmdExtrude(const std::vector<std::string>& args)
{
    static const std::string usage = 
        "Usage: extrude <sketch> [distance] <value> [reverse|symmetric] [join|cut|intersect|new]\n"
        "\n"
        "Extrudes the sketch's first closed profile from its own plane.\n"
        "\n"
        "Examples:\n"
        "  extrude Sketch1 10\n"
        "  extrude Sketch1 distance height symmetric\n"
        "  extrude id=4 5 reverse cut";
    if (static_cast<int>(args.size()) < 2) return failure(usage);

    hobbycad::ProjectSession* s = session();
    if (!s || !project()) return noDocument();

    std::string why;
    const int at = resolveObjectIndex(static_cast<int>(hobbycad::ObjectKind::Sketch), args[0], &why);
    if (at < 0) return failure(why);
    const int sketchId = project()->sketches()[static_cast<size_t>(at)].id;
    const std::string sketchName = (project()->sketches()[static_cast<size_t>(at)].name);

    const std::vector<std::string> rest = slice(args, 1);
    ArgCursor cur(rest, parameterValues(), namedPointValues());
    cur.accept("distance");
    double distance = 0.0;
    std::string expr;
    if (!cur.value(distance, expr,
                   "The distance must be a number, a parameter or an expression.")) {
        return cur.result();
    }

    auto extent = hobbycad::ExtrudeExtent::Normal;
    auto op = hobbycad::BodyOperation::NewBody;
    for (; !cur.atEnd(); cur.skip()) {
        const std::string w = toLower(cur.current());
        if (w == "reverse")        extent = hobbycad::ExtrudeExtent::Reverse;
        else if (w == "symmetric") extent = hobbycad::ExtrudeExtent::Symmetric;
        else if (!bodyOperationWord(w, op)) {
            return failure(subst("Unknown option '%1'.\n\n%2", cur.current(), usage));
        }
    }

    const hobbycad::ModelResult res = s->extrudeSketch(sketchId, distance, extent, op,
        subst("Extrude sketch '%1'", sketchName));
    if (!res.ok) {
        return failure(subst("Cannot extrude '%1': %2.", sketchName, (res.error)));
    }
    if (m_docHost) m_docHost->hostDocumentChanged();

    CliResult r;
    r.output = subst("Extruded '%1' by %2 into body %3.", sketchName, distance, res.bodyId);
    return r;
}

CliResult CliEngine::cmdRevolve(const std::vector<std::string>& args)
{
    static const std::string usage = 
        "Usage: revolve <sketch> [angle <deg>] [about x|y|line <id>] [join|cut|intersect|new]\n"
        "\n"
        "Revolves the sketch's first closed profile; 360 degrees about the\n"
        "sketch's y axis unless told otherwise.\n"
        "\n"
        "Examples:\n"
        "  revolve Sketch1\n"
        "  revolve Sketch1 angle 90 about x\n"
        "  revolve Sketch1 about line 3 join";
    if (args.empty()) return failure(usage);

    hobbycad::ProjectSession* s = session();
    if (!s || !project()) return noDocument();

    std::string why;
    const int at = resolveObjectIndex(static_cast<int>(hobbycad::ObjectKind::Sketch), args[0], &why);
    if (at < 0) return failure(why);
    const int sketchId = project()->sketches()[static_cast<size_t>(at)].id;
    const std::string sketchName = (project()->sketches()[static_cast<size_t>(at)].name);

    const std::vector<std::string> rest = slice(args, 1);
    ArgCursor cur(rest, parameterValues(), namedPointValues());
    double angle = 360.0;
    auto axis = hobbycad::RevolveAxisKind::SketchYAxis;
    int lineId = -1;
    auto op = hobbycad::BodyOperation::NewBody;
    std::string expr;
    while (!cur.atEnd()) {
        if (cur.accept("angle")) {
            if (!cur.value(angle, expr, "The angle must be a number of degrees.")) {
                return cur.result();
            }
            continue;
        }
        if (cur.accept("about")) {
            if (cur.accept("x")) {
                axis = hobbycad::RevolveAxisKind::SketchXAxis;
            } else if (cur.accept("y")) {
                axis = hobbycad::RevolveAxisKind::SketchYAxis;
            } else if (cur.accept("line")) {
                double id = 0.0;
                if (!cur.value(id, expr, "Give the line's entity id after 'line'.")) {
                    return cur.result();
                }
                axis = hobbycad::RevolveAxisKind::SketchLine;
                lineId = static_cast<int>(std::lround(id));
            } else {
                return failure("After 'about', give x, y or line <id>.");
            }
            continue;
        }
        if (!bodyOperationWord(cur.current(), op)) {
            return failure(subst("Unknown option '%1'.\n\n%2", cur.current(), usage));
        }
        cur.skip();
    }

    const hobbycad::ModelResult res = s->revolveSketch(sketchId, angle, axis, lineId, op,
        subst("Revolve sketch '%1'", sketchName));
    if (!res.ok) {
        return failure(subst("Cannot revolve '%1': %2.", sketchName, (res.error)));
    }
    if (m_docHost) m_docHost->hostDocumentChanged();

    CliResult r;
    r.output = subst("Revolved '%1' by %2 degrees into body %3.", sketchName, angle, res.bodyId);
    return r;
}

int CliEngine::addPendingEntity(hobbycad::SketchEntityData entity)
{
    // Highest existing id plus one, rather than a running counter: the
    // pending sketch is thrown away and rebuilt by discard/finish, and a
    // counter living outside it would survive that reset and hand out ids
    // with gaps or, worse, keep counting into the next sketch.
    const int next = sketch::nextFreeEntityId(m_pendingSketch.entities);
    entity.id = next;
    m_pendingSketch.entities.push_back(std::move(entity));
    return next;
}

bool CliEngine::takeConstructionFlag(std::vector<std::string>& args)
{
    if (!args.empty() &&
        toLower(args.back()) == "construction") {
        args.pop_back();
        return true;
    }
    return false;
}

CliResult CliEngine::cmdSketchPoint(std::vector<std::string> args)
{
    const bool construction = takeConstructionFlag(args);

    if (args.empty()) {
        return failure(
            "Usage: point [at] <x>,<y>\n"
            "\n"
            "Examples:\n"
            "  point at 10,20\n"
            "  point 10,20");
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    // "at" keyword is optional
    if (cur.accept("at") && cur.atEnd()) return failure("Missing coordinates after 'at'");

    double x, y;
    if (!cur.coord(x, y, "Invalid coordinates. Use format: x,y (e.g., 10,20)")) return cur.result();

    auto e = makeEntity(sketch::EntityType::Point);
    e.points = { {x, y} };
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    CliResult r;
    r.output = subst("Created point at (%1, %2) [id %3]", x, y, id);
    return r;
}

CliResult CliEngine::cmdSketchLine(std::vector<std::string> args)
{
    return sketchTwoPoints(std::move(args), sketch::EntityType::Line,
        
            "Usage: line [from] <x1>,<y1> to <x2>,<y2>\n"
            "\n"
            "Examples:\n"
            "  line from 0,0 to 100,50\n"
            "  line 0,0 to 100,50",
        "Missing 'to' keyword or end coordinates",
        "Invalid start coordinates. Use format: x,y",
        "Invalid end coordinates. Use format: x,y",
        "Created line from (%1, %2) to (%3, %4) [id %5]");
}

CliResult CliEngine::cmdSketchCircle(std::vector<std::string> args)
{
    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 3) {
        return failure(
            "Usage: circle [at] <x>,<y> radius|diameter <value>\n"
            "\n"
            "Examples:\n"
            "  circle at 50,50 radius 25\n"
            "  circle 50,50 radius 25\n"
            "  circle 100,100 diameter 60");
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    // "at" keyword is optional
    if (cur.accept("at") && static_cast<int>(args.size()) < 4) return failure("Missing arguments after 'at'");

    double cx, cy;
    if (!cur.coord(cx, cy, "Invalid center coordinates. Use format: x,y")) return cur.result();

    const std::string sizeType = toLower(cur.current());
    if (sizeType != "radius" && sizeType != "diameter") {
        return failure("Size type must be 'radius' or 'diameter'");
    }
    cur.skip();
    if (cur.atEnd()) return failure("Missing size value");

    double value;
    std::string valueExpr;
    if (!cur.value(value, valueExpr, "Invalid size value. Must be a number, parameter, or (expression).")) return cur.result();

    // For now, only validate if it's a plain number
    bool isPlainNumber = (valueExpr == trim(args[cur.index() - 1]));
    if (isPlainNumber && !geometry::isPositiveLength(value)) return failure("Size value must be positive.");

    double radius = (sizeType == "diameter") ? value / 2.0 : value;
    std::string radiusExpr = (sizeType == "diameter")
        ? subst("(%1)/2", valueExpr): valueExpr;

    auto e = makeEntity(sketch::EntityType::Circle);
    e.points = { {cx, cy} };
    e.radius = radius;
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    CliResult r;
    r.output = subst("Created circle at (%1, %2) with radius %3 [id %4]", cx, cy, isPlainNumber ? numToString(radius) : radiusExpr, id);
    return r;
}

// "set [<id>] <property> <value>": one property of an entity in the sketch
// being edited, by the names the properties panel uses. The fields, their
// locks and the edit rules are the library's (sketch/property_schema.h,
// sketch/properties.h), so the panel and this command agree.
CliResult CliEngine::cmdSet(std::vector<std::string> args)
{
    const std::string usage =
        "Usage: set [<id>] <property> <value>\n"
        "Without an id, the selected entity is changed. \"set <id>\" alone\n"
        "lists the entity's properties.\n"
        "\n"
        "Examples:\n"
        "  set 3 radius 12.5\n"
        "  set point0 10,20\n"
        "  set 4 text Hello";

    int id = m_selectedEntityId;
    std::size_t at = 0;
    bool isId = false;
    if (!args.empty()) {
        const int n = toInt(args[0], &isId);
        if (isId) {
            id = n;
            at = 1;
        }
    }
    if (args.empty() && id < 0) return failure(usage);
    if (id < 0) {
        return failure("Nothing selected. \"select <id>\" an entity first, "
                       "or name one: set <id> <property> <value>.");
    }
    sketch::Entity* entity = sketch::findEntityById(m_pendingSketch.entities, id);
    if (!entity) {
        return failure(subst("No entity %1 in this sketch. \"print\" lists them.", id));
    }

    const std::vector<sketch::PropertyField> fields = sketch::entityGeometryFields(*entity);
    const auto shown = [&](const sketch::PropertyField& f) {
        if (f.kind == sketch::FieldKind::Point) {
            const auto& p = entity->points[static_cast<std::size_t>(f.pointIndex)];
            return subst("%1,%2", p.x, p.y);
        }
        if (f.kind == sketch::FieldKind::Text) return entity->text;
        return numToString(sketch::fieldNumber(*entity, f));
    };

    // No property: list what can be set.
    if (at >= args.size()) {
        std::vector<std::string> out;
        out.push_back(subst("%1 %2", entityName(entity->type), id));
        for (const sketch::PropertyField& f : fields) {
            if (f.key.empty()) continue;
            const bool locked = sketch::fieldLocked(*entity, f, m_pendingSketch.constraints);
            out.push_back(subst("  %1 %2%3", pad(f.key, -14), shown(f),
                                locked ? std::string("   (read-only)") : std::string()));
        }
        CliResult r;
        r.output = join(out, '\n');
        return r;
    }

    const std::string property = args[at];
    const sketch::PropertyField* field = nullptr;
    std::vector<std::string> names;
    for (const sketch::PropertyField& f : fields) {
        if (f.key.empty()) continue;
        names.push_back(f.key);
        if (equalsIgnoreCase(f.key, property)) field = &f;
    }
    if (!field) {
        return failure(subst("A %1 has no property '%2'. It has: %3.",
                             entityName(entity->type), property,
                             join(names, ", ")));
    }
    if (sketch::fieldLocked(*entity, *field, m_pendingSketch.constraints)) {
        std::string why = "it is read-only here";
        if (entity->projectionSourceId >= 0) {
            why = "it is projected from another sketch; change the source";
        } else if (field->editable) {
            why = "a dimension drives it; change the dimension";
        }
        return failure(subst("'%1' cannot be set: %2.", field->key, why));
    }
    if (at + 1 >= args.size()) return failure(usage);

    const std::vector<std::string> rest(args.begin() + static_cast<long>(at) + 1, args.end());
    sketch::PropertyEdit edit;
    if (field->kind == sketch::FieldKind::Text) {
        edit = sketch::setEntityText(*entity, join(rest, " "));
    } else {
        ArgCursor cur(rest, parameterValues(), namedPointValues());
        if (field->kind == sketch::FieldKind::Point) {
            double x = 0, y = 0;
            if (!cur.coord(x, y, "Invalid point. Use format: x,y")) return cur.result();
            edit = sketch::setEntityPoint(*entity, field->pointIndex, Point2D(x, y));
        } else {
            double v = 0;
            std::string expr;
            if (!cur.value(v, expr,
                           "Invalid value. Must be a number, parameter, or (expression).")) {
                return cur.result();
            }
            edit = sketch::setEntityNumber(*entity, field->key, v);
        }
        if (!cur.atEnd()) return failure(usage);
    }

    switch (edit.problem) {
    case sketch::PropertyProblem::None:
        break;
    case sketch::PropertyProblem::NotPositive:
        return failure(subst("'%1' must be a positive number.", field->key));
    case sketch::PropertyProblem::OutOfRange:
        return failure(subst("'%1' is out of range (a polygon has 3 to 100 sides).", field->key));
    case sketch::PropertyProblem::Degenerate:
        return failure(subst("'%1' cannot be set on a zero-length entity.", field->key));
    case sketch::PropertyProblem::NotANumber:
    case sketch::PropertyProblem::UnknownProperty:
    case sketch::PropertyProblem::NoSuchPoint:
        return failure(subst("'%1' could not be set.", field->key));
    }

    CliResult r;
    r.output = subst("Set %1 of entity %2 to %3. \"solve\" re-applies the constraints.",
                     field->key, id, shown(*field));
    return r;
}

CliResult CliEngine::cmdSketchRectangle(std::vector<std::string> args)
{
    return sketchTwoPoints(std::move(args), sketch::EntityType::Rectangle,
        
            "Usage: rectangle [from] <x1>,<y1> to <x2>,<y2>\n"
            "\n"
            "Examples:\n"
            "  rectangle from 0,0 to 100,50\n"
            "  rectangle 0,0 to 100,50",
        "Missing 'to' keyword or second corner coordinates",
        "Invalid first corner coordinates. Use format: x,y",
        "Invalid second corner coordinates. Use format: x,y",
        "Created rectangle from (%1, %2) to (%3, %4) [id %5]");
}

// "<name> [from] <x1>,<y1> to <x2>,<y2>": the shape line and rectangle share.
CliResult CliEngine::sketchTwoPoints(std::vector<std::string> args, sketch::EntityType type, const std::string& usage,
                                     const std::string& missingTo, const std::string& badFirst,
                                     const std::string& badSecond, const std::string& createdFormat)
{
    const bool construction = takeConstructionFlag(args);
    if (static_cast<int>(args.size()) < 3) return failure(usage);

    // "from" keyword is optional
    int idx = 0;
    if (toLower(args[0]) == "from") idx = 1;

    // Find "to" keyword
    int toIdx = -1;
    for (int i = idx; i < static_cast<int>(args.size()); ++i) {
        if (toLower(args[i]) == "to") { toIdx = i; break; }
    }
    if (toIdx < 0 || toIdx <= idx || toIdx + 1 >= static_cast<int>(args.size())) return failure(missingTo);

    double x1, y1, x2, y2;
    if (!parseCoord(args[idx], x1, y1, parameterValues(), namedPointValues())) return failure(badFirst);
    if (!parseCoord(args[toIdx + 1], x2, y2, parameterValues(), namedPointValues())) return failure(badSecond);

    auto e = makeEntity(type);
    e.points = { {x1, y1}, {x2, y2} };
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    CliResult r;
    r.output = subst(createdFormat, x1, y1, x2, y2, id);
    return r;
}

CliResult CliEngine::cmdSketchArc(std::vector<std::string> args)
{
    CliResult r;
    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 6) {
        return failure(
            "Usage: arc [at] <x>,<y> radius <r> [angle] <start> to <end>\n"
            "\n"
            "Examples:\n"
            "  arc at 50,50 radius 30 angle 0 to 90\n"
            "  arc 50,50 radius 30 0 to 90");
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("at");   // optional

    double cx, cy;
    if (!cur.coord(cx, cy, "Invalid center coordinates. Use format: x,y")) return cur.result();
    if (!cur.expect("radius", "Expected 'radius' keyword")) return cur.result();
    if (cur.atEnd()) return failure("Missing radius value");

    double radius;
    std::string radiusExpr;
    if (!cur.value(radius, radiusExpr, "Invalid radius. Must be a number, parameter, or (expression).")) return cur.result();

    // "angle" and "from" are optional connective words before the start
    // angle; the documented form is "radius <r> from <deg> to <deg>".
    cur.accept("angle");
    cur.accept("from");
    if (cur.atEnd()) return failure("Missing start angle");

    double startAngle;
    std::string startExpr;
    if (!cur.value(startAngle, startExpr, "Invalid start angle. Must be a number, parameter, or (expression).")) return cur.result();
    if (!cur.expect("to", "Expected 'to' keyword")) return cur.result();
    if (cur.atEnd()) return failure("Missing end angle");

    double endAngle;
    std::string endExpr;
    if (!cur.value(endAngle, endExpr, "Invalid end angle. Must be a number, parameter, or (expression).")) return cur.result();

    if (!geometry::isPositiveLength(radius)) {   // zero at the length precision
        return failure("Radius must be greater than zero.");
    }

    const double arcSweep = endAngle - startAngle;
    if (geometry::isZeroAngleDeg(arcSweep)) {   // zero at the angle precision
        return failure(subst(
            "Start and end angle are both %1, so there is no arc between "
            "them. Use 'point' for a single position.", startAngle));
    }
    if (std::abs(arcSweep) > 360.0) {
        r.exitCode = 1;
        r.error = subst(
            "A sweep of %1%2 is more than a full turn; an arc cannot wrap "
            "over itself. Use a value between -360 and 360.", arcSweep, ("\xc2\xb0"));
        return r;
    }

    auto e = makeEntity(sketch::EntityType::Arc);
    // Center, START and END: three points, which is what the solver and
    // the GUI both expect of an arc. Storing only the center left
    // CLI-created arcs unsolvable: the solver could not build them, so a
    // Radius constraint on one reported success and moved nothing.
    // Entity::toArc() reads the angle fields, the solver reads the points,
    // and both have to agree.
    {
        geometry::Arc arc;
        arc.center = {cx, cy};
        arc.radius = radius;
        arc.startAngle = startAngle;
        arc.sweepAngle = arcSweep;
        e.points = { arc.center, arc.startPoint(), arc.endPoint() };
    }
    e.radius = radius;
    e.startAngle = startAngle;
    e.sweepAngle = arcSweep;
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst("Created arc at (%1, %2) with radius %3 from %4° to %5° [id %6]", cx, cy, radiusExpr, startExpr, endExpr, id);
    if (std::abs(arcSweep) == 360.0) {
        // Allowed, because Aaron says it has uses: "I could see a 360
        // degree arc being useful, but it would be a real edge case." Its
        // ends coincide, so anything walking the sketch has to cope with a
        // closed arc; worth saying so once rather than refusing it.
        r.output += 
            "\n(a full turn: the ends meet, so this closes into a circle. "
            "\"circle\" is the usual way to say it.)";
    }
    return r;
}

// ---- Additional sketch entities --------------------------------------
//
//  Aaron, 2026-08-26: "every sketch entity should be reachable from the
//  CLI". These five complete that: polygon, ellipse, slot, spline and
//  text were drawable in the GUI and had no CLI route at all.
//
//  Two of them are not fully solvable yet, and the commands say so rather
//  than letting a person find out later. See tests/solver/ and TODO
//  section 7: a regular polygon's radius and rotation, and an ellipse's
//  axes, have no libslvs primitive, so the solver cannot constrain them.
//  The geometry is real and saves correctly; only constraining is limited.

CliResult CliEngine::cmdSketchPolygon(std::vector<std::string> args)
{
    CliResult r;
    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 5) {
        return failure(
            "Usage: polygon [at] <x>,<y> radius <r> sides <n> [construction]\n"
            "\n"
            "Examples:\n"
            "  polygon at 0,0 radius 25 sides 6\n"
            "  polygon 50,50 radius (width/2) sides 8 construction");
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("at");

    double cx, cy;
    if (!cur.coord(cx, cy, "Invalid center coordinates. Use format: x,y")) return cur.result();
    if (!cur.expect("radius", "Expected 'radius' keyword")) return cur.result();

    double radius = 0.0;
    std::string radiusExpr;
    if (!cur.value(radius, radiusExpr, "Invalid radius. Must be a number, parameter, or (expression).")) return cur.result();
    if (!geometry::isPositiveLength(radius)) return failure("Radius must be greater than zero.");
    if (!cur.expect("sides", "Expected 'sides' keyword")) return cur.result();

    double sidesValue = 0.0;
    std::string sidesExpr;
    if (!cur.value(sidesValue, sidesExpr, "Invalid side count. Must be a number, parameter, or (expression).")) return cur.result();

    // A side count is a count, not a measurement. Rejecting 5.5 outright is
    // kinder than silently truncating it to 5 and drawing something the
    // person did not ask for.
    const int sides = static_cast<int>(sidesValue);
    if (static_cast<double>(sides) != sidesValue) {
        return failure(subst("Side count must be a whole number, not %1.", sidesValue));
    }
    if (sides < 3) {
        return failure(subst("A polygon needs at least 3 sides, not %1.", sides));
    }

    auto e = makeEntity(sketch::EntityType::Polygon);
    e.points = { {cx, cy} };
    e.radius = radius;
    e.sides = sides;
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst("Created %1-sided polygon at (%2, %3) with radius %4 [id %5]", sides, cx, cy, radiusExpr, id);
    return r;
}

CliResult CliEngine::cmdSketchEllipse(std::vector<std::string> args)
{
    CliResult r;
    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 5) {
        return failure(translate("QObject",
            "Usage: ellipse [at] <x>,<y> major <a> minor <b>\n"
            "                [rotation <deg>] [angle <start> to <end>]\n"
            "                [construction]\n"
            "\n"
            "Examples:\n"
            "  ellipse at 0,0 major 40 minor 20\n"
            "  ellipse 10,10 major (width/2) minor 15\n"
            "  ellipse at 0,0 major 40 minor 20 rotation 30\n"
            "  ellipse at 0,0 major 40 minor 20 angle 0 to 90"));
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("at");

    double cx, cy;
    if (!cur.coord(cx, cy, "Invalid center coordinates. Use format: x,y")) return cur.result();
    if (!cur.expect("major", "Expected 'major' keyword")) return cur.result();

    double major = 0.0;
    std::string majorExpr;
    if (!cur.value(major, majorExpr, "Invalid major radius. Must be a number, parameter, or (expression).")) return cur.result();
    if (!cur.expect("minor", "Expected 'minor' keyword")) return cur.result();

    double minor = 0.0;
    std::string minorExpr;
    if (!cur.value(minor, minorExpr, "Invalid minor radius. Must be a number, parameter, or (expression).")) return cur.result();

    if (!geometry::isPositiveLength(major) || !geometry::isPositiveLength(minor)) {
        return failure("Both radii must be greater than zero.");
    }

    // Optional: the major axis angle, and an arc range. The arc range uses
    // the arc command's own "angle <start> to <end>" wording rather than a
    // new one, and the angles are parameters in the ellipse's own frame,
    // the same convention the renderer, DXF and projection already use.
    double rotation = 0.0;
    std::string rotationExpr;
    if (cur.accept("rotation")) {
        if (!cur.value(rotation, rotationExpr,
                       translate("QObject", "Invalid rotation. Must be a number, parameter, "
                                            "or (expression)."))) {
            return cur.result();
        }
    }

    double startAngle = 0.0;
    double endAngle = 360.0;
    bool partial = false;
    if (cur.accept("angle")) {
        std::string tmp;
        if (!cur.value(startAngle, tmp,
                       translate("QObject", "Invalid start angle. Must be a number, "
                                            "parameter, or (expression)."))) {
            return cur.result();
        }
        if (!cur.expect("to", translate("QObject",
                                        "Expected 'to' between the start and end angles"))) {
            return cur.result();
        }
        if (!cur.value(endAngle, tmp,
                       translate("QObject", "Invalid end angle. Must be a number, "
                                            "parameter, or (expression)."))) {
            return cur.result();
        }
        if (geometry::isZeroAngleDeg(endAngle - startAngle)) {
            return failure(translate("QObject", "An elliptical arc needs a non-zero sweep."));
        }
        partial = true;
    }

    // Naming, not geometry: the fields mean "major" and "minor", so an
    // ellipse whose minor exceeds its major is mislabelled rather than
    // impossible. Swapping silently would contradict what was typed, so
    // say what happened.
    std::string note;
    if (minor > major) {
        std::swap(major, minor);
        std::swap(majorExpr, minorExpr);
        // Swapping the axes turns the frame a quarter turn, and the
        // parameter origin moves with it, so the SHAPE has to be turned
        // back or the swap would silently redraw what was typed.
        rotation += 90.0;
        startAngle -= 90.0;
        endAngle -= 90.0;
        note = 
            "\nNote: minor radius exceeded major, so they were swapped.";
    }

    auto e = makeEntity(sketch::EntityType::Ellipse);
    e.points = { {cx, cy} };
    e.majorRadius = major;
    e.minorRadius = minor;
    e.ellipseRotation = rotation;
    if (partial) {
        e.ellipseStart = startAngle;
        e.ellipseSweep = endAngle - startAngle;
    }
    // The axes are solver geometry, so they need their points. Last, because
    // the points are computed from the radii and the rotation set above.
    sketch::ensureEllipseAxisPoints(e);
    // Several places size an entity from `radius`; leaving it zero makes an
    // ellipse look degenerate to anything that reads the shared field.
    e.radius = major;
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst("Created ellipse at (%1, %2) major %3 minor %4 [id %5]%6", cx, cy, majorExpr, minorExpr, id, note);
    return r;
}

namespace {

/// Format a MAXIMUM for display, rounded DOWN to the shown precision.
///
/// Rounding to nearest can print a number larger than the real limit, and
/// the obvious thing to do with a printed limit is to type it back in,
/// which then gets refused, by a value the program itself supplied.
std::string maxText(double v, int decimals = 4)
{
    const double f = std::pow(10.0, decimals);
    return numToStringFixed(std::floor(v * f) / f, decimals);
}

}  // namespace

std::string CliEngine::slotUsage() const
{
    // Aaron, 2026-08-28: "Essentially this is a 2D arc sweep." That is the
    // clearest way to hold it, and it settles the naming: a slot is a round
    // profile swept along a path, so "radius" belongs to the PATH and
    // "width" is the diameter of the profile. Linear and arc slots differ
    // only in what the path is.
    return 
        "A slot is a round profile swept along a path: a line, or an arc.\n"
        "\n"
        "Usage: slot along <entity id> width <w>\n"
        "       slot [from] <x>,<y> to <x>,<y> width <w>\n"
        "       slot arc [at] <x>,<y> radius <r> from <x>,<y> to <x>,<y> "
        "width <w> [long]\n"
        "       slot arc [at] <x>,<y> radius <r> angle <a> to <b> width <w>\n"
        "\n"
        "Examples:\n"
        "  slot along 3 width 10\n"
        "  slot from 0,0 to 50,0 width 10\n"
        "  slot arc at 0,0 radius 30 from 30,0 to 0,30 width 8\n"
        "  slot arc at 0,0 radius 30 angle 0 to 90 width 8\n"
        "  slot arc at 0,0 radius 30 angle 0 to 270 width 8\n"
        "\n"
        "ALONG: sweeps an existing line or arc; draw the centerline,\n"
        "then thicken it. The path is kept and marked as construction\n"
        "geometry, because that is what it has become: the slot's\n"
        "centerline, not an edge of it.\n"
        "\n"
        "LINEAR: the two points are the centers of the round ends.\n"
        "\n"
        "ARC: the center is the arc's center, \"radius\" reaches out to the\n"
        "slot's centerline, and the two points are the centers of the round\n"
        "ends. Those points give DIRECTION only: each is placed on the\n"
        "circle of that radius, so the two ends cannot end up at different\n"
        "distances from the center. An angle range says the same thing.\n"
        "\n"
        "\"width\" (or \"thickness\") is the full width of the slot, in both\n"
        "forms. The linear form also accepts \"radius\" for the HALF-width,\n"
        "which is what earlier scripts were written against.\n"
        "\n"
        "Two points cannot say which way round the arc runs, so \"long\"\n"
        "takes the long way. An angle range of more than 180 degrees says\n"
        "it without the keyword.";
}

namespace {

/// The old half-width spelled as the width it now means.
///
/// Showing the number the person typed would be telling them to write the
/// same thing again; the point of the message is the doubling.
std::string doubledHint(const std::string& typed)
{
    bool ok = false;
    const double v = toDouble(typed, &ok);
    return ok ? numToString(v * 2.0) : "<w>";
}

}  // namespace

CliResult CliEngine::cmdSketchSlot(std::vector<std::string> args)
{
    CliResult r;
    const bool construction = takeConstructionFlag(args);

    if (!args.empty() && toLower(args[0]) == "arc") {
        args.erase(args.begin());
        return sketchArcSlot(args, construction);
    }

    // Aaron, 2026-08-28: "You could also frame an arc slot by providing it
    // an existing arc, and then sweep across it a certain thickness. Same
    // for a regular line slot."
    //
    // That is the sweep framing taken all the way: the path is not typed
    // out at all, it is geometry already in the sketch. Draw the
    // centerline, then thicken it. A line gives a straight slot and an arc
    // gives an arc one, from the same command, because the difference
    // between the two slots IS the difference between the two paths.
    if (!args.empty() && toLower(args[0]) == "along") {
        args.erase(args.begin());
        return sketchSlotAlong(args, construction);
    }

    if (static_cast<int>(args.size()) < 5) return failure(slotUsage());

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("from");

    double x1, y1;
    if (!cur.coord(x1, y1, "Invalid first center. Use format: x,y")) return cur.result();
    if (!cur.expect("to", "Expected 'to' keyword")) return cur.result();

    double x2 = 0.0, y2 = 0.0;
    if (!cur.coord(x2, y2, "Invalid second center. Use format: x,y")) return cur.result();

    // "width" is the full width a person measures; the model stores half.
    // "radius" is the older spelling and means the HALF-width, kept
    // because scripts were written against it, but width is what the docs
    // lead with.
    if (cur.atEnd()) return failure("Expected 'width' or 'radius' keyword");
    // A slot is one shape swept along a path, so the same word has to mean
    // the same thing in both forms. "radius" was briefly accepted here for
    // the HALF-width, while in the arc form it means the radius of the
    // path: two different quantities under one word. Rejected outright
    // rather than kept as an alias: nothing outside this session was ever
    // written against it, and an ambiguous spelling is worse than a
    // missing one.
    std::string sizeWord = toLower(cur.current());
    if (sizeWord == "thickness") sizeWord = "width";
    if (sizeWord == "radius") {
        return failure(subst(
            "A linear slot takes 'width' (or 'thickness'), not 'radius'.\n"
            "A slot is a round profile swept along a path, and 'radius' is\n"
            "the radius of that PATH, which a straight slot does not have.\n"
            "Its old meaning here was the HALF-width, so double it: "
            "\"width %1\".", doubledHint(valueAt(args, cur.index() + 1))));
    }
    if (sizeWord != "width") {
        return failure(subst(
            "Expected 'width' (or 'thickness'), not '%1'", cur.current()));
    }
    cur.skip();

    double sizeValue = 0.0;
    std::string sizeExpr;
    if (!cur.value(sizeValue, sizeExpr, subst(
            "Invalid %1. Must be a number, parameter, or (expression).", sizeWord))) {
        return cur.result();
    }
    if (!geometry::isPositiveLength(sizeValue)) return failure(subst("%1 must be greater than zero.", sizeWord));
    const double halfWidth = sizeValue / 2.0;

    if (x1 == x2 && y1 == y2) {
        r.exitCode = 1;
        r.error = subst(
            "A slot needs two different centers; both are (%1, %2). "
            "Use 'circle' for a round hole.", x1, y1);
        return r;
    }

    auto e = makeEntity(sketch::EntityType::Slot);
    e.points = { {x1, y1}, {x2, y2} };
    e.radius = halfWidth;
    e.isConstruction = construction;

    // Every slot gets its bones, however it was described. A slot is a
    // profile swept along a path, so the path is part of what it IS, and
    // without one a typed slot could not be dimensioned or follow anything,
    // while a "slot along" one could. Two routes to the same shape gave two
    // different structures.
    const int pathId = addSlotCenterline(e, 0.0, 0.0, 0.0);
    const int id = addPendingEntity(e);
    const int groupId = groupSlotWithPath(id, pathId);

    r.output = subst("Created slot from (%1, %2) to (%3, %4) width %5 [id %6]", x1, y1, x2, y2, (hobbycad::formatValueWithUnit(
                            halfWidth * 2.0, hobbycad::LengthUnit::Millimeters)), id);
    r.output += subst(
        "\n(centerline [id %1] added as construction, grouped as \"Slot %2\" "
        "[group %3])", pathId, id, groupId);
    return r;
}

int CliEngine::addSlotCenterline(hobbycad::SketchEntityData& slot,
                                 double pathRadius, double startAngle,
                                 double sweep)
{
    // Aaron, 2026-08-28: "So the group can't have the construction line
    // added."
    //
    // It could not, and that was the inconsistency: "slot along" produced a
    // slot WITH a centerline and a group, while the typed forms produced a
    // bare slot with neither. Two ways to make the same thing, two
    // different structures, and only one of them could be dimensioned or
    // could follow anything.
    //
    // So the typed forms build the centerline too. Every slot now has its
    // bones: a construction line or arc, the slot, and a group naming them
    // as one thing.
    if (static_cast<int>(slot.points.size()) < 2) return -1;
    // Three points for the arc, for the same reason as cmdSketchArc: an arc with
    // only a center cannot be solved, and this one exists to be constrained.
    hobbycad::SketchEntityData path =
        sketch::makeSlotCenterline(slot, pathRadius, startAngle, sweep);

    // The centerline goes in FIRST, so its id is lower than the slot's and
    // the export emits it before the slot that refers to it.
    const int pathId = addPendingEntity(path);
    slot.pathEntityIds = { pathId };
    return pathId;
}

// Aaron asked whether Points should be added at the path's ends and put in
// the group, so the slot could inherit a fixed position from them. Tried,
// and NOT kept: an arc's endpoints are already addressable. FixedPoint
// resolves through pointIndices (getPointHandle(entityIds[0],
// pointIndices[0])), so "constrain fixedpoint 1.1" pins the arc's start
// directly, and with both ends pinned the arc holds its radius and sweep.
//
// Separate Point entities would have duplicated points the solver already
// has, at two entities and one constraint per slot, and given two places
// where the same position lives.

int CliEngine::groupSlotWithPath(int slotId, int pathId)
{
    // The slot's group (kind Slot, "Slot <slot id>") through addGroup, so
    // the members point back at it as the canvas expects.
    const sketch::AddGroupResult added = sketch::addGroup(
        sketch::makeSlotGroup(0, slotId, {pathId}),
        m_pendingSketch.entities, m_pendingSketch.constraints, m_pendingSketch.groups);
    return added.problem == sketch::AddGroupProblem::None ? added.id : -1;
}

CliResult CliEngine::sketchSlotAlong(const std::vector<std::string>& args, bool construction)
{
    CliResult r;

    if (static_cast<int>(args.size()) < 3) {
        return failure(slotUsage());
    }

    bool ok = false;
    const int sourceId = toInt(args[0], &ok);
    if (!ok) {
        return failure(subst(
            "'%1' is not an entity id. \"slot along\" takes the id of a line "
            "or an arc to sweep along.", args[0]));
    }

    const hobbycad::SketchEntityData* src = pendingEntity(sourceId);
    // Reset to null before the vector can move; see below.
    if (!src) {
        return failure(subst("No entity with id %1 in this sketch.", sourceId));
    }

    int idx = 1;
    const std::string sizeWord = toLower(args[idx]);
    if (sizeWord != "width" &&
        sizeWord != "thickness") {
        return failure(subst(
            "Expected 'width' (or 'thickness'), not '%1'", args[idx]));
    }
    idx++;

    double width = 0.0;
    std::string widthExpr;
    if (idx >= static_cast<int>(args.size()) ||
        !parseValue(args[idx], width, widthExpr, parameterValues())) {
        return failure("Invalid width.");
    }
    idx++;
    if (idx < static_cast<int>(args.size())) {
        return failure(subst("Unexpected '%1'.", args[idx]));
    }

    if (!sketch::slotWidthIsPositive(width)) {   // zero at the length precision
        return failure("Width must be greater than zero.");
    }
    const double halfWidth = width / 2.0;

    auto e = makeEntity(sketch::EntityType::Slot);
    std::string what;

    switch (src->type) {
    case sketch::EntityType::Line: {
        if (static_cast<int>(src->points.size()) < 2) {
            return failure(subst("Entity %1 has no endpoints to sweep along.", sourceId));
        }
        // The line's endpoints ARE the centers of the round ends: sweeping
        // a circle along a segment puts its center at each end.
        e.points = { src->points[0], src->points[1] };
        e.radius = halfWidth;
        what = "line";
        break;
    }
    case sketch::EntityType::Arc: {
        if (src->points.empty()) {
            return failure(subst("Entity %1 has no center to sweep around.", sourceId));
        }
        // Equality is the limit case and is allowed; see the typed form.
        if (halfWidth > src->radius) {
            return failure(subst(
                "A width of %1 does not fit on an arc of radius %2: the "
                "inner edge would pass through the center. Width can be at "
                "most %3.", width, src->radius, src->radius * 2.0));
        }

        const geometry::Arc arc = src->toArc();
        e.points = { arc.center, arc.startPoint(), arc.endPoint() };
        e.radius = halfWidth;

        // Same cap-collision limit as the typed forms. An arc can sweep a
        // full turn; a slot swept along it cannot.
        const double maxSweep =
            sketch::maxArcSlotSweepDegrees(src->radius, halfWidth);
        if (std::abs(src->sweepAngle) > maxSweep) {
            return failure(subst(
                "Entity %1 sweeps %2%5, which is too far round for a slot "
                "%3 wide: the end caps would overlap.\n"
                "The most this slot can sweep is %4%5. Use a narrower slot, "
                "or a shorter arc.", sourceId, src->sweepAngle, width, maxText(maxSweep, 1), ("\xc2\xb0")));
        }

        // Two cap centers cannot say which way round; the arc's own sweep
        // can, so carry it across rather than losing it.
        e.arcFlipped = std::abs(src->sweepAngle) > 180.0;
        what = "arc";
        break;
    }
    default:
        return failure(subst(
            "Entity %1 is not a line or an arc, so there is no path to sweep "
            "along. A slot follows one or the other.", sourceId));
    }

    e.isConstruction = construction;

    // The link back to the path, so the slot can be re-derived when the
    // path moves. Without it a slot is a copy taken at one moment.
    e.pathEntityIds = { sourceId };

    // Aaron, 2026-08-28: "If you are using an existing arc, for the slot,
    // just change it to have the construction property."
    //
    // That is what the path has BECOME: once it is the centerline of a
    // slot it is a guide, not an edge of the profile. Leaving it as real
    // geometry puts a stray line down the middle of the slot; deleting it
    // would throw away the thing the slot is dimensioned against. Marking
    // it construction is the answer that keeps both. It also matches the
    // GUI, which materializes construction geometry as part of the entity.
    const bool converted = !src->isConstruction;
    if (converted) {
        if (auto* path = sketch::findEntityById(m_pendingSketch.entities, sourceId)) path->isConstruction = true;
    }
    // src points into that vector and addPendingEntity() may reallocate it,
    // so nothing may read it past this point.
    src = nullptr;

    const int id = addPendingEntity(e);

    // Aaron, 2026-08-28: "The base arc that gets turned into construction
    // becomes the bones/foundation for the arc slot. Maybe groups can help
    // with this too?"
    //
    // They can, and this is what a group is for: the centerline and the
    // slot are one thing built from two records, and nothing else says so.
    // The canvas already uses exactly this shape (an entity plus its
    // construction geometry in a named group) for sweep-angle dimensions.
    const int groupId = groupSlotWithPath(id, sourceId);
    hobbycad::sketch::Group g;
    g.id = groupId;
    g.name = sketch::slotGroupName(id);

    r.output = subst("Created slot along %1 %2, width %3 [id %4]", what, sourceId, widthExpr, id);
    r.output += subst("\n(grouped with its centerline as \"%1\" "
                               "[group %2])", (g.name), g.id);
    if (converted) {
        r.output += subst(
            "\n(entity %1 is now construction geometry: it is the slot's "
            "centerline)", sourceId);
    }
    return r;
}

CliResult CliEngine::sketchArcSlot(const std::vector<std::string>& args, bool construction)
{
    CliResult r;

    // Aaron's description of an arc slot, which is also exactly what the
    // model stores: "Center point, radius to center of slot, Center point 1
    // of arc, center point 2 ending the arc, and thickness of the slot."
    //
    //   points[0] = arc center
    //   points[1] = center of the first round end
    //   points[2] = center of the second round end
    //   radius    = HALF the thickness
    //
    // The path radius is not stored at all; the geometry code derives it
    // from the distance to points[1], and reads points[2] for its ANGLE
    // only. So two cap centers at different distances describe a shape that
    // silently resolves in favor of the first. Taking the radius as well
    // and PROJECTING both points onto that circle removes the contradiction
    // instead of validating against it: the caller says which way each end
    // lies, the radius says how far out, and the two cannot disagree.
    //
    // An angle range is accepted as an alternative spelling of the same
    // thing, for anyone thinking in the shape of the "arc" command.
    if (static_cast<int>(args.size()) < 7) return failure(slotUsage());

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("at");

    double cx, cy;
    if (!cur.coord(cx, cy, "Invalid center. Use format: x,y")) return cur.result();
    if (!cur.expect("radius", 
            "Expected 'radius': in the arc form this is the radius out to "
            "the CENTERLINE of the slot, and 'width' is how thick it is.")) {
        return cur.result();
    }

    double pathRadius = 0.0;
    std::string pathExpr;
    if (!cur.value(pathRadius, pathExpr, "Invalid radius.")) return cur.result();

    // Either two cap centers, or two angles.
    bool byAngle = false;
    if (cur.accept("angle")) byAngle = true;
    else cur.accept("from");

    double startAngle = 0.0, endAngle = 0.0;
    std::string startExpr, endExpr;

    bool endIsMax = false;
    if (byAngle) {
        if (!cur.value(startAngle, startExpr, "Invalid start angle.")) return cur.result();
        if (!cur.expect("to", "Expected 'to' between the two ends")) return cur.result();

        // "max" sweeps as far as the shape allows: the point at which the
        // two end caps touch. That is the dial-indicator case, and the
        // number depends on the radius and the width, so asking a person to
        // work it out is asking them to do arithmetic the program can do.
        if (!cur.atEnd() &&
            (toLower(cur.current()) == "max" ||
             toLower(cur.current()) == "dial")) {
            endIsMax = true;
            endExpr = cur.current();
            cur.skip();
        } else if (!cur.value(endAngle, endExpr, "Invalid end angle.")) {
            return cur.result();
        }
    } else {
        double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
        if (!cur.coord(x1, y1, "Invalid first end center. Use format: x,y")) return cur.result();
        if (!cur.expect("to", "Expected 'to' between the two ends")) return cur.result();
        if (!cur.coord(x2, y2, "Invalid second end center. Use format: x,y")) return cur.result();

        // A point AT the center has no direction, so there is nothing to
        // project onto the circle.
        if ((x1 == cx && y1 == cy) || (x2 == cx && y2 == cy)) {
            return failure(
                "An end center cannot sit on the arc center: it gives no "
                "direction to place the end at.");
        }

        startAngle = hobbycad::radiansToDegrees(std::atan2(y1 - cy, x1 - cx));
        endAngle   = hobbycad::radiansToDegrees(std::atan2(y2 - cy, x2 - cx));
        startExpr  = numToString(startAngle);
        endExpr    = numToString(endAngle);
    }

    // "long" (or "major") takes the long way round. Two cap centers cannot
    // say which way the arc runs between them, and neither can two angles
    // less than a half turn apart, so it is a keyword rather than
    // something to infer. A sweep given as an angle range of more than 180
    // says it on its own.
    bool takeLongWay = false;
    while (cur.accept("long") || cur.accept("major")) takeLongWay = true;

    if (cur.atEnd() ||
        (toLower(cur.current()) != "width" &&
         toLower(cur.current()) != "thickness")) {
        return failure(
            "Expected 'width' (or 'thickness') and the slot's thickness");
    }
    cur.skip();

    double width = 0.0;
    std::string widthExpr;
    if (!cur.value(width, widthExpr, "Invalid width.")) return cur.result();

    while (!cur.atEnd()) {
        if (cur.accept("long") || cur.accept("major")) { takeLongWay = true; continue; }
        return failure(subst("Unexpected '%1'.", cur.current()));
    }

    if (!geometry::isPositiveLength(pathRadius)) {
        return failure("Radius must be greater than zero.");
    }
    if (!sketch::slotWidthIsPositive(width)) {   // zero at the length precision
        return failure("Width must be greater than zero.");
    }

    const double halfWidth = width / 2.0;
    // The inner edge sits at pathRadius - halfWidth. Reaching the center is
    // the limit case and is allowed (Aaron, 2026-08-28: "slot width/2 is
    // less than or equal to the arc radius"). Only PAST it does the edge
    // turn inside out through the center, which is not a slot.
    if (halfWidth > pathRadius) {
        return failure(subst(
            "A width of %1 does not fit on a centerline radius of %2: the "
            "inner edge would pass through the center and turn the shape "
            "inside out. Width can be at most %3, where the inner edge "
            "meets the center exactly.", width, pathRadius, pathRadius * 2.0));
    }

    if (endIsMax) {
        // Now that the radius and width are known, the limit can be
        // computed; it could not be at the point "max" was read.
        endAngle = startAngle + sketch::maxArcSlotSweepDegrees(pathRadius, halfWidth);
    }

    double sweep = endAngle - startAngle;
    if (byAngle) {
        if (geometry::isZeroAngleDeg(sweep)) {
            r.exitCode = 1;
            r.error = subst(
                "Start and end angle are both %1; an arc slot needs a sweep.", startAngle);
            return r;
        }
        // The real ceiling is below 360: see the check after this block,
        // which applies to both spellings.
        if (std::abs(sweep) >= 360.0) {
            return failure(subst(
                "A sweep of %1 degrees is a full turn or more.", sweep));
        }
    } else {
        while (sweep > 180.0) sweep -= 360.0;
        while (sweep <= -180.0) sweep += 360.0;
        if (geometry::isZeroAngleDeg(sweep)) {
            return failure(
                "Both ends lie in the same direction from the center, so "
                "there is no arc between them.");
        }
        if (takeLongWay) sweep = hobbycad::geometry::oppositeSweepDeg(sweep);
    }

    // An arc slot cannot reach a full turn: its two semicircular end caps
    // would land on top of each other. Aaron's rule, from 2026-02-20, is
    // "full circle minus 2x radius of arc end": back off by the width of
    // two caps so they stay distinct.
    const double maxSweep =
        sketch::maxArcSlotSweepDegrees(pathRadius, halfWidth);
    // Past the tangent sweep the caps OVERLAP. That is not an error: it
    // is how the middle of the ring is actually freed, since at tangency
    // it is still held by a point. Aaron, 2026-08-28: "I want to see it
    // where the 2 arc slot ends' hemispheres overlap."
    //
    // The floor is one cap radius of separation. Below that the two ends
    // have effectively merged and the outline stops describing the shape.
    const double hardMax =
        sketch::absoluteMaxArcSlotSweepDegrees(pathRadius, halfWidth);

    // A hair over, from rounding a hand-computed maximum, should not be
    // refused as if it were a different intention.
    if (std::abs(sweep) > hardMax + geometry::kZeroEps) {
        r.exitCode = 1;
        r.error = subst(
            "A sweep of %1%3 leaves the two ends less than one cap radius "
            "apart, so they merge rather than overlap.\n"
            "For a slot %2 wide the furthest is %4%3. At %5%3 the ends are "
            "tangent and leave a cusp; between the two they overlap and the "
            "middle comes free.", sweep, halfWidth * 2.0, ("\xc2\xb0"), maxText(hardMax), maxText(maxSweep));
        return r;
    }

    // The stored form cannot express a sweep on its own; two endpoints
    // describe both ways round. arcFlipped is what distinguishes them, and
    // a sweep past a half turn IS the long way.
    const bool flipped = std::abs(sweep) > 180.0;
    geometry::Arc pathArc;
    pathArc.center = {cx, cy};
    pathArc.radius = pathRadius;
    pathArc.startAngle = startAngle;
    pathArc.sweepAngle = sweep;

    auto e = makeEntity(sketch::EntityType::Slot);
    e.points = { pathArc.center, pathArc.startPoint(), pathArc.endPoint() };
    e.radius = halfWidth;
    e.arcFlipped = flipped;
    e.isConstruction = construction;

    // Same bones as the linear form; see the note there.
    const int pathId = addSlotCenterline(e, pathRadius, startAngle, sweep);
    const int id = addPendingEntity(e);
    const int groupId = groupSlotWithPath(id, pathId);

    const std::string deg = ("\xc2\xb0");
    r.output = subst(
        "Created arc slot at (%1, %2) radius %3, ends at %4%7 and %5%7, "
        "width %6 [id %8]", cx, cy, pathExpr, startExpr, endExpr, widthExpr, deg, id);
    std::string note;
    if (std::abs(std::abs(sweep) - maxSweep) < geometry::kAngleEpsDeg) {
        note = "; closed: the ends' perimeters touch";
    } else if (std::abs(sweep) > maxSweep) {
        // Worth saying every time: the shape is legitimate but its outline
        // self-intersects, and anything reading it back has to cope.
        note = "; the ends OVERLAP: no cusp, the middle "
                              "is cut free";
    } else if (flipped) {
        note = "; the long way round";
    }
    r.output += subst("\n(sweep %1%2%3)", sweep, deg, note);
    r.output += subst(
        "\n(centerline [id %1] added as construction, grouped as \"Slot %2\" "
        "[group %3])", pathId, id, groupId);
    return r;
}

CliResult CliEngine::cmdSketchSpline(std::vector<std::string> args)
{
    CliResult r;
    const bool construction = takeConstructionFlag(args);

    // "through" is optional, matching the optional "from"/"at" of the others.
    if (!args.empty() && toLower(args[0]) == "through") {
        args.erase(args.begin());
    }

    if (static_cast<int>(args.size()) < 2) {
        return failure(
            "Usage: spline [through] <x>,<y> <x>,<y> [<x>,<y> ...] [construction]\n"
            "\n"
            "Examples:\n"
            "  spline through 0,0 25,40 50,0\n"
            "  spline 0,0 10,10 20,0 30,10\n"
            "\n"
            "At least two control points are needed.");
    }

    std::vector<hobbycad::Point2D> pts;
    pts.reserve(static_cast<size_t>(static_cast<int>(args.size())));
    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        double x, y;
        if (!parseCoord(args[i], x, y, parameterValues(), namedPointValues())) {
            r.exitCode = 1;
            // Name the offending point. "Invalid coordinates" in a list of
            // eight is a hunt.
            r.error = subst(
                "Invalid coordinates for control point %1 ('%2'). Use format: x,y", i + 1, args[i]);
            return r;
        }
        pts.push_back({x, y});
    }

    auto e = makeEntity(sketch::EntityType::Spline);
    e.points.assign(pts.begin(), pts.end());
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst("Created spline through %1 control points [id %2]", pts.size(), id);
    return r;
}

CliResult CliEngine::cmdSketchBezier(std::vector<std::string> args)
{
    CliResult r;

    // Edit form: bezier <id> handle <anchor> in|out|tan <ang> <len>
    // Recompute one handle of an existing bezier precisely (angle + length),
    // the way the GUI cannot. Anchor is 0-based (matching `points <id>`).
    {
        bool idOk = false;
        const int eid = toInt(valueAt(args, 0), &idOk);
        if (idOk && toLower(valueAt(args, 1)) == "handle") {
            if (args.size() != 6) {
                return failure(
                    "Usage: bezier <id> handle <anchor> in|out|tan <ang> <len>");
            }
            hobbycad::SketchEntityData* ent = sketch::findEntityById(m_pendingSketch.entities, eid);
            if (!ent) {
                return failure(subst("No entity %1 in the current sketch.", eid));
            }
            if (ent->type != sketch::EntityType::Spline || !ent->splineBezier) {
                return failure(subst("Entity %1 is not a bezier.", eid));
            }
            bool aOk = false;
            const int ai = toInt(args[2], &aOk);
            const std::string kw = toLower(args[3]);
            const bool isIn = (kw == "in");
            const bool isOut = (kw == "out");
            const bool isTan = (kw == "tan");
            bool okA = false, okL = false;
            const double ang = toDouble(args[4], &okA);
            const double len = toDouble(args[5], &okL);
            if (!aOk || !(isIn || isOut || isTan) || !okA || !okL) {
                return failure(
                    "Usage: bezier <id> handle <anchor> in|out|tan <ang> <len>");
            }
            std::vector<hobbycad::Point2D> poly(ent->points.begin(), ent->points.end());
            auto anchors = sketch::bezierAnchorsFromControlPolygon(poly);
            if (anchors.empty()) {
                return failure(subst("Entity %1 is not a valid bezier.", eid));
            }
            const int N = static_cast<int>(anchors.size()) - 1;
            if (ai < 0 || ai > N) {
                return failure(subst("Anchor %1 out of range (0..%2).", ai, N));
            }
            const bool canOut = (ai < N), canIn = (ai > 0);
            if (isOut && !canOut) { r.exitCode = 1;
                r.error = subst("Anchor %1 is the last; it has no out handle.", ai); return r; }
            if (isIn && !canIn) { r.exitCode = 1;
                r.error = subst("Anchor %1 is the first; it has no in handle.", ai); return r; }
            hobbycad::sketch::BezierAnchor& a = anchors[static_cast<size_t>(ai)];
            sketch::setAnchorHandle(a, isIn ? sketch::BezierHandleSide::In
                                       : isOut ? sketch::BezierHandleSide::Out
                                               : sketch::BezierHandleSide::Tangent,
                                    ang, len, canIn, canOut);
            const std::vector<hobbycad::Point2D> rebuilt = sketch::bezierControlPolygon(anchors);
            ent->points.assign(rebuilt.begin(), rebuilt.end());
            r.exitCode = 0;
            r.output = subst("Updated bezier %1 anchor %2 %3 handle.", eid, ai, kw);
            return r;
        }
    }

    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 2) {
        r.exitCode = 1;
        r.error = 
            "Usage: bezier <x>,<y> [in <ang> <len>] [out <ang> <len>] | [tan <ang> <len>]\n"
            "              <x>,<y> ...  [construction]\n"
            "\n"
            "Handles are an angle (degrees, CCW from +X) and a length. 'out' points\n"
            "toward the next anchor, 'in' from the previous, 'tan' is a smooth\n"
            "symmetric pair. A missing handle makes a corner on that side.\n"
            "\n"
            "Examples:\n"
            "  bezier 0,0 out 45 1.4  3,3 tan 0 1  6,0 in 135 1.4\n"
            "  bezier 0,0 5,5 10,0            (corners: a polyline of cubics)\n"
            "\n"
            "At least two anchors are needed.";
        return r;
    }

    std::vector<hobbycad::sketch::BezierAnchor> anchors;
    int i = 0;
    while (i < static_cast<int>(args.size())) {
        double x, y;
        if (!parseCoord(args[i], x, y, parameterValues(), namedPointValues())) {
            r.exitCode = 1;
            r.error = subst("Invalid anchor coordinates ('%1'). Use format: x,y", args[i]);
            return r;
        }
        hobbycad::sketch::BezierAnchor a;
        a.pos = {x, y};
        ++i;
        while (i < static_cast<int>(args.size())) {
            const std::string kw = toLower(args[i]);
            const bool isIn  = (kw == "in");
            const bool isOut = (kw == "out");
            const bool isTan = (kw == "tan");
            if (kw == "weight") {          // per-anchor rational weight
                if (i + 1 >= static_cast<int>(args.size())) {
                    r.exitCode = 1; r.error = "'weight' needs a value."; return r;
                }
                bool okW = false; const double wv = toDouble(args[i + 1], &okW);
                if (!okW || wv <= 0.0) {
                    r.exitCode = 1;
                    r.error = subst("weight must be a positive number ('%1').", args[i + 1]);
                    return r;
                }
                a.weight = wv; i += 2; continue;
            }
            if (!isIn && !isOut && !isTan) break;
            if (i + 2 >= static_cast<int>(args.size())) {
                r.exitCode = 1;
                r.error = subst("Handle '%1' needs an angle and a length.", kw);
                return r;
            }
            bool okA = false, okL = false;
            const double ang = toDouble(args[i + 1], &okA);
            const double len = toDouble(args[i + 2], &okL);
            if (!okA || !okL) {
                r.exitCode = 1;
                r.error = subst("Handle '%1' angle and length must be numbers ('%2' '%3').", kw, args[i + 1], args[i + 2]);
                return r;
            }
            sketch::setAnchorHandle(a, isIn ? sketch::BezierHandleSide::In
                                       : isOut ? sketch::BezierHandleSide::Out
                                               : sketch::BezierHandleSide::Tangent,
                                    ang, len);
            i += 3;
        }
        anchors.push_back(a);
    }

    if (static_cast<int>(anchors.size()) < 2) {
        r.exitCode = 1;
        r.error = "A bezier needs at least two anchors.";
        return r;
    }

    std::vector<hobbycad::Point2D> poly = hobbycad::sketch::bezierControlPolygon(anchors);
    std::vector<double> polyW = hobbycad::sketch::bezierControlPolygonWeights(anchors);
    bool anyRational = false;
    for (double w : polyW) if (w < 0.999999999 || w > 1.000000001) { anyRational = true; break; }
    auto e = makeEntity(sketch::EntityType::Spline);
    e.splineBezier = true;
    if (anyRational) { e.splineRational = true; e.weights = polyW; }
    e.points.assign(poly.begin(), poly.end());
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst("Created %4bezier through %1 anchors (%2 control points) [id %3]", anchors.size(), poly.size(), id, anyRational ? "rational " : std::string());
    return r;
}

CliResult CliEngine::cmdSketchConic(std::vector<std::string> args)
{
    CliResult r;

    // Edit form: conic <id> rho <r>
    // Re-author an existing conic with a new rho; ends, end tangents and the
    // apex stay. This is the Properties panel's edit, on the command line
    // first (every capability lands in the command layer).
    {
        bool idOk = false;
        const int eid = toInt(valueAt(args, 0), &idOk);
        if (idOk && toLower(valueAt(args, 1)) == "rho") {
            if (args.size() != 3) {
                return failure(translate("QObject", "Usage: conic <id> rho <r>"));
            }
            hobbycad::SketchEntityData* ent =
                sketch::findEntityById(m_pendingSketch.entities, eid);
            if (!ent) {
                return failure(subst("No entity %1 in the current sketch.", eid));
            }
            if (ent->type != sketch::EntityType::Spline || !(ent->conicRho > 0.0)) {
                return failure(subst(translate("QObject", "Entity %1 is not a conic arc."), eid));
            }
            bool okR = false;
            const double rho = toDouble(args[2], &okR);
            if (!okR || !(rho > 0.0) || !(rho < 1.0)) {
                return failure(subst(translate("QObject", "rho must be a number between 0 and "
                                                          "1, exclusive ('%1')."),
                                     args[2]));
            }
            if (!sketch::setConicRho(*ent, rho)) {
                return failure(subst(translate("QObject", "Entity %1 has no recoverable apex; "
                                                          "it is not a conic arc."),
                                     eid));
            }
            r.exitCode = 0;
            r.output = subst(translate("QObject", "Updated conic %1: rho %2 (%3)."), eid, rho,
                             translate("QObject", sketch::conicKindName(rho)));
            return r;
        }
    }

    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 7) {
        return failure(translate("QObject",
            "Usage: conic [from] <x>,<y> to <x>,<y> apex <x>,<y> rho <r>\n"
            "             [construction]\n"
            "\n"
            "A conic arc by rho, as in Fusion, Onshape and SolidWorks: the two\n"
            "points are its ends, the apex is where the end tangents meet, and rho\n"
            "says where the curve's shoulder sits between the chord's midpoint (0)\n"
            "and the apex (1). Below 0.5 it is an elliptical arc, at 0.5 a parabola,\n"
            "above 0.5 a hyperbola. It is stored as one rational Bezier segment; rho\n"
            "stays a property of the curve (see `points <id>`, `conic <id> rho`).\n"
            "\n"
            "Examples:\n"
            "  conic 0,0 to 40,0 apex 20,30 rho 0.5          (a parabola)\n"
            "  conic 0,0 to 40,40 apex 40,0 rho 0.41421      (a quarter circle)"));
    }

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.accept("from");
    double sx, sy;
    if (!cur.coord(sx, sy, translate("QObject", "Invalid start coordinates. Use format: x,y"))
        || !cur.expect("to", translate("QObject",
                                       "Expected 'to' between the start and end points"))) {
        return cur.result();
    }
    double ex, ey;
    if (!cur.coord(ex, ey, translate("QObject", "Invalid end coordinates. Use format: x,y"))
        || !cur.expect("apex", translate("QObject", "Expected 'apex' keyword"))) {
        return cur.result();
    }
    double ax, ay;
    if (!cur.coord(ax, ay, translate("QObject", "Invalid apex coordinates. Use format: x,y"))
        || !cur.expect("rho", translate("QObject", "Expected 'rho' keyword"))) {
        return cur.result();
    }
    double rho = 0.0;
    std::string rhoExpr;
    if (!cur.value(rho, rhoExpr,
                   translate("QObject",
                             "Invalid rho. Must be a number, parameter, or (expression)."))) {
        return cur.result();
    }
    if (!(rho > 0.0) || !(rho < 1.0)) {
        return failure(translate("QObject", "rho must be between 0 and 1, exclusive."));
    }

    auto e = makeEntity(sketch::EntityType::Spline);
    if (!sketch::conicFromRho(e.id, {sx, sy}, {ex, ey}, {ax, ay}, rho, e)) {
        return failure(translate("QObject",
                                 "The two ends must be apart and the apex off their line."));
    }
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    r.output = subst(translate("QObject", "Created %6conic arc (%5) from (%1, %2) to (%3, %4), "
                                          "rho %7 [id %8]"),
                     sx, sy, ex, ey, translate("QObject", sketch::conicKindName(rho)),
                     construction ? "construction " : std::string(), rhoExpr, id);
    return r;
}

CliResult CliEngine::cmdSketchText(std::vector<std::string> args)
{
    const bool construction = takeConstructionFlag(args);

    if (static_cast<int>(args.size()) < 3) {
        return failure(
            "Usage: text <string> [at] <x>,<y> [size <n>] [rotation <deg>]\n"
            "\n"
            "Examples:\n"
            "  text \"Part A\" at 0,0\n"
            "  text Label at 10,10 size 8\n"
            "  text \"Top view\" at 0,50 size 6 rotation 90\n"
            "\n"
            "Quote the string if it contains spaces.");
    }

    const std::string content = args[0];
    if (content.empty()) return failure("Text cannot be empty.");

    ArgCursor cur(args, parameterValues(), namedPointValues());
    cur.skip();          // the string
    cur.accept("at");    // optional

    double x = 0.0, y = 0.0;
    if (!cur.coord(x, y, "Invalid position. Use format: x,y")) return cur.result();

    double size = 12.0;        // Entity's own default
    double rotation = 0.0;

    while (!cur.atEnd()) {
        const std::string key = toLower(cur.current());
        if (key != "size" && key != "rotation") {
            return failure(subst("Unexpected '%1'. Expected 'size' or 'rotation'.", cur.current()));
        }
        cur.skip();

        double value = 0.0;
        std::string expr;
        if (!cur.value(value, expr, subst("Missing or invalid value after '%1'.", key))) {
            return cur.result();
        }

        if (key == "size") {
            if (!geometry::isPositiveLength(value)) return failure("Text size must be greater than zero.");
            size = value;
        } else {
            rotation = value;
        }
    }

    auto e = makeEntity(sketch::EntityType::Text);
    e.points = { {x, y} };
    e.text = content;
    e.fontSize = size;
    e.textRotation = rotation;
    e.isConstruction = construction;
    const int id = addPendingEntity(e);

    CliResult r;
    r.output = subst("Created text \"%1\" at (%2, %3) size %4 [id %5]", content, x, y, size, id);
    return r;
}

// ---- Viewport commands (zoom, panto, rotate) -------------------------

CliResult CliEngine::cmdZoom(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.empty()) {
        return failure(
            "Usage: zoom <percent> | zoom home\n"
            "\n"
            "Examples:\n"
            "  zoom 100       Set zoom to 100% (fit all)\n"
            "  zoom 200       Zoom in to 200%\n"
            "  zoom 50        Zoom out to 50%\n"
            "  zoom home      Reset zoom to fit all objects");
    }

    std::string arg = toLower(args[0]);

    if (arg == "home") {
        r.viewportAction = ViewportAction::ZoomHome;
        r.output = "Zoom reset to fit all.";
        return r;
    }

    // Parse as percentage
    bool ok = false;
    double percent = toDouble(args[0], &ok);
    if (!ok || percent <= 0.0) {
        r.exitCode = 1;
        r.error = "Invalid zoom percentage. Must be a positive number.";
        return r;
    }

    r.viewportAction = ViewportAction::ZoomPercent;
    r.vpArg1 = percent;
    r.output = subst("Zoom set to %1%.", percent);
    return r;
}

CliResult CliEngine::cmdPanTo(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.empty()) {
        return failure(
            "Usage: panto <x>,<y>,<z> | panto home\n"
            "\n"
            "Pan the camera to center on the specified coordinates.\n"
            "\n"
            "Examples:\n"
            "  panto 0,0,0       Center on the origin\n"
            "  panto 100,50,0    Center on point (100, 50, 0)\n"
            "  panto home        Center on the origin");
    }

    std::string arg = toLower(args[0]);

    if (arg == "home") {
        r.viewportAction = ViewportAction::PanHome;
        r.output = "Panned to origin.";
        return r;
    }

    // Parse as x,y,z coordinates through the shared resolver, so named
    // points, parameters and expressions work here as in every sketch command.
    double x = 0.0, y = 0.0, z = 0.0;
    if (!parseCoord3(args[0], x, y, z, parameterValues(), namedPointValues())) {
        return failure(
            "Invalid coordinates. Use format: x,y,z (e.g., 100,50,0), a named "
            "point, or expressions of parameters.");
    }

    r.viewportAction = ViewportAction::PanTo;
    r.vpArg1 = x;
    r.vpArg2 = y;
    r.vpArg3 = z;
    r.output = subst("Panned to (%1, %2, %3).", x, y, z);
    return r;
}

CliResult CliEngine::cmdRotate(const std::vector<std::string>& args)
{
    CliResult r;

    if (args.empty()) {
        return failure(
            "Usage: rotate on <axis> <degrees> | rotate home\n"
            "\n"
            "Rotate the camera around a world axis.\n"
            "\n"
            "Arguments:\n"
            "  <axis>      x, y, or z\n"
            "  <degrees>   Rotation angle (positive = CCW)\n"
            "\n"
            "Examples:\n"
            "  rotate on z 45      Rotate 45° around the Z axis\n"
            "  rotate on x -90     Rotate -90° around the X axis\n"
            "  rotate home         Reset to isometric view");
    }

    std::string arg = toLower(args[0]);

    if (arg == "home") {
        r.viewportAction = ViewportAction::RotateHome;
        r.output = "View reset to isometric.";
        return r;
    }

    // Expect: "on <axis> <degrees>"
    if (arg != "on" || static_cast<int>(args.size()) < 3) {
        r.exitCode = 1;
        r.error = 
            "Usage: rotate on <axis> <degrees>\n"
            "\n"
            "Example: rotate on z 45";
        return r;
    }

    std::string axisStr = toLower(args[1]);
    if (axisStr != "x" &&
        axisStr != "y" &&
        axisStr != "z") {
        r.exitCode = 1;
        r.error = "Invalid axis. Use x, y, or z.";
        return r;
    }

    bool ok = false;
    double degrees = toDouble(args[2], &ok);
    if (!ok) {
        r.exitCode = 1;
        r.error = "Invalid angle. Must be a number (degrees).";
        return r;
    }

    r.viewportAction = ViewportAction::RotateAxis;
    r.vpAxis = axisStr[0];
    r.vpArg1 = degrees;
    r.output = subst("Rotated %1° around %2 axis.", degrees, toUpper(axisStr));
    return r;
}

}  // namespace hobbycad

