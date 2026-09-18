// =====================================================================
//  src/libhobbycad/hobbycad/sketch/dimension_input.h — typing a value
//  while placing geometry
// =====================================================================
//
//  Capability tier of the front-end support layer. The fields a tool
//  offers while an entity is being placed ("Length", "Angle"), and the
//  editing that goes on in them: typing an expression, moving the cursor,
//  Enter to lock the value (the placement then follows it), Tab to the
//  next field, Escape to clear or unlock. A front end turns its key events
//  into DimKeyPress and draws the fields from what this reports.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_DIMENSION_INPUT_H
#define HOBBYCAD_SKETCH_DIMENSION_INPUT_H

#include "../core.h"
#include "../units.h"
#include "dimension_field.h"

#include <string>
#include <vector>

namespace hobbycad {

class ParameterEngine;

namespace sketch {

/// The editing keys a field responds to. Characters arrive as text.
enum class DimKey {
    None,        ///< a character (DimKeyPress::character) or an unhandled key
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Enter,
    Tab,
    Backtab,     ///< Shift+Tab
    Escape,
};

/// One key press, as a front end reports it.
struct DimKeyPress {
    DimKey key = DimKey::None;
    char32_t character = 0;   ///< the typed character, 0 for none
};

/// What a key press did, for the front end to follow up on.
struct DimKeyOutcome {
    bool consumed = false;   ///< the field used the key; the front end must not
    bool repaint = false;    ///< the fields' appearance changed
    bool locked = false;     ///< a value was locked: re-apply the placement
    bool unlocked = false;   ///< a value was unlocked: re-apply without it
};

/// How a field looks right now.
enum class DimFieldLook {
    Inactive,   ///< showing the live value, not the active field
    Live,       ///< the active field, showing the live value
    Selected,   ///< the active field, its value selected (typing replaces it)
    Typing,     ///< the active field, being edited
    Locked,     ///< its value is locked
};

/// One field and its editing state.
struct DimFieldEntry {
    DimField id = DimField::Length;
    std::string label;             ///< display name, as the front end gave it
    bool isAngle = false;          ///< degrees, else a length in millimeters
    double liveValue = 0.0;        ///< what the placement measures right now
    std::u32string buffer;         ///< typed so far; empty when not typing
    std::size_t cursor = 0;        ///< insertion point within `buffer`
    bool selectAll = false;        ///< the whole buffer is selected
    bool locked = false;
    double lockedValue = 0.0;      ///< millimeters or degrees
};

/// The fields of the placement in progress.
class HOBBYCAD_EXPORT DimensionInput {
public:
    void setDisplayUnit(LengthUnit unit) { m_unit = unit; }
    LengthUnit displayUnit() const { return m_unit; }
    /// Parameters an expression may use; may be null.
    void setParameterEngine(const ParameterEngine* engine) { m_parameters = engine; }

    // ---- Lifecycle ---------------------------------------------------

    /// Forget everything, including the values locked in earlier stages.
    void clearAll();
    /// Add a field for the stage now being placed.
    void addField(DimField id, const std::string& label);
    /// Keep this stage's locked values for the constraints, then drop its
    /// fields, before the next stage adds its own.
    void reinitForNextStage();
    /// Make the first field active; call after the stage's fields are added.
    void beginStates();
    /// Keep the locked values of the current fields for the constraints.
    void flushLocked();
    /// Forget the values kept for the constraints.
    void clearLocked();

    // ---- State -------------------------------------------------------

    bool empty() const { return m_fields.empty(); }
    int fieldCount() const { return static_cast<int>(m_fields.size()); }
    int activeIndex() const { return m_active; }
    const DimFieldEntry& field(int index) const
    {
        return m_fields.at(static_cast<std::size_t>(index));
    }
    /// Set what the placement measures for a field.
    void setLiveValue(int index, double value);
    bool allLocked() const;
    /// A field's locked value, or -1 when it is not locked.
    double lockedValue(int index) const;
    /// The values locked so far, in every stage, for the constraints the
    /// finished entity gets.
    const LockedDims& lockedForConstraints() const { return m_lockedForConstraints; }

    // ---- Editing -----------------------------------------------------

    /// Put a field's live value in its buffer, selected, ready to be
    /// replaced by typing.
    void prefill(int index);
    /// Handle a key while a field is active. `chains` is true when the tool
    /// strings segments together: an empty Enter then ends the chain and is
    /// left to the front end.
    DimKeyOutcome handleKey(const DimKeyPress& press, bool chains);

    // ---- Showing -----------------------------------------------------

    /// How a field looks.
    DimFieldLook look(int index) const;
    /// The text a field shows, without its label: the formatted value, or
    /// what is being typed (the cursor is at typedCursor()).
    std::string text(int index) const;
    /// Where the cursor sits in text() while typing, in characters.
    std::size_t typedCursor(int index) const;

    /// A typed expression's value: millimeters for a length (the display
    /// unit applies to bare numbers), degrees for an angle (degrees,
    /// minutes and seconds accepted). Parameters come from the engine set
    /// with setParameterEngine().
    double evaluate(const std::string& expression, bool isAngle) const;

private:
    void advanceTo(int step);

    std::vector<DimFieldEntry> m_fields;
    int m_active = -1;
    LockedDims m_lockedForConstraints;
    LengthUnit m_unit = LengthUnit::Millimeters;
    const ParameterEngine* m_parameters = nullptr;
};

/// True when `c` may be typed into a field: digits, letters, and the
/// characters an expression or an angle uses.
HOBBYCAD_EXPORT bool isDimFieldCharacter(char32_t c);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_DIMENSION_INPUT_H
