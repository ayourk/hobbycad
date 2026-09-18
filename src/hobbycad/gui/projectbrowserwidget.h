// =====================================================================
//  src/hobbycad/gui/projectbrowserwidget.h — Project Files Browser
// =====================================================================
//
//  A dockable widget that displays the project directory structure.
//  Shows all files in the project, with special styling for:
//  - CAD files (listed in manifest)
//  - Foreign files (user-added, tracked separately)
//  - Untracked files (not in manifest or foreign_files)
//
//  Features:
//  - Tree view of project directory
//  - Right-click context menu for file operations
//  - Toolbar with common actions
//  - Drag-and-drop support for adding files
//  - Double-click to open files
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PROJECTBROWSERWIDGET_H
#define HOBBYCAD_PROJECTBROWSERWIDGET_H

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QToolBar>
#include <QMenu>
#include <QStringList>

#include <hobbycad/project_files.h>

#include <set>
#include <string>
#include <vector>

namespace hobbycad {

class Project;

/// Category for foreign files
struct ForeignFileEntry {
    QString path;           ///< Relative path from project root
    QString description;    ///< Optional description
    QString category;       ///< Category (version_control, documentation, etc.)
};

/// Custom file system model with project-aware styling
class ProjectFileModel : public QFileSystemModel {
    Q_OBJECT

public:
    explicit ProjectFileModel(QObject* parent = nullptr);

    // Override for custom display
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Project file tracking
    void setProjectRoot(const QString& path);
    void setCadFiles(const QStringList& files);
    void setForeignFiles(const QVector<ForeignFileEntry>& files);
    void setGitIgnore(const GitIgnore& gitIgnore);
    void refresh();

    // Query file status
    ProjectFileStatus fileStatus(const QString& relativePath, bool isDirectory) const;
    bool isCadFile(const QString& relativePath) const;
    bool isForeignFile(const QString& relativePath) const;
    bool isGitIgnored(const QString& relativePath, bool isDirectory) const;

private:
    QString m_projectRoot;
    std::set<std::string> m_cadFiles;
    std::vector<std::string> m_foreignFiles;
    GitIgnore m_gitIgnore;
    QVector<ForeignFileEntry> m_foreignFileEntries;

    QString relativePath(const QModelIndex& index) const;
};

/// Project Files Browser widget
class ProjectBrowserWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProjectBrowserWidget(QWidget* parent = nullptr);

    /// Set the project to display
    void setProject(Project* project);

    /// Refresh the file tree
    void refresh();

    /// Get the currently selected file path (absolute)
    QString selectedFilePath() const;

    /// Get the currently selected relative path
    QString selectedRelativePath() const;

signals:
    /// Emitted when a file is double-clicked
    void fileDoubleClicked(const QString& absolutePath);

    /// Emitted when a CAD file should be opened
    void openCadFileRequested(const QString& relativePath);

    /// Emitted when foreign_files list changes
    void foreignFilesChanged();

    /// Emitted when .gitignore changes
    void gitIgnoreChanged();

    /// Emitted when files are added via drag-drop
    void filesDropped(const QStringList& absolutePaths);

public slots:
    /// Expand all items
    void expandAll();

    /// Collapse all items
    void collapseAll();

private slots:
    void onItemDoubleClicked(const QModelIndex& index);
    void onCustomContextMenu(const QPoint& pos);
    void onSelectionChanged();

    // Toolbar actions
    void onAddFile();
    void onAddFolder();
    void onRemoveSelected();
    void onToggleForeignFile();
    void onToggleGitIgnore();
    void onRefresh();
    void onOpenInExternalEditor();

    // Context menu actions
    void onRename();
    void onDelete();
    void onShowProperties();
    void onRevealInFileManager();

private:
    void setupUi();
    void setupToolbar();
    void setupContextMenu();
    void updateToolbarState();
    void loadProjectFiles();
    void loadGitIgnore();
    void saveGitIgnore();

    // File operations
    bool addToForeignFiles(const QString& relativePath, const QString& category = QString());
    bool removeFromForeignFiles(const QString& relativePath);
    bool addToGitIgnore(const QString& relativePath);
    bool removeFromGitIgnore(const QString& relativePath);
    bool isInGitIgnore(const QString& relativePath) const;
    /// True when .gitignore can stop ignoring the path (no folder above it
    /// is ignored).
    bool canRemoveFromGitIgnore(const QString& relativePath) const;
    bool isDirectory(const QString& relativePath) const;

    // Helpers
    QString absolutePath(const QString& relativePath) const;

    // Project reference
    Project* m_project = nullptr;
    QString m_projectRoot;

    // UI components
    QToolBar* m_toolbar = nullptr;
    QTreeView* m_treeView = nullptr;
    ProjectFileModel* m_model = nullptr;
    QMenu* m_contextMenu = nullptr;

    // Toolbar actions
    QAction* m_actionAddFile = nullptr;
    QAction* m_actionAddFolder = nullptr;
    QAction* m_actionRemove = nullptr;
    QAction* m_actionToggleForeign = nullptr;
    QAction* m_actionToggleGitIgnore = nullptr;
    QAction* m_actionRefresh = nullptr;

    // Context menu actions
    QAction* m_actionOpen = nullptr;
    QAction* m_actionOpenExternal = nullptr;
    QAction* m_actionRename = nullptr;
    QAction* m_actionDelete = nullptr;
    QAction* m_actionProperties = nullptr;
    QAction* m_actionRevealInFileManager = nullptr;
    QAction* m_actionAddToForeign = nullptr;
    QAction* m_actionRemoveFromForeign = nullptr;
    QAction* m_actionAddToGitIgnore = nullptr;
    QAction* m_actionRemoveFromGitIgnore = nullptr;

    // Git ignore patterns (cached)
    GitIgnore m_gitIgnore;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECTBROWSERWIDGET_H
