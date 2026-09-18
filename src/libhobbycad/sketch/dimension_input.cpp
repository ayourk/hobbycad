// =====================================================================
//  src/libhobbycad/sketch/dimension_input.cpp — typing a value while
//  placing geometry
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/dimension_input.h>

#include <hobbycad/parameters.h>
#include <hobbycad/strutil.h>

#include <iterator>

namespace hobbycad {
namespace sketch {

namespace {

std::string toUtf8(const std::u32string& s)
{
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::u32string toUtf32(const std::string& s)
{
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        char32_t c = lead;
        if (lead >= 0xF0) { len = 4; c = lead & 0x07; }
        else if (lead >= 0xE0) { len = 3; c = lead & 0x0F; }
        else if (lead >= 0xC0) { len = 2; c = lead & 0x1F; }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) {
            c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        out.push_back(c);
        i += len;
    }
    return out;
}

/// The formatted value a field shows when nothing is being typed.
std::string valueText(const DimFieldEntry& f, double value, LengthUnit unit)
{
    return f.isAngle ? formatAngle(value) : formatValueWithUnit(value, unit);
}

}  // namespace

bool isDimFieldCharacter(char32_t c)
{
    if ((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) {
        return true;
    }
    switch (c) {
    case U'_': case U'.': case U'-': case U'+': case U'*': case U'/': case U'^':
    case U'(': case U')': case U',': case U' ': case U'%':
    case U'\u00B0':   // degree sign
    case U'\'':       // arcminute
    case U'"':        // arcsecond
    case U'\u2032':   // prime
    case U'\u2033':   // double prime
        return true;
    default:
        return false;
    }
}

// ---- Lifecycle ----------------------------------------------------------

void DimensionInput::clearAll()
{
    m_fields.clear();
    m_active = -1;
    m_lockedForConstraints.clear();
}

void DimensionInput::addField(DimField id, const std::string& label)
{
    DimFieldEntry f;
    f.id = id;
    f.label = label;
    f.isAngle = isAngleDimField(id);
    m_fields.push_back(f);
}

void DimensionInput::flushLocked()
{
    for (const DimFieldEntry& f : m_fields) {
        if (f.locked) m_lockedForConstraints.emplace_back(f.id, f.lockedValue);
    }
}

void DimensionInput::clearLocked()
{
    m_lockedForConstraints.clear();
}

void DimensionInput::reinitForNextStage()
{
    flushLocked();
    m_fields.clear();
    m_active = -1;
}

void DimensionInput::beginStates()
{
    for (DimFieldEntry& f : m_fields) {
        f.buffer.clear();
        f.cursor = 0;
        f.selectAll = false;
        f.locked = false;
        f.lockedValue = 0.0;
    }
    m_active = m_fields.empty() ? -1 : 0;
}

// ---- State --------------------------------------------------------------

void DimensionInput::setLiveValue(int index, double value)
{
    if (index >= 0 && index < fieldCount()) {
        m_fields[static_cast<std::size_t>(index)].liveValue = value;
    }
}

bool DimensionInput::allLocked() const
{
    if (m_fields.empty()) return false;
    for (const DimFieldEntry& f : m_fields) {
        if (!f.locked) return false;
    }
    return true;
}

double DimensionInput::lockedValue(int index) const
{
    if (index < 0 || index >= fieldCount()) return -1.0;
    const DimFieldEntry& f = m_fields[static_cast<std::size_t>(index)];
    return f.locked ? f.lockedValue : -1.0;
}

// ---- Editing ------------------------------------------------------------

void DimensionInput::prefill(int index)
{
    if (index < 0 || index >= fieldCount()) return;
    DimFieldEntry& f = m_fields[static_cast<std::size_t>(index)];
    if (f.locked) return;
    const double shown = f.isAngle ? f.liveValue : mmToUnit(f.liveValue, m_unit);
    f.buffer = toUtf32(formatValue(shown));
    f.cursor = f.buffer.size();
    f.selectAll = true;
}

double DimensionInput::evaluate(const std::string& expression, bool isAngle) const
{
    const std::string expr = trim(expression);
    double result = 0.0;
    if (isAngle) {
        // Degrees, minutes and seconds first, then an expression, then a
        // plain number (0 when it is none).
        if (parseDMS(expr, result)) return result;
        if (m_parameters && m_parameters->evaluateExpression(expr, result)) return result;
        return toDouble(expr);
    }
    // Lengths: bare numbers are in the display unit, the result in mm.
    if (m_parameters && m_parameters->evaluateExpression(expr, result, m_unit)) {
        return unitToMm(result, m_unit);
    }
    return parseValueWithUnit(expr, m_unit);
}

void DimensionInput::advanceTo(int step)
{
    const int n = fieldCount();
    for (int i = 1; i <= n; ++i) {
        const int next = ((m_active + step * i) % n + n) % n;
        if (!m_fields[static_cast<std::size_t>(next)].locked) {
            m_active = next;
            return;
        }
    }
}

DimKeyOutcome DimensionInput::handleKey(const DimKeyPress& press, bool chains)
{
    DimKeyOutcome out;
    if (m_active < 0 || m_active >= fieldCount()) return out;
    DimFieldEntry& f = m_fields[static_cast<std::size_t>(m_active)];
    const auto consumed = [&out]() {
        out.consumed = true;
        out.repaint = true;
        return out;
    };

    // A character: replaces the selection, else goes in at the cursor. A
    // locked field takes no typing.
    if (!f.locked && press.character != 0 && isDimFieldCharacter(press.character)) {
        if (f.selectAll) {
            f.buffer = std::u32string(1, press.character);
            f.cursor = 1;
            f.selectAll = false;
        } else {
            f.buffer.insert(f.cursor, 1, press.character);
            ++f.cursor;
        }
        return consumed();
    }

    switch (press.key) {
    case DimKey::Backspace:
        if (f.selectAll) {
            f.buffer.clear();
            f.cursor = 0;
            f.selectAll = false;
            return consumed();
        }
        if (f.cursor > 0) {
            f.buffer.erase(f.cursor - 1, 1);
            --f.cursor;
            return consumed();
        }
        return out;   // nothing to delete: the front end's Backspace applies

    case DimKey::Delete:
        if (f.selectAll) {
            f.buffer.clear();
            f.cursor = 0;
            f.selectAll = false;
            return consumed();
        }
        if (f.cursor < f.buffer.size()) {
            f.buffer.erase(f.cursor, 1);
            return consumed();
        }
        return out;

    // Moving the cursor ends a selection; the buffer is refreshed from the
    // live value first, so the cursor moves through what is shown.
    case DimKey::Left:
        if (f.selectAll) {
            prefill(m_active);
            f.cursor = 0;
            f.selectAll = false;
        } else if (f.cursor > 0) {
            --f.cursor;
        }
        return consumed();

    case DimKey::Right:
        if (f.selectAll) {
            prefill(m_active);
            f.selectAll = false;
        } else if (f.cursor < f.buffer.size()) {
            ++f.cursor;
        }
        return consumed();

    case DimKey::Home:
        if (f.selectAll) prefill(m_active);
        f.selectAll = false;
        f.cursor = 0;
        return consumed();

    case DimKey::End:
        if (f.selectAll) prefill(m_active);
        f.selectAll = false;
        f.cursor = f.buffer.size();
        return consumed();

    case DimKey::Enter: {
        // An empty Enter on a chaining tool ends the chain: the front end
        // cancels the segment in progress and keeps the committed ones.
        if (f.buffer.empty() && chains) return out;
        if (!f.buffer.empty()) {
            // A selected buffer is the live value, taken as it is.
            const double value =
                f.selectAll ? f.liveValue : evaluate(toUtf8(f.buffer), f.isAngle);
            if (f.isAngle || value > 0.0) {
                f.locked = true;
                f.lockedValue = value;
                f.buffer.clear();
                f.cursor = 0;
                f.selectAll = false;
                // On to the next unlocked field, ready to type over.
                for (int i = 1; i < fieldCount(); ++i) {
                    const int next = (m_active + i) % fieldCount();
                    if (!m_fields[static_cast<std::size_t>(next)].locked) {
                        m_active = next;
                        prefill(next);
                        break;
                    }
                }
                out.locked = true;
            }
        }
        return consumed();
    }

    case DimKey::Tab:
    case DimKey::Backtab:
        f.buffer.clear();
        f.cursor = 0;
        f.selectAll = false;
        if (fieldCount() > 1) advanceTo(press.key == DimKey::Backtab ? -1 : 1);
        prefill(m_active);
        return consumed();

    case DimKey::Escape: {
        if (!f.buffer.empty()) {
            f.buffer.clear();
            f.cursor = 0;
            f.selectAll = false;
            return consumed();
        }
        // Unlock the last locked angle, else the last locked length.
        int unlock = -1;
        for (int pass = 0; pass < 2 && unlock < 0; ++pass) {
            const bool angles = pass == 0;
            for (int i = fieldCount() - 1; i >= 0; --i) {
                const DimFieldEntry& g = m_fields[static_cast<std::size_t>(i)];
                if (g.locked && g.isAngle == angles) {
                    unlock = i;
                    break;
                }
            }
        }
        if (unlock < 0) return out;   // nothing to unlock: the front end cancels
        DimFieldEntry& g = m_fields[static_cast<std::size_t>(unlock)];
        g.locked = false;
        g.lockedValue = 0.0;
        m_active = unlock;
        for (auto it = m_lockedForConstraints.rbegin(); it != m_lockedForConstraints.rend();
             ++it) {
            if (it->first == g.id) {
                m_lockedForConstraints.erase(std::next(it).base());
                break;
            }
        }
        out.unlocked = true;
        return consumed();
    }

    case DimKey::None:
        break;
    }
    return out;
}

// ---- Showing ------------------------------------------------------------

DimFieldLook DimensionInput::look(int index) const
{
    const DimFieldEntry& f = field(index);
    if (f.locked) return DimFieldLook::Locked;
    if (index != m_active) return DimFieldLook::Inactive;
    if (f.buffer.empty()) return DimFieldLook::Live;
    return f.selectAll ? DimFieldLook::Selected : DimFieldLook::Typing;
}

std::string DimensionInput::text(int index) const
{
    const DimFieldEntry& f = field(index);
    switch (look(index)) {
    case DimFieldLook::Locked:
        return valueText(f, f.lockedValue, m_unit);
    case DimFieldLook::Selected:
        // The live value, without its unit: typing replaces the number.
        return formatValue(f.isAngle ? f.liveValue : mmToUnit(f.liveValue, m_unit));
    case DimFieldLook::Typing:
        return toUtf8(f.buffer);
    case DimFieldLook::Live:
    case DimFieldLook::Inactive:
        break;
    }
    return valueText(f, f.liveValue, m_unit);
}

std::size_t DimensionInput::typedCursor(int index) const
{
    const DimFieldEntry& f = field(index);
    return f.cursor <= f.buffer.size() ? f.cursor : f.buffer.size();
}

}  // namespace sketch
}  // namespace hobbycad
