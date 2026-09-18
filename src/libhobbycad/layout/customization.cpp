// =====================================================================
//  src/libhobbycad/layout/customization.cpp — the user's own arrangement
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/layout/customization.h>

#include <hobbycad/commands.h>
#include <hobbycad/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <set>

namespace hobbycad {
namespace layout {

namespace {

// ---------------------------------------------------------------------
//  A strict, capped JSON reader
// ---------------------------------------------------------------------
//  Only what the arrangement file needs, and nothing a hostile file can
//  use: depth, node count, array length and string length are all capped,
//  duplicate keys are refused, every string must be valid UTF-8, and no
//  path fails by throwing.

constexpr std::size_t kMaxStringBytes = 4096;
constexpr std::size_t kMaxNodes = 65536;
constexpr std::size_t kMaxArrayItems = kMaxEntries;

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;                                  ///< Array
    std::vector<std::pair<std::string, Json>> members;        ///< Object

    const Json* member(const std::string& name) const
    {
        for (const auto& m : members) {
            if (m.first == name) return &m.second;
        }
        return nullptr;
    }
};

/// True when `text` is valid UTF-8 with no over-long or surrogate
/// encodings.
bool validUtf8(const std::string& text, std::size_t* codePoints = nullptr)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned int cp = 0;
        if (c < 0x80) {
            extra = 0;
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + extra >= text.size() + (extra == 0 ? 1 : 0)) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (extra == 1 && cp < 0x80) return false;
        if (extra == 2 && cp < 0x800) return false;
        if (extra == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += extra + 1;
        ++count;
    }
    if (codePoints) *codePoints = count;
    return true;
}

void appendUtf8(std::string& out, unsigned int cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Reader {
public:
    explicit Reader(const std::string& text) : m_text(text) {}

    bool parse(Json& out, std::string& error)
    {
        if (!value(out, 1)) {
            error = m_error.empty() ? "not readable" : m_error;
            return false;
        }
        space();
        if (m_at != m_text.size()) {
            error = "text after the end of the arrangement";
            return false;
        }
        return true;
    }

private:
    bool fail(const char* why)
    {
        if (m_error.empty()) m_error = why;
        return false;
    }

    void space()
    {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_at;
            } else {
                break;
            }
        }
    }

    bool literal(const char* word)
    {
        const std::size_t n = std::string(word).size();
        if (m_text.compare(m_at, n, word) != 0) return fail("a word that is not JSON");
        m_at += n;
        return true;
    }

    bool string(std::string& out)
    {
        if (m_at >= m_text.size() || m_text[m_at] != '"') return fail("a string was expected");
        ++m_at;
        out.clear();
        while (true) {
            if (m_at >= m_text.size()) return fail("a string that never ends");
            const unsigned char c = static_cast<unsigned char>(m_text[m_at]);
            if (c == '"') {
                ++m_at;
                break;
            }
            if (out.size() > kMaxStringBytes) return fail("a string past the length cap");
            if (c < 0x20) return fail("a control character in a string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++m_at;
                continue;
            }
            ++m_at;
            if (m_at >= m_text.size()) return fail("an escape that never ends");
            const char e = m_text[m_at++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned int cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // A high surrogate needs its low one.
                    if (m_text.compare(m_at, 2, "\\u") != 0) return fail("a lone surrogate");
                    m_at += 2;
                    unsigned int low = 0;
                    if (!hex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return fail("a lone surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("a lone surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("an escape that is not JSON");
            }
        }
        if (!validUtf8(out)) return fail("a string that is not UTF-8");
        return true;
    }

    bool hex4(unsigned int& out)
    {
        if (m_at + 4 > m_text.size()) return fail("a short escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = m_text[m_at++];
            int d = 0;
            if (c >= '0' && c <= '9') {
                d = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                d = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                d = c - 'A' + 10;
            } else {
                return fail("an escape that is not hexadecimal");
            }
            out = out * 16 + static_cast<unsigned int>(d);
        }
        return true;
    }

    bool number(double& out)
    {
        const std::size_t start = m_at;
        if (m_at < m_text.size() && m_text[m_at] == '-') ++m_at;
        std::size_t digits = 0;
        while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9') {
            ++m_at;
            ++digits;
        }
        if (digits == 0) return fail("a number with no digits");
        if (m_at < m_text.size() && m_text[m_at] == '.') {
            ++m_at;
            std::size_t frac = 0;
            while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9') {
                ++m_at;
                ++frac;
            }
            if (frac == 0) return fail("a number with nothing after the point");
        }
        if (m_at < m_text.size() && (m_text[m_at] == 'e' || m_text[m_at] == 'E')) {
            ++m_at;
            if (m_at < m_text.size() && (m_text[m_at] == '+' || m_text[m_at] == '-')) ++m_at;
            std::size_t exp = 0;
            while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9') {
                ++m_at;
                ++exp;
            }
            if (exp == 0) return fail("a number with nothing in its exponent");
        }
        if (m_at - start > 40) return fail("a number past the length cap");
        // No exception escapes: the text is already known to be a number.
        out = std::strtod(m_text.substr(start, m_at - start).c_str(), nullptr);
        if (!std::isfinite(out)) return fail("a number that is not finite");
        return true;
    }

    bool value(Json& out, int depth)
    {
        if (depth > kMaxDepth) return fail("nesting deeper than the cap");
        if (++m_nodes > kMaxNodes) return fail("more values than the cap");
        space();
        if (m_at >= m_text.size()) return fail("nothing to read");
        const char c = m_text[m_at];
        if (c == '{') {
            ++m_at;
            out.kind = Json::Kind::Object;
            space();
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                ++m_at;
                return true;
            }
            while (true) {
                space();
                std::string name;
                if (!string(name)) return false;
                for (const auto& m : out.members) {
                    if (m.first == name) return fail("the same key twice in one object");
                }
                space();
                if (m_at >= m_text.size() || m_text[m_at] != ':') {
                    return fail("a key with no value");
                }
                ++m_at;
                Json child;
                if (!value(child, depth + 1)) return false;
                if (out.members.size() >= kMaxArrayItems) return fail("more keys than the cap");
                out.members.emplace_back(std::move(name), std::move(child));
                space();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == '}') {
                    ++m_at;
                    return true;
                }
                return fail("an object that never ends");
            }
        }
        if (c == '[') {
            ++m_at;
            out.kind = Json::Kind::Array;
            space();
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                ++m_at;
                return true;
            }
            while (true) {
                Json child;
                if (!value(child, depth + 1)) return false;
                if (out.items.size() >= kMaxArrayItems) return fail("more items than the cap");
                out.items.push_back(std::move(child));
                space();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                    continue;
                }
                if (m_at < m_text.size() && m_text[m_at] == ']') {
                    ++m_at;
                    return true;
                }
                return fail("an array that never ends");
            }
        }
        if (c == '"') {
            out.kind = Json::Kind::String;
            return string(out.text);
        }
        if (c == 't') {
            out.kind = Json::Kind::Bool;
            out.boolean = true;
            return literal("true");
        }
        if (c == 'f') {
            out.kind = Json::Kind::Bool;
            out.boolean = false;
            return literal("false");
        }
        if (c == 'n') {
            out.kind = Json::Kind::Null;
            return literal("null");
        }
        out.kind = Json::Kind::Number;
        return number(out.number);
    }

    const std::string& m_text;
    std::size_t m_at = 0;
    std::size_t m_nodes = 0;
    std::string m_error;
};

// ---------------------------------------------------------------------
//  Checks and message text
// ---------------------------------------------------------------------

/// An id or element key: the plain characters ids are made of.
bool plainId(const std::string& id, std::size_t cap = kMaxIdLength)
{
    if (id.empty() || id.size() > cap) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                     || c == '.' || c == '_' || c == '-' || c == '/';
        if (!ok) return false;
    }
    return true;
}

/// A binding, as the bindings editor writes it: printable ASCII, short.
bool plainBinding(const std::string& binding)
{
    if (binding.size() > 64) return false;
    for (char c : binding) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u >= 0x7F) return false;
    }
    return true;
}

/// Text from the file, made safe to show in a message: printable ASCII
/// only, and short.
std::string shown(const std::string& text)
{
    std::string out;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        out.push_back(u >= 0x20 && u < 0x7F ? c : '?');
        if (out.size() >= 64) {
            out += "...";
            break;
        }
        }
    return out;
}

std::string numberText(double value)
{
    return formatStorageDouble(value);
}

void escapeInto(std::string& out, const std::string& text)
{
    out.push_back('"');
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (u < 0x20) {
                out += format("\\u%04x", static_cast<unsigned int>(u));
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

const char* kindName(ElementKind kind)
{
    switch (kind) {
    case ElementKind::Command: return "command";
    case ElementKind::Container: return "container";
    case ElementKind::Separator: return "separator";
    case ElementKind::Placeholder: return "placeholder";
    }
    return "command";
}

const char* containerKindName(ContainerKind kind)
{
    switch (kind) {
    case ContainerKind::MenuBar: return "menubar";
    case ContainerKind::Menu: return "menu";
    case ContainerKind::Toolbar: return "toolbar";
    case ContainerKind::ToolGroup: return "toolgroup";
    case ContainerKind::Variants: return "variants";
    }
    return "menu";
}

/// Which area an element's default container belongs to.
bool inArea(const Arrangement& base, const std::string& key, RestoreArea area)
{
    const Element* element = base.element(key);
    if (!element) return false;
    const Container* container = base.container(element->container);
    const ContainerKind kind = container ? container->kind : ContainerKind::Menu;
    const bool menus = kind == ContainerKind::Menu || kind == ContainerKind::MenuBar;
    return area == RestoreArea::Menus ? menus : !menus;
}

/// One element entry, read strictly. False means it was skipped, with why
/// in `problem`.
bool readElementEntry(const Json& item, const Arrangement& base, bool forImport,
                      Customization& out, std::string& problem)
{
    if (item.kind != Json::Kind::Object) {
        problem = "an entry that is not an object";
        return false;
    }
    static const std::set<std::string> kOwn = {"key", "order", "container", "hidden", "label"};
    // An exported arrangement carries what the default already says; those
    // keys are read past, not customized.
    static const std::set<std::string> kExported = {"id", "kind"};
    for (const auto& m : item.members) {
        if (kOwn.count(m.first)) continue;
        if (forImport && kExported.count(m.first)) continue;
        problem = "an entry with the unknown key \"" + shown(m.first) + "\"";
        return false;
    }
    const Json* key = item.member("key");
    if (!key || key->kind != Json::Kind::String) {
        problem = "an entry with no element key";
        return false;
    }
    if (!plainId(key->text)) {
        problem = "the element key \"" + shown(key->text) + "\" is not one an id may have";
        return false;
    }
    if (!base.element(key->text)) {
        problem = "no element \"" + shown(key->text) + "\" any more; its entry is dropped";
        return false;
    }
    out = Customization();
    out.key = key->text;
    if (const Json* order = item.member("order")) {
        if (order->kind != Json::Kind::Number) {
            problem = "the sort order of \"" + shown(out.key) + "\" is not a number";
            return false;
        }
        out.order = order->number;
    }
    if (const Json* container = item.member("container")) {
        if (container->kind != Json::Kind::String || !plainId(container->text)) {
            problem = "the container of \"" + shown(out.key) + "\" is not an id";
            return false;
        }
        if (!base.container(container->text)) {
            problem = "no container \"" + shown(container->text) + "\" to hold \""
                    + shown(out.key) + "\"";
            return false;
        }
        out.container = container->text;
    }
    if (const Json* hidden = item.member("hidden")) {
        if (hidden->kind != Json::Kind::Bool) {
            problem = "the hidden flag of \"" + shown(out.key) + "\" is not true or false";
            return false;
        }
        out.hidden = hidden->boolean;
    }
    if (const Json* label = item.member("label")) {
        if (label->kind != Json::Kind::String || !isUserLabel(label->text)) {
            problem = "the name given to \"" + shown(out.key) + "\" is not a name";
            return false;
        }
        out.label = label->text;
    }
    return true;
}

bool readAddedEntry(const Json& item, const Arrangement& base, AddedElement& out,
                    std::string& problem)
{
    if (item.kind != Json::Kind::Object) {
        problem = "an added entry that is not an object";
        return false;
    }
    for (const auto& m : item.members) {
        if (m.first == "key" || m.first == "container" || m.first == "order"
            || m.first == "kind") {
            continue;
        }
        problem = "an added entry with the unknown key \"" + shown(m.first) + "\"";
        return false;
    }
    const Json* key = item.member("key");
    // Only a separator can be added, and only under the key that says so.
    if (!key || key->kind != Json::Kind::String || !plainId(key->text)
        || key->text.rfind(kAddedPrefix, 0) != 0) {
        problem = "an added entry whose key is not one a person may add";
        return false;
    }
    if (base.element(key->text)) {
        problem = "\"" + shown(key->text) + "\" is already in the arrangement";
        return false;
    }
    const Json* container = item.member("container");
    if (!container || container->kind != Json::Kind::String || !plainId(container->text)
        || !base.container(container->text)) {
        problem = "the added \"" + shown(key->text) + "\" names no container that exists";
        return false;
    }
    const Json* order = item.member("order");
    if (!order || order->kind != Json::Kind::Number) {
        problem = "the added \"" + shown(key->text) + "\" has no sort order";
        return false;
    }
    out = AddedElement();
    out.key = key->text;
    out.container = container->text;
    out.order = order->number;
    return true;
}

bool readBindingEntry(const Json& item, BindingCustomization& out, std::string& problem)
{
    if (item.kind != Json::Kind::Object) {
        problem = "a binding entry that is not an object";
        return false;
    }
    for (const auto& m : item.members) {
        if (m.first == "command" || m.first == "slots") continue;
        problem = "a binding entry with the unknown key \"" + shown(m.first) + "\"";
        return false;
    }
    const Json* command = item.member("command");
    if (!command || command->kind != Json::Kind::String || !plainId(command->text)) {
        problem = "a binding entry with no command";
        return false;
    }
    if (!commands::findCommand(command->text)) {
        problem = "no command \"" + shown(command->text) + "\" any more; its binding is dropped";
        return false;
    }
    const Json* keys = item.member("slots");
    if (!keys || keys->kind != Json::Kind::Array) {
        problem = "the bindings of \"" + shown(command->text) + "\" are not a list";
        return false;
    }
    if (keys->items.size() > static_cast<std::size_t>(bindings::kSlotCount)) {
        problem = "more bindings for \"" + shown(command->text) + "\" than a command has slots";
        return false;
    }
    out = BindingCustomization();
    out.commandId = command->text;
    for (std::size_t i = 0; i < keys->items.size(); ++i) {
        const Json& slot = keys->items[i];
        if (slot.kind != Json::Kind::String || !plainBinding(slot.text)) {
            problem = "a binding of \"" + shown(command->text) + "\" is not a binding";
            return false;
        }
        out.keys[i] = slot.text;
    }
    return true;
}

/// The common part of reading the file and an imported arrangement.
ReadResult readEntries(const std::string& text, const Arrangement& base, bool forImport)
{
    ReadResult result;
    if (text.size() > kMaxFileBytes) {
        result.problems.push_back("the arrangement is larger than the size cap; it is not read");
        return result;
    }
    Json root;
    std::string error;
    if (!Reader(text).parse(root, error)) {
        result.problems.push_back("the arrangement cannot be read: " + error);
        return result;
    }
    if (root.kind != Json::Kind::Object) {
        result.problems.push_back("the arrangement is not an object");
        return result;
    }
    result.usable = true;
    std::size_t entries = 0;
    for (const auto& member : root.members) {
        const std::string& name = member.first;
        const Json& node = member.second;
        if (name == "version") {
            if (node.kind != Json::Kind::Number) {
                result.problems.push_back("the version is not a number");
            } else if (node.number > kCustomizationVersion) {
                result.problems.push_back("the arrangement was written by a later version of "
                                          "HobbyCAD; what this one understands is read");
            }
            continue;
        }
        if (name == "containers" && forImport) {
            // The containers of an exported arrangement are the default's;
            // an import places elements, it does not add containers.
            continue;
        }
        if (name == "elements" || name == "bindings" || name == "added") {
            if (node.kind != Json::Kind::Array) {
                result.problems.push_back("\"" + name + "\" is not a list");
                continue;
            }
            for (const Json& item : node.items) {
                if (++entries > kMaxEntries) {
                    result.problems.push_back("more entries than the cap; the rest are skipped");
                    break;
                }
                std::string problem;
                if (name == "elements") {
                    Customization entry;
                    if (readElementEntry(item, base, forImport, entry, problem)) {
                        result.customizations.add(entry);
                    } else {
                        result.problems.push_back(problem);
                    }
                } else if (name == "added") {
                    AddedElement entry;
                    if (readAddedEntry(item, base, entry, problem)) {
                        result.customizations.add(entry);
                    } else {
                        result.problems.push_back(problem);
                    }
                } else {
                    BindingCustomization entry;
                    if (readBindingEntry(item, entry, problem)) {
                        result.customizations.add(entry);
                    } else {
                        result.problems.push_back(problem);
                    }
                }
            }
            continue;
        }
        result.problems.push_back("the unknown key \"" + shown(name) + "\" is skipped");
    }
    return result;
}

}  // namespace

// ---------------------------------------------------------------------
//  Customizations
// ---------------------------------------------------------------------

const Customization* Customizations::find(const std::string& key) const
{
    for (const Customization& c : m_elements) {
        if (c.key == key) return &c;
    }
    return nullptr;
}

const BindingCustomization* Customizations::findBinding(const std::string& commandId) const
{
    for (const BindingCustomization& b : m_bindings) {
        if (b.commandId == commandId) return &b;
    }
    return nullptr;
}

Customization& Customizations::entryFor(const std::string& key)
{
    for (Customization& c : m_elements) {
        if (c.key == key) return c;
    }
    Customization fresh;
    fresh.key = key;
    m_elements.push_back(fresh);
    return m_elements.back();
}

void Customizations::setOrder(const std::string& key, double order)
{
    if (!std::isfinite(order)) return;
    entryFor(key).order = order;
}

void Customizations::setContainer(const std::string& key, const std::string& container)
{
    entryFor(key).container = container;
}

void Customizations::setHidden(const std::string& key, bool hidden)
{
    entryFor(key).hidden = hidden;
}

void Customizations::clearHidden(const std::string& key)
{
    for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        if (it->key != key) continue;
        it->hidden.reset();
        if (it->empty()) m_elements.erase(it);
        return;
    }
}

void Customizations::setLabel(const std::string& key, const std::string& label)
{
    if (label.empty()) {
        // The default text again, while the rest of the entry stands.
        for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
            if (it->key != key) continue;
            it->label.reset();
            if (it->empty()) m_elements.erase(it);
            return;
        }
        return;
    }
    if (!isUserLabel(label)) return;
    entryFor(key).label = label;
}

void Customizations::setBinding(const std::string& commandId, const bindings::Slots& keys)
{
    for (BindingCustomization& b : m_bindings) {
        if (b.commandId == commandId) {
            b.keys = keys;
            return;
        }
    }
    BindingCustomization fresh;
    fresh.commandId = commandId;
    fresh.keys = keys;
    m_bindings.push_back(fresh);
}

bool Customizations::move(const Arrangement& resolved, const std::string& key,
                          const std::string& container, int index)
{
    const Element* element = resolved.element(key);
    if (!element || !resolved.container(container)) return false;

    // The numbers of what stays put, in the order shown.
    std::vector<double> orders;
    for (const Element* sibling : resolved.children(container, true)) {
        if (sibling->key == key) continue;
        orders.push_back(sibling->order);
    }
    const int count = static_cast<int>(orders.size());
    index = std::max(0, std::min(index, count));

    // A number between its new neighbors', so nothing else is renumbered.
    double order = 100.0;
    if (count == 0) {
        order = 100.0;
    } else if (index == 0) {
        order = orders.front() - 100.0;
    } else if (index == count) {
        order = orders.back() + 100.0;
    } else {
        const double before = orders[static_cast<std::size_t>(index) - 1];
        const double after = orders[static_cast<std::size_t>(index)];
        order = before + (after - before) / 2.0;
        // Neighbors that share a number (or sit next to each other in the
        // smallest step a double has) still need them apart.
        if (!(order > before)) order = std::nextafter(before, after);
    }
    setOrder(key, order);
    if (element->container != container) setContainer(key, container);
    return true;
}

std::string Customizations::addSeparator(const Arrangement& resolved,
                                         const std::string& container, int index)
{
    if (!resolved.container(container)) return std::string();
    // A number of its own, stable across sessions.
    int next = 1;
    for (const AddedElement& a : m_added) {
        const std::string tail = a.key.substr(std::string(kAddedPrefix).size());
        next = std::max(next, std::atoi(tail.c_str()) + 1);
    }
    AddedElement fresh;
    fresh.key = std::string(kAddedPrefix) + std::to_string(next);
    fresh.container = container;
    fresh.order = 100.0;
    const std::vector<const Element*> kids = resolved.children(container, true);
    const int count = static_cast<int>(kids.size());
    index = std::max(0, std::min(index, count));
    if (count == 0) {
        fresh.order = 100.0;
    } else if (index == 0) {
        fresh.order = kids.front()->order - 100.0;
    } else if (index == count) {
        fresh.order = kids.back()->order + 100.0;
    } else {
        const double before = kids[static_cast<std::size_t>(index) - 1]->order;
        const double after = kids[static_cast<std::size_t>(index)]->order;
        fresh.order = before + (after - before) / 2.0;
        if (!(fresh.order > before)) fresh.order = std::nextafter(before, after);
    }
    m_added.push_back(fresh);
    return fresh.key;
}

bool Customizations::restoreElement(const std::string& key)
{
    bool gone = false;
    for (auto it = m_elements.begin(); it != m_elements.end(); ++it) {
        if (it->key != key) continue;
        m_elements.erase(it);
        gone = true;
        break;
    }
    // A separator the person added goes away entirely.
    for (auto it = m_added.begin(); it != m_added.end(); ++it) {
        if (it->key != key) continue;
        m_added.erase(it);
        gone = true;
        break;
    }
    return gone;
}

bool Customizations::restoreBinding(const std::string& commandId)
{
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
        if (it->commandId != commandId) continue;
        m_bindings.erase(it);
        return true;
    }
    return false;
}

std::size_t Customizations::restoreArea(RestoreArea area, const Arrangement& base)
{
    std::size_t gone = 0;
    if (area == RestoreArea::All || area == RestoreArea::Bindings) {
        gone += m_bindings.size();
        m_bindings.clear();
    }
    if (area == RestoreArea::Bindings) return gone;
    if (area == RestoreArea::All) {
        gone += m_elements.size() + m_added.size();
        m_elements.clear();
        m_added.clear();
        return gone;
    }
    std::vector<AddedElement> keptAdded;
    for (const AddedElement& a : m_added) {
        const Container* container = base.container(a.container);
        const ContainerKind kind = container ? container->kind : ContainerKind::Menu;
        const bool menus = kind == ContainerKind::Menu || kind == ContainerKind::MenuBar;
        if (menus == (area == RestoreArea::Menus)) {
            ++gone;
        } else {
            keptAdded.push_back(a);
        }
    }
    m_added = keptAdded;
    std::vector<Customization> kept;
    for (const Customization& c : m_elements) {
        if (inArea(base, c.key, area)) {
            ++gone;
        } else {
            kept.push_back(c);
        }
    }
    m_elements = kept;
    return gone;
}

void Customizations::add(const Customization& entry)
{
    if (entry.key.empty() || entry.empty()) return;
    Customization& own = entryFor(entry.key);
    if (entry.order) own.order = entry.order;
    if (entry.container) own.container = entry.container;
    if (entry.hidden) own.hidden = entry.hidden;
    if (entry.label) own.label = entry.label;
}

void Customizations::add(const BindingCustomization& entry)
{
    if (entry.commandId.empty()) return;
    setBinding(entry.commandId, entry.keys);
}

void Customizations::add(const AddedElement& entry)
{
    if (entry.key.empty() || entry.container.empty() || !std::isfinite(entry.order)) return;
    for (const AddedElement& own : m_added) {
        if (own.key == entry.key) return;
    }
    m_added.push_back(entry);
}

// ---------------------------------------------------------------------
//  Reading, writing, resolving
// ---------------------------------------------------------------------

bool isUserLabel(const std::string& label)
{
    if (label.size() > kMaxLabelLength * 4) return false;
    std::size_t codePoints = 0;
    if (!validUtf8(label, &codePoints)) return false;
    if (codePoints > kMaxLabelLength) return false;
    for (char c : label) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) return false;
    }
    return true;
}

ReadResult readCustomizations(const std::string& text, const Arrangement& base)
{
    return readEntries(text, base, false);
}

std::string writeCustomizations(const Customizations& customizations)
{
    if (customizations.empty()) return std::string();
    std::string out = "{\n";
    out += "  \"version\": " + numberText(kCustomizationVersion) + ",\n";
    out += "  \"elements\": [\n";
    bool first = true;
    for (const Customization& c : customizations.elements()) {
        if (c.empty()) continue;
        if (!first) out += ",\n";
        first = false;
        out += "    {\"key\": ";
        escapeInto(out, c.key);
        if (c.container) {
            out += ", \"container\": ";
            escapeInto(out, *c.container);
        }
        if (c.order) out += ", \"order\": " + numberText(*c.order);
        if (c.hidden) out += std::string(", \"hidden\": ") + (*c.hidden ? "true" : "false");
        if (c.label) {
            out += ", \"label\": ";
            escapeInto(out, *c.label);
        }
        out += "}";
    }
    out += "\n  ],\n  \"added\": [\n";
    first = true;
    for (const AddedElement& a : customizations.added()) {
        if (!first) out += ",\n";
        first = false;
        out += "    {\"key\": ";
        escapeInto(out, a.key);
        out += ", \"kind\": \"separator\", \"container\": ";
        escapeInto(out, a.container);
        out += ", \"order\": " + numberText(a.order) + "}";
    }
    out += "\n  ],\n  \"bindings\": [\n";
    first = true;
    for (const BindingCustomization& b : customizations.bindings()) {
        if (!first) out += ",\n";
        first = false;
        out += "    {\"command\": ";
        escapeInto(out, b.commandId);
        out += ", \"slots\": [";
        for (int i = 0; i < bindings::kSlotCount; ++i) {
            if (i) out += ", ";
            escapeInto(out, b.keys[static_cast<std::size_t>(i)]);
        }
        out += "]}";
    }
    out += "\n  ]\n}\n";
    return out;
}

Arrangement resolve(const Arrangement& base, const Customizations& customizations)
{
    Arrangement out = base;
    for (const AddedElement& a : customizations.added()) {
        if (!out.container(a.container) || !std::isfinite(a.order)) continue;
        out.addKeyedElement(a.key, ElementKind::Separator, std::string(), a.container, a.order);
    }
    for (const Customization& c : customizations.elements()) {
        Element* element = out.element(c.key);
        if (!element) continue;   // dropped; the reader said so
        if (c.container && out.container(*c.container)) element->container = *c.container;
        if (c.order && std::isfinite(*c.order)) element->order = *c.order;
        if (c.hidden) element->hidden = *c.hidden;
        if (c.label && isUserLabel(*c.label)) element->userLabel = *c.label;
    }
    for (const BindingCustomization& b : customizations.bindings()) {
        out.addDefaultBinding(b.commandId, b.keys);
    }
    return out;
}

std::string exportArrangement(const Arrangement& resolved)
{
    std::string out = "{\n";
    out += "  \"version\": " + numberText(kCustomizationVersion) + ",\n";
    out += "  \"containers\": [\n";
    bool first = true;
    for (const Container& container : resolved.containers()) {
        if (!first) out += ",\n";
        first = false;
        out += "    {\"id\": ";
        escapeInto(out, container.id);
        out += ", \"kind\": ";
        escapeInto(out, containerKindName(container.kind));
        if (!container.title.empty()) {
            out += ", \"title\": ";
            escapeInto(out, container.title);
        }
        if (!container.activates.empty()) {
            out += ", \"activates\": ";
            escapeInto(out, container.activates);
        }
        out += "}";
    }
    out += "\n  ],\n  \"added\": [\n";
    first = true;
    for (const Container& container : resolved.containers()) {
        for (const Element* element : resolved.children(container.id, true)) {
            if (element->key.rfind(kAddedPrefix, 0) != 0) continue;
            if (!first) out += ",\n";
            first = false;
            out += "    {\"key\": ";
            escapeInto(out, element->key);
            out += ", \"kind\": \"separator\", \"container\": ";
            escapeInto(out, element->container);
            out += ", \"order\": " + numberText(element->order) + "}";
        }
    }
    out += "\n  ],\n  \"elements\": [\n";
    first = true;
    for (const Container& container : resolved.containers()) {
        for (const Element* element : resolved.children(container.id, true)) {
            // What the person added is in "added" above; this is the
            // default's own elements, wherever they now sit.
            if (element->key.rfind(kAddedPrefix, 0) == 0) continue;
            if (!first) out += ",\n";
            first = false;
            out += "    {\"key\": ";
            escapeInto(out, element->key);
            out += ", \"id\": ";
            escapeInto(out, element->id);
            out += ", \"kind\": ";
            escapeInto(out, kindName(element->kind));
            out += ", \"container\": ";
            escapeInto(out, element->container);
            out += ", \"order\": " + numberText(element->order);
            out += std::string(", \"hidden\": ") + (element->hidden ? "true" : "false");
            if (!element->userLabel.empty()) {
                out += ", \"label\": ";
                escapeInto(out, element->userLabel);
            }
            out += "}";
        }
    }
    out += "\n  ],\n  \"bindings\": [\n";
    first = true;
    for (const auto& binding : resolved.defaultBindings()) {
        if (!first) out += ",\n";
        first = false;
        out += "    {\"command\": ";
        escapeInto(out, binding.first);
        out += ", \"slots\": [";
        for (int i = 0; i < bindings::kSlotCount; ++i) {
            if (i) out += ", ";
            escapeInto(out, binding.second[static_cast<std::size_t>(i)]);
        }
        out += "]}";
    }
    out += "\n  ]\n}\n";
    return out;
}

ReadResult importArrangement(const std::string& text, const Arrangement& base)
{
    ReadResult read = readEntries(text, base, true);
    if (!read.usable) return read;

    // What the file says each element's place is, whether or not it differs.
    ReadResult out;
    out.usable = true;
    out.problems = read.problems;

    // Relative order is what counts: a container whose elements come in the
    // default's order needs no entry, however the file numbers them.
    std::map<std::string, std::vector<const Customization*>> byContainer;
    for (const Customization& c : read.customizations.elements()) {
        const Element* element = base.element(c.key);
        if (!element) continue;
        const std::string container = c.container ? *c.container : element->container;
        byContainer[container].push_back(&c);
    }
    for (auto& pair : byContainer) {
        std::vector<const Customization*>& entries = pair.second;
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Customization* a, const Customization* b) {
                             const double av = a->order ? *a->order : 0.0;
                             const double bv = b->order ? *b->order : 0.0;
                             return av < bv;
                         });
        // The default's order for the same elements, for comparison.
        std::vector<std::string> theirs;
        for (const Customization* c : entries) theirs.push_back(c->key);
        std::vector<std::string> ours;
        for (const Element* element : base.children(pair.first, true)) {
            for (const Customization* c : entries) {
                if (c->key == element->key) ours.push_back(element->key);
            }
        }
        const bool sameOrder = ours == theirs;
        for (const Customization* c : entries) {
            const Element* element = base.element(c->key);
            Customization entry;
            entry.key = c->key;
            if (c->container && *c->container != element->container) {
                entry.container = c->container;
            }
            if (!sameOrder && c->order && *c->order != element->order) entry.order = c->order;
            if (c->hidden && *c->hidden != element->hidden) entry.hidden = c->hidden;
            if (c->label && *c->label != element->userLabel) entry.label = c->label;
            if (!entry.empty()) out.customizations.add(entry);
        }
    }
    for (const AddedElement& a : read.customizations.added()) {
        out.customizations.add(a);
    }
    for (const BindingCustomization& b : read.customizations.bindings()) {
        bindings::Slots theirs = b.keys;
        bindings::Slots ours{};
        for (const auto& pair : base.defaultBindings()) {
            if (pair.first == b.commandId) ours = pair.second;
        }
        if (theirs != ours) out.customizations.add(b);
    }
    return out;
}

}  // namespace layout
}  // namespace hobbycad
