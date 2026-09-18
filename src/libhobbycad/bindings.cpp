// =====================================================================
//  src/libhobbycad/bindings.cpp — key and mouse bindings by command
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/bindings.h>

#include <hobbycad/commands.h>

#include <algorithm>
#include <cctype>

namespace hobbycad {
namespace bindings {

namespace {

const Slots kNoSlots{};

std::string trimmed(const std::string& s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lowered(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// The binding context of a command: its registry context, or the id's
/// first component for an id the registry does not know.
std::string contextOf(const std::string& commandId)
{
    if (const commands::Command* c = commands::findCommand(commandId)) return c->context;
    const std::size_t dot = commandId.find('.');
    return dot == std::string::npos ? std::string() : commandId.substr(0, dot);
}

/// True when two alternative lists share an entry.
bool sharesAlternative(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    for (const std::string& x : a) {
        if (std::find(b.begin(), b.end(), x) != b.end()) return true;
    }
    return false;
}

/// Every alternative of every non-empty slot, keys and mouse gestures alike.
std::vector<std::string> allAlternatives(const Slots& slots)
{
    std::vector<std::string> out;
    for (const std::string& s : slots) {
        for (std::string& part : splitAlternatives(s)) out.push_back(std::move(part));
    }
    return out;
}

}  // namespace

InputKind classify(const std::string& binding)
{
    const std::string t = trimmed(binding);
    if (t.empty()) return InputKind::None;
    const std::string l = lowered(t);
    for (const char* word : {"button", "wheel", "drag", "click"}) {
        if (l.find(word) != std::string::npos) return InputKind::Mouse;
    }
    return InputKind::Keyboard;
}

std::vector<std::string> splitAlternatives(const std::string& binding)
{
    std::vector<std::string> out;
    std::string token;
    for (char ch : binding) {
        if (ch == ',') {
            const std::string t = trimmed(token);
            // A comma with nothing before it, or right after a '+', is the
            // comma key itself.
            if (t.empty() || t.back() == '+') {
                token.push_back(ch);
                continue;
            }
            out.push_back(t);
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    const std::string t = trimmed(token);
    if (!t.empty()) out.push_back(t);
    return out;
}

std::vector<std::string> keySequences(const Slots& slots)
{
    std::vector<std::string> out;
    for (const std::string& s : slots) {
        if (classify(s) != InputKind::Keyboard) continue;
        for (std::string& part : splitAlternatives(s)) out.push_back(std::move(part));
    }
    return out;
}

std::string standardKeyName(const std::string& binding)
{
    static const std::string prefix = "std:";
    if (binding.compare(0, prefix.size(), prefix) != 0) return std::string();
    return binding.substr(prefix.size());
}

bool contextsOverlap(const std::string& a, const std::string& b)
{
    const commands::BindingContext* ca = commands::findBindingContext(a);
    const commands::BindingContext* cb = commands::findBindingContext(b);
    const auto wide = [](const commands::BindingContext* c) {
        return !c || c->scope != commands::BindingScope::Surface;
    };
    if (wide(ca) || wide(cb)) return true;
    return std::string(ca->surface) == cb->surface;
}

// ---- Table ------------------------------------------------------------

void Table::addCommand(const std::string& commandId, const Slots& defaults)
{
    auto it = m_entries.find(commandId);
    if (it == m_entries.end()) {
        m_order.push_back(commandId);
        m_entries.emplace(commandId, Entry{defaults, defaults});
        return;
    }
    it->second = Entry{defaults, defaults};
}

bool Table::contains(const std::string& commandId) const
{
    return m_entries.count(commandId) != 0;
}

const Slots& Table::current(const std::string& commandId) const
{
    const auto it = m_entries.find(commandId);
    return it == m_entries.end() ? kNoSlots : it->second.current;
}

const Slots& Table::defaults(const std::string& commandId) const
{
    const auto it = m_entries.find(commandId);
    return it == m_entries.end() ? kNoSlots : it->second.defaults;
}

bool Table::set(const std::string& commandId, int slot, const std::string& binding)
{
    const auto it = m_entries.find(commandId);
    if (it == m_entries.end() || slot < 0 || slot >= kSlotCount) return false;
    it->second.current[static_cast<std::size_t>(slot)] = binding;
    return true;
}

bool Table::isDefault(const std::string& commandId, int slot) const
{
    const auto it = m_entries.find(commandId);
    if (it == m_entries.end() || slot < 0 || slot >= kSlotCount) return true;
    const auto i = static_cast<std::size_t>(slot);
    return it->second.current[i] == it->second.defaults[i];
}

void Table::restore(const std::string& commandId)
{
    const auto it = m_entries.find(commandId);
    if (it != m_entries.end()) it->second.current = it->second.defaults;
}

void Table::restoreAll()
{
    for (auto& kv : m_entries) kv.second.current = kv.second.defaults;
}

std::string Table::findConflict(const std::string& commandId,
                                const std::string& binding) const
{
    const std::vector<std::string> mine = splitAlternatives(binding);
    if (mine.empty()) return std::string();
    const std::string myContext = contextOf(commandId);
    for (const std::string& other : m_order) {
        if (other == commandId) continue;
        if (!contextsOverlap(myContext, contextOf(other))) continue;
        if (sharesAlternative(mine, allAlternatives(m_entries.at(other).current))) {
            return other;
        }
    }
    return std::string();
}

std::vector<std::pair<std::string, std::string>> Table::conflicts() const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        const std::vector<std::string> a = allAlternatives(m_entries.at(m_order[i]).current);
        if (a.empty()) continue;
        const std::string ca = contextOf(m_order[i]);
        for (std::size_t j = i + 1; j < m_order.size(); ++j) {
            if (!contextsOverlap(ca, contextOf(m_order[j]))) continue;
            if (sharesAlternative(a, allAlternatives(m_entries.at(m_order[j]).current))) {
                out.emplace_back(m_order[i], m_order[j]);
            }
        }
    }
    return out;
}

std::string Table::commandForKey(const std::string& contextId,
                                 const std::string& sequence) const
{
    const std::string key = trimmed(sequence);
    if (key.empty()) return std::string();
    const commands::BindingContext* here = commands::findBindingContext(contextId);
    const auto heardHere = [&](const std::string& other) {
        if (other == contextId) return true;
        const commands::BindingContext* there = commands::findBindingContext(other);
        return here && there && here->scope == commands::BindingScope::Surface
               && there->scope == commands::BindingScope::Surface
               && std::string(here->surface) == there->surface;
    };
    for (const std::string& id : m_order) {
        if (!heardHere(contextOf(id))) continue;
        const std::vector<std::string> keys = keySequences(m_entries.at(id).current);
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) return id;
    }
    return std::string();
}

}  // namespace bindings
}  // namespace hobbycad
