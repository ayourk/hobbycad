// =====================================================================
//  src/libhobbycad/document_undo.cpp — Document-level undo/redo
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "hobbycad/document_undo.h"

#include <algorithm>

namespace hobbycad {
namespace {

/// Index of the feature with this id, or -1.
int indexOfId(const std::vector<FeatureData>& features, int id)
{
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (features[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool insertAt(std::vector<FeatureData>& features, const FeatureData& f, int index)
{
    // An index past the end is clamped rather than refused: appending is the
    // common case and a recipe that grew since the command was recorded is
    // not a reason to lose the feature.
    if (index < 0 || index > static_cast<int>(features.size())) {
        index = static_cast<int>(features.size());
    }
    features.insert(features.begin() + index, f);
    return true;
}

bool removeId(std::vector<FeatureData>& features, int id)
{
    const int at = indexOfId(features, id);
    if (at < 0) {
        return false;
    }
    features.erase(features.begin() + at);
    return true;
}

bool replaceId(std::vector<FeatureData>& features, const FeatureData& f)
{
    const int at = indexOfId(features, f.id);
    if (at < 0) {
        return false;
    }
    features[at] = f;
    return true;
}

bool moveFeature(std::vector<FeatureData>& features, int from, int to)
{
    const int n = static_cast<int>(features.size());
    if (from < 0 || from >= n || to < 0 || to >= n) {
        return false;
    }
    if (from == to) {
        return true;
    }
    FeatureData f = features[from];
    features.erase(features.begin() + from);
    features.insert(features.begin() + to, f);
    return true;
}

}  // namespace

int DocumentCommand::operationCount() const
{
    if (!isCompound()) {
        return 1;
    }
    int n = 0;
    for (const DocumentCommand& c : subCommands) {
        n += c.operationCount();
    }
    return n;
}

DocumentCommand DocumentCommand::addFeature(const FeatureData& f, int index,
                                            const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::AddFeature;
    c.feature = f;
    c.index = index;
    c.description = desc.empty() ? ("Add " + f.name) : desc;
    return c;
}

DocumentCommand DocumentCommand::deleteFeature(const FeatureData& f, int index,
                                               const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::DeleteFeature;
    c.feature = f;
    c.index = index;
    c.description = desc.empty() ? ("Delete " + f.name) : desc;
    return c;
}

DocumentCommand DocumentCommand::modifyFeature(const FeatureData& before,
                                               const FeatureData& after,
                                               const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::ModifyFeature;
    c.previousFeature = before;
    c.feature = after;
    c.description = desc.empty() ? ("Edit " + after.name) : desc;
    return c;
}

DocumentCommand DocumentCommand::reorderFeature(int fromIndex, int toIndex,
                                                const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::ReorderFeature;
    c.previousIndex = fromIndex;
    c.index = toIndex;
    c.description = desc.empty() ? "Reorder feature" : desc;
    return c;
}

DocumentCommand DocumentCommand::modifySketch(int sketchFeatureId,
                                             const SketchContents& before,
                                             const SketchContents& after,
                                             const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::ModifySketch;
    c.sketchFeatureId = sketchFeatureId;
    c.sketchBefore = before;
    c.sketchAfter = after;
    c.description = desc.empty() ? "Edit sketch" : desc;
    return c;
}

DocumentCommand DocumentCommand::compound(const std::vector<DocumentCommand>& cmds,
                                          const std::string& desc)
{
    DocumentCommand c;
    c.type = DocumentCommandType::Compound;
    c.subCommands = cmds;
    c.description = desc.empty() ? "Multiple changes" : desc;
    return c;
}

bool isProjectListCommand(const DocumentCommand& cmd)
{
    if (cmd.type == DocumentCommandType::ModifyProjectList) return true;
    // A compound is a project-list command if any step is: the host has to
    // route the whole thing through the project applier.
    for (const auto& c : cmd.subCommands) {
        if (isProjectListCommand(c)) return true;
    }
    return false;
}

bool applyUndo(const DocumentCommand& cmd, std::vector<FeatureData>& features)
{
    switch (cmd.type) {
    case DocumentCommandType::AddFeature:
        return removeId(features, cmd.feature.id);

    case DocumentCommandType::DeleteFeature:
        // Restore at the recorded position: undoing a delete has to put the
        // feature back where it was, because later features may depend on
        // ordering.
        return insertAt(features, cmd.feature, cmd.index);

    case DocumentCommandType::ModifyFeature:
        return replaceId(features, cmd.previousFeature);

    case DocumentCommandType::ReorderFeature:
        return moveFeature(features, cmd.index, cmd.previousIndex);

    case DocumentCommandType::ModifySketch:
        // The FEATURE LIST is unchanged by a sketch edit: the recipe still
        // holds the same sketch in the same place. The geometry payload is
        // applied by whoever owns the sketches and their views, which is the
        // undo host; see MainWindow::undoDocument(). Reporting success here
        // is correct: there is nothing about this command for a feature-list
        // applier to do.
        return true;

    case DocumentCommandType::ModifyProjectList:
        // Deliberately false: this command is about Project's own object
        // lists, and a caller holding only a feature vector cannot carry it
        // out. Returning true here would report an undo that never happened.
        return false;

    case DocumentCommandType::Compound: {
        // Reverse order, and ALL-OR-NOTHING: a half-undone compound would
        // leave the recipe in a state the user never created. Work on a copy
        // and only publish it if every step succeeded.
        std::vector<FeatureData> scratch = features;
        for (auto it = cmd.subCommands.rbegin(); it != cmd.subCommands.rend(); ++it) {
            if (!applyUndo(*it, scratch)) {
                return false;
            }
        }
        features = scratch;
        return true;
    }
    }
    return false;
}

bool applyRedo(const DocumentCommand& cmd, std::vector<FeatureData>& features)
{
    switch (cmd.type) {
    case DocumentCommandType::AddFeature:
        return insertAt(features, cmd.feature, cmd.index);

    case DocumentCommandType::DeleteFeature:
        return removeId(features, cmd.feature.id);

    case DocumentCommandType::ModifyFeature:
        return replaceId(features, cmd.feature);

    case DocumentCommandType::ReorderFeature:
        return moveFeature(features, cmd.previousIndex, cmd.index);

    case DocumentCommandType::ModifySketch:
        // The FEATURE LIST is unchanged by a sketch edit: the recipe still
        // holds the same sketch in the same place. The geometry payload is
        // applied by whoever owns the sketches and their views, which is the
        // undo host; see MainWindow::undoDocument(). Reporting success here
        // is correct: there is nothing about this command for a feature-list
        // applier to do.
        return true;

    case DocumentCommandType::ModifyProjectList:
        // Deliberately false, matching applyUndo(): this command is about
        // Project's own object lists, which a feature vector cannot carry
        // out. Returning true would report a redo that never happened.
        return false;

    case DocumentCommandType::Compound: {
        std::vector<FeatureData> scratch = features;
        for (const DocumentCommand& c : cmd.subCommands) {
            if (!applyRedo(c, scratch)) {
                return false;
            }
        }
        features = scratch;
        return true;
    }
    }
    return false;
}

DocumentUndoStack::DocumentUndoStack(int maxSize)
    : m_maxSize(maxSize > 0 ? maxSize : 1)
{
}

void DocumentUndoStack::push(const DocumentCommand& command)
{
    if (m_recordingCompound) {
        m_compoundBuffer.push_back(command);
        return;
    }
    m_undoStack.push_back(command);
    m_redoStack.clear();      // a new edit invalidates any redo future
    m_modified = true;
    trimToMaxSize();
}

DocumentCommand DocumentUndoStack::undo()
{
    if (m_undoStack.empty()) {
        return {};
    }
    DocumentCommand c = m_undoStack.back();
    m_undoStack.pop_back();
    m_redoStack.push_back(c);
    m_modified = true;
    return c;
}

DocumentCommand DocumentUndoStack::redo()
{
    if (m_redoStack.empty()) {
        return {};
    }
    DocumentCommand c = m_redoStack.back();
    m_redoStack.pop_back();
    m_undoStack.push_back(c);
    m_modified = true;
    return c;
}

std::string DocumentUndoStack::undoDescription() const
{
    return m_undoStack.empty() ? std::string() : m_undoStack.back().description;
}

std::string DocumentUndoStack::redoDescription() const
{
    return m_redoStack.empty() ? std::string() : m_redoStack.back().description;
}

std::vector<std::string> DocumentUndoStack::undoDescriptions() const
{
    std::vector<std::string> out;
    out.reserve(m_undoStack.size());
    // Most recent first, which is the order a menu lists them in.
    for (auto it = m_undoStack.rbegin(); it != m_undoStack.rend(); ++it) {
        out.push_back(it->description);
    }
    return out;
}

std::vector<std::string> DocumentUndoStack::redoDescriptions() const
{
    std::vector<std::string> out;
    out.reserve(m_redoStack.size());
    for (auto it = m_redoStack.rbegin(); it != m_redoStack.rend(); ++it) {
        out.push_back(it->description);
    }
    return out;
}

void DocumentUndoStack::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_compoundBuffer.clear();
    m_recordingCompound = false;
    m_modified = false;
}

void DocumentUndoStack::clearRedo()
{
    m_redoStack.clear();
}

void DocumentUndoStack::setMaxSize(int maxSize)
{
    m_maxSize = (maxSize > 0) ? maxSize : 1;
    trimToMaxSize();
}

void DocumentUndoStack::beginCompound(const std::string& description)
{
    if (m_recordingCompound) {
        return;               // already recording; nesting collapses
    }
    m_recordingCompound = true;
    m_compoundDescription = description;
    m_compoundBuffer.clear();
}

void DocumentUndoStack::endCompound()
{
    if (!m_recordingCompound) {
        return;
    }
    m_recordingCompound = false;
    if (m_compoundBuffer.empty()) {
        return;               // nothing happened; do not push an empty step
    }
    // A single operation does not need a Compound wrapper; unwrapping it
    // keeps the undo menu readable ("Delete Extrude1", not "Multiple changes").
    DocumentCommand c = (m_compoundBuffer.size() == 1)
        ? m_compoundBuffer.front()
        : DocumentCommand::compound(m_compoundBuffer, m_compoundDescription);
    m_compoundBuffer.clear();
    push(c);
}

void DocumentUndoStack::trimToMaxSize()
{
    while (static_cast<int>(m_undoStack.size()) > m_maxSize) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

}  // namespace hobbycad
