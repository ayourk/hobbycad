// =====================================================================
//  src/libhobbycad/hobbycad/layout/arrangement.h — where HobbyCAD puts things
// =====================================================================
//
//  The arrangement tier of the front-end support layer: the menu bar, the
//  menus, the toolbars, the tool groups on them and the variants under a
//  tool, and the default key bindings. It names commands by id
//  (hobbycad/commands.h) and holds no text of its own, so a front end that
//  wants a different layout replaces this and keeps everything else.
//
//  An arrangement is a set of containers and the elements shown in them.
//  Each element carries a sort order number; a container shows its
//  elements in number order. The built-in default spaces the numbers apart
//  (100, 200, ...) so that moving one element later means changing that
//  element's number only, to any value between its new neighbors'.
//
//  Includes go one way: this may include capability headers, and no
//  capability header may include this.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_LAYOUT_ARRANGEMENT_H
#define HOBBYCAD_LAYOUT_ARRANGEMENT_H

#include "../bindings.h"
#include "../core.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hobbycad {
namespace layout {

/// Well-known container ids.
constexpr const char* kMenuBar = "menubar.main";
constexpr const char* kSketchToolbar = "toolbar.sketch";
constexpr const char* kModelToolbar = "toolbar.model";

/// The placeholder a front end fills with one entry per available language.
constexpr const char* kLanguagesPlaceholder = "languages";

/// The prefix of the container listing a tool's creation-mode variants;
/// the rest of the id is the tool's command id.
constexpr const char* kVariantsPrefix = "variants.";

/// Containers nest no deeper than this, counting the one at the top.
constexpr int kMaxDepth = 16;

/// What a container is.
enum class ContainerKind {
    MenuBar,
    Menu,
    Toolbar,
    ToolGroup,   ///< a toolbar button with a list of tools under it
    Variants,    ///< the creation modes offered for one tool
};

/// One container.
struct Container {
    std::string id;
    ContainerKind kind = ContainerKind::Menu;
    /// The command whose text names the container; "" when it has no name.
    std::string title;
    /// For a tool group: the command a click on the group runs before the
    /// user has picked one. "" means the click opens the list instead.
    std::string activates;
};

/// What an element is.
enum class ElementKind {
    Command,
    Container,     ///< a submenu or a group; `id` is the container's id
    Separator,
    Placeholder,   ///< filled by the front end; `id` names what goes there
};

/// One thing shown in a container.
struct Element {
    /// Unique within the arrangement and stable across versions:
    /// "<container in the default>/<id>", with separators numbered
    /// ("menu.file/separator.2"). Customizations refer to elements by key.
    std::string key;
    std::string id;
    ElementKind kind = ElementKind::Command;
    std::string container;
    double order = 0.0;
    bool hidden = false;
    /// The user's own name for the element, shown exactly as typed and
    /// never translated; "" shows the command's text.
    std::string userLabel;
};

/// A complete arrangement.
class HOBBYCAD_EXPORT Arrangement {
public:
    /// Add a container. A second container with the same id is ignored.
    void addContainer(const Container& container);

    /// Add an element at the end of `container` (its number is 100 past
    /// the largest there) and return its key.
    std::string append(ElementKind kind, const std::string& id,
                       const std::string& container);

    /// Add an element with a given number and return its key.
    std::string addElement(ElementKind kind, const std::string& id,
                           const std::string& container, double order);

    /// Add an element under a key the caller chooses, for the separators a
    /// person added to their own arrangement (layout/customization.h).
    /// False when the key is taken.
    bool addKeyedElement(const std::string& key, ElementKind kind, const std::string& id,
                         const std::string& container, double order);

    const std::vector<Container>& containers() const { return m_containers; }
    const Container* container(const std::string& id) const;

    const std::vector<Element>& elements() const { return m_elements; }
    const Element* element(const std::string& key) const;
    Element* element(const std::string& key);

    /// The elements of a container in the order shown: by number, and
    /// equal numbers in the order the elements were added (the default's
    /// order). Hidden ones are left out unless asked for.
    std::vector<const Element*> children(const std::string& containerId,
                                         bool includeHidden = false) const;

    /// The commands a container shows, in order, looking through nothing
    /// (a submenu is not expanded).
    std::vector<std::string> commandIds(const std::string& containerId) const;

    /// The container listing `commandId`'s variants, or nullptr when the
    /// command has none here.
    const Container* variantsOf(const std::string& commandId) const;

    /// The first tool group showing `commandId`, directly or as one of a
    /// tool's variants, or nullptr.
    const Container* groupOf(const std::string& commandId) const;

    /// Default bindings, in the order a bindings editor lists them.
    const std::vector<std::pair<std::string, bindings::Slots>>& defaultBindings() const
    {
        return m_bindings;
    }
    void addDefaultBinding(const std::string& commandId, const bindings::Slots& slots);

    /// Problems that would make a front end show something wrong: an
    /// unknown command, an element in an unknown container, a container
    /// shown in two places or inside itself, nesting deeper than kMaxDepth,
    /// a sort order that is not a number. Empty when there are none. (A
    /// second element with a key already used is never added.)
    std::vector<std::string> problems() const;

private:
    std::vector<Container> m_containers;
    std::vector<Element> m_elements;
    std::unordered_map<std::string, std::size_t> m_containerIndex;
    std::unordered_map<std::string, std::size_t> m_elementIndex;
    std::unordered_map<std::string, int> m_separatorCount;
    std::vector<std::pair<std::string, bindings::Slots>> m_bindings;
};

/// HobbyCAD's own arrangement.
HOBBYCAD_EXPORT const Arrangement& defaultArrangement();

}  // namespace layout
}  // namespace hobbycad

#endif  // HOBBYCAD_LAYOUT_ARRANGEMENT_H
