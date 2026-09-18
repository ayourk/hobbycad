// =====================================================================
//  tests/project/dimension_input.cpp — typing a value while placing
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The Length/Angle fields' editing lived inside the Qt canvas, so a
//  front end without Qt had to copy it. It is sketch::DimensionInput now;
//  this pins what the canvas relied on: typing replaces a selected value,
//  Enter locks and moves on, Tab cycles past locked fields, Escape clears
//  then unlocks (angles first), and expressions use parameters and units.
// =====================================================================
#include <hobbycad/parameters.h>
#include <hobbycad/sketch/dimension_input.h>

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

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static DimKeyOutcome key(DimensionInput& in, DimKey k, bool chains = false)
{
    DimKeyPress p;
    p.key = k;
    return in.handleKey(p, chains);
}

static void type(DimensionInput& in, const std::u32string& text)
{
    for (char32_t c : text) {
        DimKeyPress p;
        p.character = c;
        in.handleKey(p, false);
    }
}

static DimensionInput lengthAndAngle()
{
    DimensionInput in;
    in.addField(DimField::Length, "Length");
    in.addField(DimField::Angle, "Angle");
    in.beginStates();
    in.setLiveValue(0, 12.5);
    in.setLiveValue(1, 30.123456);   // more places than a field shows
    return in;
}

int main()
{
    std::printf("dimension input\n");

    // ---- typing and locking --------------------------------------------
    {
        DimensionInput in = lengthAndAngle();
        check(in.activeIndex() == 0 && in.look(0) == DimFieldLook::Live
                  && in.look(1) == DimFieldLook::Inactive,
              "the first field starts active, showing the live value");
        check(in.text(0) == "12.5 mm" && in.text(1) == "30.1235\xC2\xB0",
              "live values show with their unit");

        in.prefill(0);
        check(in.look(0) == DimFieldLook::Selected && in.text(0) == "12.5",
              "a prefilled field is selected, shown without its unit");
        type(in, U"4");
        check(in.look(0) == DimFieldLook::Typing && in.text(0) == "4",
              "typing replaces a selected value");
        type(in, U"0");
        key(in, DimKey::Left);
        type(in, U"1");
        check(in.text(0) == "410" && in.typedCursor(0) == 2, "Left moves the cursor");

        DimKeyOutcome out = key(in, DimKey::Enter);
        check(out.consumed && out.locked && out.repaint, "Enter locks a typed value");
        check(near(in.lockedValue(0), 410.0) && in.look(0) == DimFieldLook::Locked
                  && in.text(0) == "410 mm",
              "the locked value is kept and shown");
        check(in.activeIndex() == 1 && in.look(1) == DimFieldLook::Selected,
              "Enter moves on to the next field, prefilled");
        check(in.lockedValue(1) < 0 && !in.allLocked(), "the other field is still free");

        out = key(in, DimKey::Enter);
        check(out.locked && near(in.lockedValue(1), 30.123456) && in.allLocked(),
              "Enter on a selected value locks the live value as it is");

        // A locked field takes no typing.
        DimKeyPress p;
        p.character = U'7';
        check(!in.handleKey(p, false).consumed, "a locked field refuses typing");

        // Escape unlocks angles before lengths.
        out = key(in, DimKey::Escape);
        check(out.consumed && out.unlocked && in.lockedValue(1) < 0 && in.lockedValue(0) > 0
                  && in.activeIndex() == 1,
              "Escape unlocks the angle first and makes it active");
        out = key(in, DimKey::Escape);
        check(out.unlocked && in.lockedValue(0) < 0 && in.activeIndex() == 0,
              "a second Escape unlocks the length");
        out = key(in, DimKey::Escape);
        check(!out.consumed, "with nothing locked, Escape is left to the front end");
    }

    // ---- Enter that does not lock ---------------------------------------
    {
        DimensionInput in = lengthAndAngle();
        type(in, U"-3");
        DimKeyOutcome out = key(in, DimKey::Enter);
        check(out.consumed && !out.locked && in.lockedValue(0) < 0,
              "a length must be positive to lock");
        key(in, DimKey::Escape);
        check(in.look(0) == DimFieldLook::Live, "Escape clears what was typed");

        check(key(in, DimKey::Enter).consumed, "an empty Enter is swallowed");
        check(!key(in, DimKey::Enter, true).consumed,
              "an empty Enter on a chaining tool is left to the front end");

        in.setLiveValue(1, 0.0);
        key(in, DimKey::Tab);
        type(in, U"-45");
        out = key(in, DimKey::Enter);
        check(out.locked && near(in.lockedValue(1), -45.0), "a negative angle locks");
    }

    // ---- Tab and editing keys --------------------------------------------
    {
        DimensionInput in;
        in.addField(DimField::Width, "Width");
        in.addField(DimField::Height, "Height");
        in.addField(DimField::Angle, "Angle");
        in.beginStates();
        in.setLiveValue(1, 7.0);
        type(in, U"5");
        key(in, DimKey::Enter);   // Width locked, Height active
        key(in, DimKey::Tab);
        check(in.activeIndex() == 2, "Tab goes to the next field");
        key(in, DimKey::Tab);
        check(in.activeIndex() == 1, "Tab skips a locked field");
        key(in, DimKey::Backtab);
        check(in.activeIndex() == 2, "Shift+Tab goes back, skipping the locked field");
        key(in, DimKey::Backtab);
        check(in.activeIndex() == 1 && in.text(1) == "7",
              "the field reached is prefilled with its live value");

        key(in, DimKey::End);
        check(in.look(1) == DimFieldLook::Typing && in.typedCursor(1) == 1,
              "End ends the selection, cursor at the end");
        type(in, U"2");
        key(in, DimKey::Home);
        check(in.typedCursor(1) == 0, "Home moves to the start");
        check(!key(in, DimKey::Backspace).consumed,
              "Backspace at the start is left to the front end");
        check(key(in, DimKey::Delete).consumed && in.text(1) == "2",
              "Delete removes the character after the cursor");
        key(in, DimKey::Right);
        check(key(in, DimKey::Backspace).consumed && in.look(1) == DimFieldLook::Live,
              "Backspace removes the character before it");

        DimKeyPress bad;
        bad.character = U'$';
        check(!in.handleKey(bad, false).consumed, "a character no expression uses is refused");
        check(isDimFieldCharacter(U'\u2032') && isDimFieldCharacter(U'\u00B0')
                  && !isDimFieldCharacter(U'\u00E4'),
              "degree and prime marks are accepted, non-ASCII letters are not");
    }

    // ---- stages and constraints ------------------------------------------
    {
        DimensionInput in;
        in.addField(DimField::Radius, "Radius");
        in.beginStates();
        type(in, U"8");
        key(in, DimKey::Enter);
        in.reinitForNextStage();
        check(in.empty() && in.activeIndex() == -1, "the next stage starts with no fields");
        in.addField(DimField::SweepAngle, "Sweep Angle");
        in.beginStates();
        type(in, U"90");
        key(in, DimKey::Enter);
        in.flushLocked();
        const LockedDims& locked = in.lockedForConstraints();
        check(locked.size() == 2 && locked[0].first == DimField::Radius
                  && near(locked[0].second, 8.0) && locked[1].first == DimField::SweepAngle
                  && near(locked[1].second, 90.0),
              "values locked in every stage are kept for the constraints, in order");
        key(in, DimKey::Escape);
        check(in.lockedForConstraints().size() == 1
                  && in.lockedForConstraints()[0].first == DimField::Radius,
              "unlocking drops that field's kept value");
        in.clearAll();
        check(in.empty() && in.lockedForConstraints().empty(), "clearAll forgets everything");
    }

    // ---- evaluation -------------------------------------------------------
    {
        ParameterEngine params;
        params.setParameter("w", "20");
        params.evaluate();
        DimensionInput in;
        in.setDisplayUnit(LengthUnit::Inches);
        check(near(in.evaluate("2", false), 50.8), "a bare length is in the display unit");
        check(near(in.evaluate("10 mm", false), 10.0), "a length may name its unit");
        check(near(in.evaluate("w", false), 0.0), "without parameters, a name has no value");
        in.setParameterEngine(&params);
        check(near(in.evaluate(" w / 2 ", false), 254.0),
              "expressions use parameters, in the display unit");
        check(near(in.evaluate("45\xC2\xB0" "30'", true), 45.5),
              "angles accept degrees and minutes");
        check(near(in.evaluate("w * 3", true), 60.0), "angles accept expressions");

        in.addField(DimField::Length, "Length");
        in.beginStates();
        type(in, U"w+1");
        key(in, DimKey::Enter);
        check(near(in.lockedValue(0), 21.0 * 25.4), "a typed expression locks at its value");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
