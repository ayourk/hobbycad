=====================================================================
  devtools/layout-metrics/README.txt — Layout measurement probes
=====================================================================

  A translation changes the width of the interface, and a caption
  that does not fit is elided rather than reported.  These probes
  answer "how wide is this string in this widget, in this locale"
  directly, so a fix can be aimed at the element that actually pins
  the width.

  Measure BEFORE editing any string.  On cherryrgb-qt a tab label
  was shortened twice (once to a word that meant the wrong thing)
  and neither shortening fixed the overflow, because the real
  constraint was a group-box title in a different column.  A
  shortening that does not fix the problem is pure loss: it costs
  correctness and buys nothing.

  Order that works:
    1. Measure which element pins the width.
    2. Only then look for a shorter wording, for that string alone.
    3. Re-measure: the budget has moved, and a wording rejected
       earlier may fit now.

  Ranked options when a caption overflows, cheapest first:
    a shorter natural synonym; moving detail to the tooltip and
    leaving a short label; wrapping (HobbyCAD toolbar captions
    already use an embedded newline: "Simple\nHole"); an
    abbreviation; and only then changing the layout.

  Note that the resolved font differs per locale, so a width taken
  under en_US is not the width under ja_JP.  Run the probe under
  the locale you are asking about.


  BUILD AND RUN
  --------------

    cd devtools/layout-metrics
    cmake -B build && cmake --build build
    LC_ALL=de_DE.UTF-8 QT_QPA_PLATFORM=offscreen \
        ./build/toolbarwidth $'Volumen-\nkörper'


  MEASURED — model toolbar group captions (Sans Serif 9pt)
  ---------------------------------------------------------

    language   widest caption          toolbar total
    English     77  Params                    ~584
    German     123  Volumenkörper             ~737   <- overflowed
    Japanese    90  フィレット / パラメータ       ~616
    Korean      75  매개변수                   ~508

  "Volumenkörper" alone was 61px over the English "Solid" and about
  40% of the entire German overflow.  Wrapping it as
  "Volumen-\nkörper" brings it to 89px and everything fits; no other
  caption needed touching, including "Verschieben" at 106px.

  "Körper" would be shorter still at 72px, but that is already the
  word the project tree uses for Bodies, so it was rejected as
  ambiguous rather than as too long.
