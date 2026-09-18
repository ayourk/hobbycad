// =====================================================================
//  src/libhobbycad/hobbycad/layout/customization.h — the user's own
//  arrangement, as customizations of the default
// =====================================================================
//
//  Arrangement tier. HobbyCAD ships one built-in arrangement
//  (defaultArrangement()) and works with no file at all. A person who
//  moves, hides or renames something gets one entry per element they
//  changed, like the hunks of a diff: an element with no entry is not
//  customized, so a command HobbyCAD adds later arrives in its default
//  place among customized neighbors, and an entry naming an element
//  HobbyCAD no longer has is dropped with a notice.
//
//  The file (arrangement.json in the user's configuration directory) does
//  not exist until something is customized, and is removed again when the
//  last entry goes.
//
//  Reading it is the one place in the library that parses a file a person
//  (or something pretending to be one) may have written by hand, so the
//  reader here is its own strict, allocation-capped JSON reader rather
//  than Qt's or nlohmann's: it caps the text's size, the nesting depth,
//  the number of entries and the length of every string, rejects
//  duplicate keys, an unknown key, a wrong type, a number that is not
//  finite and text that is not valid UTF-8, reports what it skipped, and
//  never throws. No field is ever used as a path, a command line or a
//  format string.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_LAYOUT_CUSTOMIZATION_H
#define HOBBYCAD_LAYOUT_CUSTOMIZATION_H

#include "arrangement.h"

#include <optional>
#include <string>
#include <vector>

namespace hobbycad {
namespace layout {

/// The file's limits. A file over the size cap is not parsed at all.
constexpr std::size_t kMaxFileBytes = 256u * 1024u;
constexpr std::size_t kMaxEntries = 4096;
constexpr std::size_t kMaxIdLength = 128;
constexpr std::size_t kMaxLabelLength = 96;

/// The version this library writes. A file from a later version is read
/// for what it understands.
constexpr int kCustomizationVersion = 1;

/// One element's customization. A field that is not set is not
/// customized; the default's value stands.
struct Customization {
    std::string key;                       ///< the element's key in the default
    std::optional<double> order;           ///< its sort order number
    std::optional<std::string> container;  ///< moved to this container
    std::optional<bool> hidden;
    /// The person's own name for it, shown exactly as typed and never
    /// translated. Restoring the element brings the translated default
    /// text back.
    std::optional<std::string> label;

    /// True when nothing is customized, so the entry can be dropped.
    bool empty() const { return !order && !container && !hidden && !label; }
};

/// A separator the person added, which the default does not have. Its
/// key is "user.separator.<number>", so hiding, moving or restoring it
/// works like any other element's.
struct AddedElement {
    std::string key;
    std::string container;
    double order = 0.0;
};

/// The prefix of a key for something the person added themselves.
constexpr const char* kAddedPrefix = "user.separator.";

/// One command's bindings, set by the person. All slots empty means the
/// command is deliberately unbound.
struct BindingCustomization {
    std::string commandId;
    /// Not called "slots": Qt makes that word a macro, and a front end
    /// includes this header after Qt's own.
    bindings::Slots keys{};
};

/// The parts of the arrangement "Restore defaults" can cover.
enum class RestoreArea {
    Menus,      ///< the menu bar and its menus
    Toolbars,   ///< the toolbars, their groups and a tool's variants
    Bindings,   ///< the key and mouse bindings
    All,
};

/// Everything the person has customized.
class HOBBYCAD_EXPORT Customizations {
public:
    const std::vector<Customization>& elements() const { return m_elements; }
    const std::vector<BindingCustomization>& bindings() const { return m_bindings; }
    const std::vector<AddedElement>& added() const { return m_added; }
    bool empty() const { return m_elements.empty() && m_bindings.empty() && m_added.empty(); }

    const Customization* find(const std::string& key) const;
    const BindingCustomization* findBinding(const std::string& commandId) const;

    /// Set one field of one element's entry, adding the entry if needed.
    void setOrder(const std::string& key, double order);
    void setContainer(const std::string& key, const std::string& container);
    void setHidden(const std::string& key, bool hidden);
    /// Stop customizing whether an element is shown, so the default says
    /// again. The entry goes when nothing else is customized.
    void clearHidden(const std::string& key);
    /// An empty label restores the default text.
    void setLabel(const std::string& key, const std::string& label);

    /// Bind a command. All-empty slots record it as unbound; to give the
    /// default back use restoreBinding().
    void setBinding(const std::string& commandId, const bindings::Slots& keys);

    /// Add a separator to `container` at `index` among what `resolved`
    /// shows there. Returns its key, or "" when the container is unknown.
    std::string addSeparator(const Arrangement& resolved, const std::string& container, int index);

    /// Put an element at `index` among the elements of `container` as
    /// `resolved` shows them, writing one entry: its number, and its
    /// container when that changes. Every other element keeps its number.
    bool move(const Arrangement& resolved, const std::string& key,
              const std::string& container, int index);

    /// Drop an element's entry, or a command's binding entry. True when
    /// there was one.
    bool restoreElement(const std::string& key);
    bool restoreBinding(const std::string& commandId);

    /// Drop the entries of an area, deciding by where `base` puts each
    /// element. Returns how many entries went.
    std::size_t restoreArea(RestoreArea area, const Arrangement& base);

    /// Add entries read from a file. Used by the reader and by import.
    void add(const Customization& entry);
    void add(const BindingCustomization& entry);
    void add(const AddedElement& entry);

private:
    Customization& entryFor(const std::string& key);

    std::vector<Customization> m_elements;
    std::vector<BindingCustomization> m_bindings;
    std::vector<AddedElement> m_added;
};

/// What reading produced: the entries that were good, and a line per entry
/// that was not. `usable` is false only when the text could not be read at
/// all (too large, not an object, junk), in which case the built-in
/// default stands.
struct ReadResult {
    Customizations customizations;
    std::vector<std::string> problems;
    bool usable = false;
};

/// Read arrangement.json. Entries naming an element or a command `base`
/// does not have are skipped with a notice, as are entries with an unknown
/// key, a wrong type, a label that is too long or holds control
/// characters, or an id outside the plain character set ids use.
HOBBYCAD_EXPORT ReadResult readCustomizations(const std::string& text, const Arrangement& base);

/// The text to write for `customizations`. An empty set writes "", which
/// the front end stores by removing the file.
HOBBYCAD_EXPORT std::string writeCustomizations(const Customizations& customizations);

/// `base` with the customizations applied: numbers, containers, hidden
/// flags, labels and bindings.
HOBBYCAD_EXPORT Arrangement resolve(const Arrangement& base,
                                    const Customizations& customizations);

/// A complete arrangement, for sharing: every element with its container,
/// number, hidden flag and label, and every command's bindings.
HOBBYCAD_EXPORT std::string exportArrangement(const Arrangement& resolved);

/// Read a complete arrangement and keep an entry only for each element
/// whose place in it differs from `base`: importing `base` itself writes
/// nothing. Read through the same hardened reader.
HOBBYCAD_EXPORT ReadResult importArrangement(const std::string& text, const Arrangement& base);

/// True when `label` is a name a person may give an element: within the
/// length cap, valid UTF-8, no control characters (an ampersand for a
/// mnemonic is fine).
HOBBYCAD_EXPORT bool isUserLabel(const std::string& label);

}  // namespace layout
}  // namespace hobbycad

#endif  // HOBBYCAD_LAYOUT_CUSTOMIZATION_H
