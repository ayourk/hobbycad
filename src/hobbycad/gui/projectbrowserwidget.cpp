// =====================================================================
//  src/hobbycad/gui/projectbrowserwidget.cpp — Project Files Browser
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "projectbrowserwidget.h"

#include <hobbycad/project.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QApplication>
#include <QStyle>
#include <QToolButton>

namespace hobbycad {

// =====================================================================
//  ProjectFileModel
// =====================================================================

ProjectFileModel::ProjectFileModel(QObject* parent)
    : QFileSystemModel(parent)
{
    setReadOnly(false);  // Allow rename/delete operations
}

void ProjectFileModel::setProjectRoot(const QString& path)
{
    m_projectRoot = path;
    if (!path.isEmpty()) {
        setRootPath(path);
    }
}

void ProjectFileModel::setCadFiles(const QStringList& files)
{
    m_cadFiles.clear();
    for (const QString& file : files) {
        m_cadFiles.insert(file);
    }
}

void ProjectFileModel::setForeignFiles(const QVector<ForeignFileEntry>& files)
{
    m_foreignFiles.clear();
    m_foreignFileEntries = files;
    for (const ForeignFileEntry& entry : files) {
        m_foreignFiles.insert(entry.path);
    }
}

void ProjectFileModel::setGitIgnoredFiles(const QStringList& files)
{
    m_gitIgnoredFiles.clear();
    for (const QString& file : files) {
        m_gitIgnoredFiles.insert(file);
    }
}

void ProjectFileModel::refresh()
{
    // Force model to re-read directory
    if (!m_projectRoot.isEmpty()) {
        setRootPath(QString());
        setRootPath(m_projectRoot);
    }
}

QString ProjectFileModel::relativePath(const QModelIndex& index) const
{
    if (!index.isValid() || m_projectRoot.isEmpty()) {
        return QString();
    }

    QString absPath = filePath(index);
    if (absPath.startsWith(m_projectRoot)) {
        QString rel = absPath.mid(m_projectRoot.length());
        if (rel.startsWith(QLatin1Char('/'))) {
            rel = rel.mid(1);
        }
        return rel;
    }
    return QString();
}

ProjectFileStatus ProjectFileModel::fileStatus(const QString& relativePath) const
{
    if (relativePath.isEmpty()) {
        return ProjectFileStatus::Untracked;
    }

    // Check if it's a CAD file (in manifest)
    if (m_cadFiles.contains(relativePath)) {
        return ProjectFileStatus::CadFile;
    }

    // Check if it's a foreign file
    if (m_foreignFiles.contains(relativePath)) {
        return ProjectFileStatus::ForeignFile;
    }

    // Check parent directories for foreign files (e.g., "docs/" matches "docs/readme.txt")
    for (const QString& foreign : m_foreignFiles) {
        if (foreign.endsWith(QLatin1Char('/')) && relativePath.startsWith(foreign)) {
            return ProjectFileStatus::ForeignFile;
        }
    }

    // Check git ignore
    if (m_gitIgnoredFiles.contains(relativePath)) {
        return ProjectFileStatus::GitIgnored;
    }

    return ProjectFileStatus::Untracked;
}

bool ProjectFileModel::isCadFile(const QString& relativePath) const
{
    return m_cadFiles.contains(relativePath);
}

bool ProjectFileModel::isForeignFile(const QString& relativePath) const
{
    if (m_foreignFiles.contains(relativePath)) {
        return true;
    }
    // Check parent directories
    for (const QString& foreign : m_foreignFiles) {
        if (foreign.endsWith(QLatin1Char('/')) && relativePath.startsWith(foreign)) {
            return true;
        }
    }
    return false;
}

bool ProjectFileModel::isGitIgnored(const QString& relativePath) const
{
    return m_gitIgnoredFiles.contains(relativePath);
}

QVariant ProjectFileModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QFileSystemModel::data(index, role);
    }

    QString relPath = relativePath(index);

    if (role == Qt::ForegroundRole) {
        ProjectFileStatus status = fileStatus(relPath);
        switch (status) {
        case ProjectFileStatus::CadFile:
            return QColor(0, 120, 215);  // Blue for CAD files
        case ProjectFileStatus::ForeignFile:
            return QColor(100, 100, 100);  // Gray for foreign files
        case ProjectFileStatus::GitIgnored:
            return QColor(150, 150, 150);  // Light gray for ignored
        case ProjectFileStatus::Untracked:
            return QColor(180, 120, 0);  // Orange/brown for untracked
        }
    }

    if (role == Qt::FontRole) {
        ProjectFileStatus status = fileStatus(relPath);
        if (status == ProjectFileStatus::CadFile) {
            QFont font;
            font.setBold(true);
            return font;
        }
        if (status == ProjectFileStatus::GitIgnored) {
            QFont font;
            font.setItalic(true);
            return font;
        }
    }

    if (role == Qt::ToolTipRole) {
        ProjectFileStatus status = fileStatus(relPath);
        QString tooltip = filePath(index);

        switch (status) {
        case ProjectFileStatus::CadFile:
            tooltip += QStringLiteral("\n[CAD File - in manifest]");
            break;
        case ProjectFileStatus::ForeignFile:
            tooltip += QStringLiteral("\n[Foreign File - tracked separately]");
            // Add description if available
            for (const ForeignFileEntry& entry : m_foreignFileEntries) {
                if (entry.path == relPath && !entry.description.isEmpty()) {
                    tooltip += QStringLiteral("\n") + entry.description;
                    break;
                }
            }
            break;
        case ProjectFileStatus::GitIgnored:
            tooltip += QStringLiteral("\n[Git Ignored]");
            break;
        case ProjectFileStatus::Untracked:
            tooltip += QStringLiteral("\n[Untracked - not in manifest]");
            break;
        }

        return tooltip;
    }

    return QFileSystemModel::data(index, role);
}

Qt::ItemFlags ProjectFileModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QFileSystemModel::flags(index);

    if (index.isValid()) {
        // Allow drops on directories
        if (isDir(index)) {
            defaultFlags |= Qt::ItemIsDropEnabled;
        }
        // Allow dragging files
        defaultFlags |= Qt::ItemIsDragEnabled;
    }

    return defaultFlags;
}

// =====================================================================
//  ProjectBrowserWidget
// =====================================================================

ProjectBrowserWidget::ProjectBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setupToolbar();
    setupContextMenu();
}

void ProjectBrowserWidget::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    layout->addWidget(m_toolbar);

    // Tree view
    m_treeView = new QTreeView(this);
    m_treeView->setHeaderHidden(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed);

    // File system model
    m_model = new ProjectFileModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_treeView->setModel(m_model);

    // Hide all columns except name
    m_treeView->setColumnHidden(1, true);  // Size
    m_treeView->setColumnHidden(2, true);  // Type
    m_treeView->setColumnHidden(3, true);  // Date Modified

    layout->addWidget(m_treeView, 1);

    // Connections
    connect(m_treeView, &QTreeView::doubleClicked,
            this, &ProjectBrowserWidget::onItemDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &ProjectBrowserWidget::onCustomContextMenu);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ProjectBrowserWidget::onSelectionChanged);
}

void ProjectBrowserWidget::setupToolbar()
{
    QStyle* style = QApplication::style();

    // Add file
    m_actionAddFile = new QAction(style->standardIcon(QStyle::SP_FileIcon),
                                   tr("Add File"), this);
    m_actionAddFile->setToolTip(tr("Add a file to the project"));
    connect(m_actionAddFile, &QAction::triggered, this, &ProjectBrowserWidget::onAddFile);
    m_toolbar->addAction(m_actionAddFile);

    // Add folder
    m_actionAddFolder = new QAction(style->standardIcon(QStyle::SP_DirIcon),
                                     tr("Add Folder"), this);
    m_actionAddFolder->setToolTip(tr("Create a new folder"));
    connect(m_actionAddFolder, &QAction::triggered, this, &ProjectBrowserWidget::onAddFolder);
    m_toolbar->addAction(m_actionAddFolder);

    m_toolbar->addSeparator();

    // Remove
    m_actionRemove = new QAction(style->standardIcon(QStyle::SP_TrashIcon),
                                  tr("Remove"), this);
    m_actionRemove->setToolTip(tr("Remove selected file or folder"));
    m_actionRemove->setEnabled(false);
    connect(m_actionRemove, &QAction::triggered, this, &ProjectBrowserWidget::onRemoveSelected);
    m_toolbar->addAction(m_actionRemove);

    m_toolbar->addSeparator();

    // Toggle foreign file
    m_actionToggleForeign = new QAction(tr("F"), this);
    m_actionToggleForeign->setToolTip(tr("Toggle foreign file status"));
    m_actionToggleForeign->setCheckable(true);
    m_actionToggleForeign->setEnabled(false);
    connect(m_actionToggleForeign, &QAction::triggered,
            this, &ProjectBrowserWidget::onToggleForeignFile);
    m_toolbar->addAction(m_actionToggleForeign);

    // Toggle gitignore
    m_actionToggleGitIgnore = new QAction(tr("G"), this);
    m_actionToggleGitIgnore->setToolTip(tr("Toggle .gitignore status"));
    m_actionToggleGitIgnore->setCheckable(true);
    m_actionToggleGitIgnore->setEnabled(false);
    connect(m_actionToggleGitIgnore, &QAction::triggered,
            this, &ProjectBrowserWidget::onToggleGitIgnore);
    m_toolbar->addAction(m_actionToggleGitIgnore);

    m_toolbar->addSeparator();

    // Refresh
    m_actionRefresh = new QAction(style->standardIcon(QStyle::SP_BrowserReload),
                                   tr("Refresh"), this);
    m_actionRefresh->setToolTip(tr("Refresh the file tree"));
    connect(m_actionRefresh, &QAction::triggered, this, &ProjectBrowserWidget::onRefresh);
    m_toolbar->addAction(m_actionRefresh);
}

void ProjectBrowserWidget::setupContextMenu()
{
    QStyle* style = QApplication::style();

    m_contextMenu = new QMenu(this);

    // Open
    m_actionOpen = new QAction(tr("Open"), this);
    connect(m_actionOpen, &QAction::triggered, this, [this]() {
        QModelIndex index = m_treeView->currentIndex();
        if (index.isValid()) {
            onItemDoubleClicked(index);
        }
    });
    m_contextMenu->addAction(m_actionOpen);

    // Open in external editor
    m_actionOpenExternal = new QAction(tr("Open in External Editor"), this);
    connect(m_actionOpenExternal, &QAction::triggered,
            this, &ProjectBrowserWidget::onOpenInExternalEditor);
    m_contextMenu->addAction(m_actionOpenExternal);

    // Reveal in file manager
    m_actionRevealInFileManager = new QAction(tr("Reveal in File Manager"), this);
    connect(m_actionRevealInFileManager, &QAction::triggered,
            this, &ProjectBrowserWidget::onRevealInFileManager);
    m_contextMenu->addAction(m_actionRevealInFileManager);

    m_contextMenu->addSeparator();

    // Foreign files section
    m_actionAddToForeign = new QAction(tr("Add to Foreign Files"), this);
    connect(m_actionAddToForeign, &QAction::triggered, this, [this]() {
        QString relPath = selectedRelativePath();
        if (!relPath.isEmpty()) {
            addToForeignFiles(relPath);
            refresh();
            emit foreignFilesChanged();
        }
    });
    m_contextMenu->addAction(m_actionAddToForeign);

    m_actionRemoveFromForeign = new QAction(tr("Remove from Foreign Files"), this);
    connect(m_actionRemoveFromForeign, &QAction::triggered, this, [this]() {
        QString relPath = selectedRelativePath();
        if (!relPath.isEmpty()) {
            removeFromForeignFiles(relPath);
            refresh();
            emit foreignFilesChanged();
        }
    });
    m_contextMenu->addAction(m_actionRemoveFromForeign);

    m_contextMenu->addSeparator();

    // Git ignore section
    m_actionAddToGitIgnore = new QAction(tr("Add to .gitignore"), this);
    connect(m_actionAddToGitIgnore, &QAction::triggered, this, [this]() {
        QString relPath = selectedRelativePath();
        if (!relPath.isEmpty()) {
            addToGitIgnore(relPath);
            refresh();
            emit gitIgnoreChanged();
        }
    });
    m_contextMenu->addAction(m_actionAddToGitIgnore);

    m_actionRemoveFromGitIgnore = new QAction(tr("Remove from .gitignore"), this);
    connect(m_actionRemoveFromGitIgnore, &QAction::triggered, this, [this]() {
        QString relPath = selectedRelativePath();
        if (!relPath.isEmpty()) {
            removeFromGitIgnore(relPath);
            refresh();
            emit gitIgnoreChanged();
        }
    });
    m_contextMenu->addAction(m_actionRemoveFromGitIgnore);

    m_contextMenu->addSeparator();

    // File operations
    m_actionRename = new QAction(tr("Rename"), this);
    m_actionRename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_actionRename, &QAction::triggered, this, &ProjectBrowserWidget::onRename);
    m_contextMenu->addAction(m_actionRename);

    m_actionDelete = new QAction(style->standardIcon(QStyle::SP_TrashIcon),
                                  tr("Delete"), this);
    m_actionDelete->setShortcut(QKeySequence::Delete);
    connect(m_actionDelete, &QAction::triggered, this, &ProjectBrowserWidget::onDelete);
    m_contextMenu->addAction(m_actionDelete);

    m_contextMenu->addSeparator();

    // Properties
    m_actionProperties = new QAction(tr("Properties"), this);
    connect(m_actionProperties, &QAction::triggered,
            this, &ProjectBrowserWidget::onShowProperties);
    m_contextMenu->addAction(m_actionProperties);
}

void ProjectBrowserWidget::setProject(Project* project)
{
    m_project = project;

    if (project && !project->projectPath().isEmpty()) {
        m_projectRoot = project->projectPath();
        m_model->setProjectRoot(m_projectRoot);
        m_treeView->setRootIndex(m_model->index(m_projectRoot));
        loadProjectFiles();
        loadGitIgnore();
        m_treeView->expandToDepth(0);
    } else {
        m_projectRoot.clear();
        m_model->setProjectRoot(QString());
    }

    updateToolbarState();
}

void ProjectBrowserWidget::refresh()
{
    if (m_project) {
        loadProjectFiles();
        loadGitIgnore();
        m_model->refresh();
    }
}

QString ProjectBrowserWidget::selectedFilePath() const
{
    QModelIndex index = m_treeView->currentIndex();
    if (index.isValid()) {
        return m_model->filePath(index);
    }
    return QString();
}

QString ProjectBrowserWidget::selectedRelativePath() const
{
    QString absPath = selectedFilePath();
    if (absPath.isEmpty() || m_projectRoot.isEmpty()) {
        return QString();
    }

    if (absPath.startsWith(m_projectRoot)) {
        QString rel = absPath.mid(m_projectRoot.length());
        if (rel.startsWith(QLatin1Char('/'))) {
            rel = rel.mid(1);
        }
        return rel;
    }
    return QString();
}

void ProjectBrowserWidget::expandAll()
{
    m_treeView->expandAll();
}

void ProjectBrowserWidget::collapseAll()
{
    m_treeView->collapseAll();
}

void ProjectBrowserWidget::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString path = m_model->filePath(index);
    QString relPath = selectedRelativePath();

    if (m_model->isDir(index)) {
        // Toggle expand/collapse for directories
        if (m_treeView->isExpanded(index)) {
            m_treeView->collapse(index);
        } else {
            m_treeView->expand(index);
        }
        return;
    }

    // Check if it's a CAD file
    if (m_model->isCadFile(relPath)) {
        emit openCadFileRequested(relPath);
    } else {
        // Open in system default application
        emit fileDoubleClicked(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ProjectBrowserWidget::onCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    QString relPath = selectedRelativePath();
    bool hasSelection = index.isValid();
    bool isForeign = hasSelection && m_model->isForeignFile(relPath);
    bool isCad = hasSelection && m_model->isCadFile(relPath);
    bool isIgnored = hasSelection && isInGitIgnore(relPath);

    // Update action visibility based on selection
    m_actionOpen->setEnabled(hasSelection);
    m_actionOpenExternal->setEnabled(hasSelection && !m_model->isDir(index));
    m_actionRevealInFileManager->setEnabled(hasSelection);
    m_actionRename->setEnabled(hasSelection && !isCad);  // Can't rename CAD files
    m_actionDelete->setEnabled(hasSelection && !isCad);  // Can't delete CAD files from browser

    // Foreign file actions
    m_actionAddToForeign->setVisible(hasSelection && !isForeign && !isCad);
    m_actionRemoveFromForeign->setVisible(hasSelection && isForeign);

    // Git ignore actions
    m_actionAddToGitIgnore->setVisible(hasSelection && !isIgnored);
    m_actionRemoveFromGitIgnore->setVisible(hasSelection && isIgnored);

    m_actionProperties->setEnabled(hasSelection);

    m_contextMenu->popup(m_treeView->viewport()->mapToGlobal(pos));
}

void ProjectBrowserWidget::onSelectionChanged()
{
    updateToolbarState();
}

void ProjectBrowserWidget::updateToolbarState()
{
    bool hasProject = m_project && !m_projectRoot.isEmpty();
    bool hasSelection = m_treeView->currentIndex().isValid();
    QString relPath = selectedRelativePath();
    bool isForeign = hasSelection && m_model->isForeignFile(relPath);
    bool isCad = hasSelection && m_model->isCadFile(relPath);
    bool isIgnored = hasSelection && isInGitIgnore(relPath);

    m_actionAddFile->setEnabled(hasProject);
    m_actionAddFolder->setEnabled(hasProject);
    m_actionRemove->setEnabled(hasSelection && !isCad);
    m_actionToggleForeign->setEnabled(hasSelection && !isCad);
    m_actionToggleForeign->setChecked(isForeign);
    m_actionToggleGitIgnore->setEnabled(hasSelection);
    m_actionToggleGitIgnore->setChecked(isIgnored);
    m_actionRefresh->setEnabled(hasProject);
}

void ProjectBrowserWidget::loadProjectFiles()
{
    if (!m_project) return;

    QStringList cadFiles;

    // Collect all CAD file paths from the project
    // Geometry files
    for (const auto& shape : m_project->shapes()) {
        Q_UNUSED(shape);
        // Geometry files are in geometry/ subdirectory
    }

    // Get the file lists from the project (these are stored in manifest)
    // For now, we'll build the list from known subdirectories
    QDir projectDir(m_projectRoot);

    // Geometry files
    QDir geomDir(m_projectRoot + QStringLiteral("/geometry"));
    if (geomDir.exists()) {
        for (const QString& file : geomDir.entryList(QDir::Files)) {
            cadFiles.append(QStringLiteral("geometry/") + file);
        }
    }

    // Sketch files
    QDir sketchDir(m_projectRoot + QStringLiteral("/sketches"));
    if (sketchDir.exists()) {
        for (const QString& file : sketchDir.entryList(QDir::Files)) {
            cadFiles.append(QStringLiteral("sketches/") + file);
        }
    }

    // Construction planes
    QDir constDir(m_projectRoot + QStringLiteral("/construction"));
    if (constDir.exists()) {
        for (const QString& file : constDir.entryList(QDir::Files)) {
            cadFiles.append(QStringLiteral("construction/") + file);
        }
    }

    // Features
    QDir featDir(m_projectRoot + QStringLiteral("/features"));
    if (featDir.exists()) {
        for (const QString& file : featDir.entryList(QDir::Files)) {
            cadFiles.append(QStringLiteral("features/") + file);
        }
    }

    // Manifest file
    QString manifestName = projectDir.dirName() + QStringLiteral(".hcad");
    if (QFileInfo::exists(m_projectRoot + QStringLiteral("/") + manifestName)) {
        cadFiles.append(manifestName);
    }

    m_model->setCadFiles(cadFiles);

    // Load foreign files from project
    QVector<ForeignFileEntry> foreignFiles;

    // Get foreign files from project
    const auto& projectForeignFiles = m_project->foreignFiles();
    for (const ForeignFileData& data : projectForeignFiles) {
        foreignFiles.append({data.path, data.description, data.category});
    }

    // Also detect common files that might not be tracked yet
    // These are shown as "suggested" but not added automatically
    auto addIfExists = [&](const QString& path, const QString& cat) {
        if (QFileInfo::exists(m_projectRoot + QStringLiteral("/") + path)) {
            // Only add if not already tracked
            bool found = false;
            for (const ForeignFileEntry& e : foreignFiles) {
                if (e.path == path) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Don't auto-add, but the file will show as untracked
                // User can add it via context menu
            }
        }
    };

    addIfExists(QStringLiteral(".git/"), QStringLiteral("version_control"));
    addIfExists(QStringLiteral(".gitignore"), QStringLiteral("version_control"));
    addIfExists(QStringLiteral("README.md"), QStringLiteral("documentation"));
    addIfExists(QStringLiteral("LICENSE"), QStringLiteral("documentation"));

    m_model->setForeignFiles(foreignFiles);
}

void ProjectBrowserWidget::loadGitIgnore()
{
    m_gitIgnorePatterns = parseGitIgnore();

    QStringList ignoredFiles;
    for (const QString& pattern : m_gitIgnorePatterns) {
        // Simple pattern matching - just exact matches for now
        // TODO: Implement proper gitignore glob matching
        ignoredFiles.append(pattern);
    }

    m_model->setGitIgnoredFiles(ignoredFiles);
}

void ProjectBrowserWidget::saveGitIgnore()
{
    writeGitIgnore(m_gitIgnorePatterns);
}

QStringList ProjectBrowserWidget::parseGitIgnore() const
{
    QStringList patterns;

    QString gitIgnorePath = m_projectRoot + QStringLiteral("/.gitignore");
    QFile file(gitIgnorePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return patterns;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        patterns.append(line);
    }

    return patterns;
}

void ProjectBrowserWidget::writeGitIgnore(const QStringList& patterns)
{
    QString gitIgnorePath = m_projectRoot + QStringLiteral("/.gitignore");
    QFile file(gitIgnorePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "# HobbyCAD project gitignore\n";
    out << "# Auto-generated entries below\n\n";

    for (const QString& pattern : patterns) {
        out << pattern << "\n";
    }
}

bool ProjectBrowserWidget::addToForeignFiles(const QString& relativePath,
                                              const QString& category)
{
    if (!m_project) return false;

    // Check if already in foreign files
    if (m_project->isForeignFile(relativePath)) {
        return false;
    }

    // Add to project's foreign_files list
    m_project->addForeignFile(relativePath, category);
    m_project->setModified(true);

    // Reload foreign files into model
    loadProjectFiles();

    return true;
}

bool ProjectBrowserWidget::removeFromForeignFiles(const QString& relativePath)
{
    if (!m_project) return false;

    // Remove from project's foreign_files list
    m_project->removeForeignFile(relativePath);
    m_project->setModified(true);

    // Reload foreign files into model
    loadProjectFiles();

    return true;
}

bool ProjectBrowserWidget::addToGitIgnore(const QString& relativePath)
{
    if (!m_gitIgnorePatterns.contains(relativePath)) {
        m_gitIgnorePatterns.append(relativePath);
        saveGitIgnore();
        loadGitIgnore();
        return true;
    }
    return false;
}

bool ProjectBrowserWidget::removeFromGitIgnore(const QString& relativePath)
{
    if (m_gitIgnorePatterns.removeAll(relativePath) > 0) {
        saveGitIgnore();
        loadGitIgnore();
        return true;
    }
    return false;
}

bool ProjectBrowserWidget::isInGitIgnore(const QString& relativePath) const
{
    return m_gitIgnorePatterns.contains(relativePath);
}

QString ProjectBrowserWidget::absolutePath(const QString& relativePath) const
{
    if (m_projectRoot.isEmpty() || relativePath.isEmpty()) {
        return QString();
    }
    return m_projectRoot + QStringLiteral("/") + relativePath;
}

// Toolbar action slots

void ProjectBrowserWidget::onAddFile()
{
    if (m_projectRoot.isEmpty()) return;

    QString targetDir = m_projectRoot;

    // If a directory is selected, add to that directory
    QModelIndex index = m_treeView->currentIndex();
    if (index.isValid() && m_model->isDir(index)) {
        targetDir = m_model->filePath(index);
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Add Files to Project"),
        QDir::homePath(),
        tr("All Files (*)"));

    if (files.isEmpty()) return;

    for (const QString& srcPath : files) {
        QFileInfo srcInfo(srcPath);
        QString destPath = targetDir + QStringLiteral("/") + srcInfo.fileName();

        if (QFile::exists(destPath)) {
            int result = QMessageBox::question(
                this,
                tr("File Exists"),
                tr("'%1' already exists. Overwrite?").arg(srcInfo.fileName()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

            if (result == QMessageBox::Cancel) return;
            if (result == QMessageBox::No) continue;

            QFile::remove(destPath);
        }

        if (!QFile::copy(srcPath, destPath)) {
            QMessageBox::warning(this, tr("Error"),
                tr("Failed to copy '%1'").arg(srcInfo.fileName()));
        }
    }

    refresh();
    emit filesDropped(files);
}

void ProjectBrowserWidget::onAddFolder()
{
    if (m_projectRoot.isEmpty()) return;

    QString targetDir = m_projectRoot;

    QModelIndex index = m_treeView->currentIndex();
    if (index.isValid() && m_model->isDir(index)) {
        targetDir = m_model->filePath(index);
    }

    bool ok;
    QString folderName = QInputDialog::getText(
        this,
        tr("New Folder"),
        tr("Folder name:"),
        QLineEdit::Normal,
        tr("New Folder"),
        &ok);

    if (!ok || folderName.isEmpty()) return;

    QDir dir(targetDir);
    if (!dir.mkdir(folderName)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Failed to create folder '%1'").arg(folderName));
    }

    refresh();
}

void ProjectBrowserWidget::onRemoveSelected()
{
    onDelete();
}

void ProjectBrowserWidget::onToggleForeignFile()
{
    QString relPath = selectedRelativePath();
    if (relPath.isEmpty()) return;

    if (m_model->isForeignFile(relPath)) {
        removeFromForeignFiles(relPath);
    } else {
        addToForeignFiles(relPath);
    }

    refresh();
    emit foreignFilesChanged();
}

void ProjectBrowserWidget::onToggleGitIgnore()
{
    QString relPath = selectedRelativePath();
    if (relPath.isEmpty()) return;

    if (isInGitIgnore(relPath)) {
        removeFromGitIgnore(relPath);
    } else {
        addToGitIgnore(relPath);
    }

    refresh();
    emit gitIgnoreChanged();
}

void ProjectBrowserWidget::onRefresh()
{
    refresh();
}

void ProjectBrowserWidget::onOpenInExternalEditor()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ProjectBrowserWidget::onRename()
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return;

    m_treeView->edit(index);
}

void ProjectBrowserWidget::onDelete()
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return;

    QString path = m_model->filePath(index);
    QString relPath = selectedRelativePath();
    bool isDir = m_model->isDir(index);

    // Don't allow deleting CAD files from the browser
    if (m_model->isCadFile(relPath)) {
        QMessageBox::warning(this, tr("Cannot Delete"),
            tr("CAD files cannot be deleted from the project browser.\n"
               "Use the feature tree to remove sketches and geometry."));
        return;
    }

    QString message = isDir
        ? tr("Delete folder '%1' and all its contents?").arg(relPath)
        : tr("Delete file '%1'?").arg(relPath);

    int result = QMessageBox::question(this, tr("Confirm Delete"),
        message, QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    bool success;
    if (isDir) {
        QDir dir(path);
        success = dir.removeRecursively();
    } else {
        success = QFile::remove(path);
    }

    if (!success) {
        QMessageBox::warning(this, tr("Error"),
            tr("Failed to delete '%1'").arg(relPath));
    }

    refresh();
}

void ProjectBrowserWidget::onShowProperties()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    QString relPath = selectedRelativePath();
    ProjectFileStatus status = m_model->fileStatus(relPath);

    QString statusStr;
    switch (status) {
    case ProjectFileStatus::CadFile:
        statusStr = tr("CAD File (in manifest)");
        break;
    case ProjectFileStatus::ForeignFile:
        statusStr = tr("Foreign File (tracked separately)");
        break;
    case ProjectFileStatus::GitIgnored:
        statusStr = tr("Git Ignored");
        break;
    case ProjectFileStatus::Untracked:
        statusStr = tr("Untracked (not in manifest)");
        break;
    }

    QString message = tr(
        "Name: %1\n"
        "Path: %2\n"
        "Size: %3\n"
        "Modified: %4\n"
        "Status: %5")
        .arg(info.fileName())
        .arg(relPath)
        .arg(info.isDir() ? tr("(directory)") : QString::number(info.size()) + tr(" bytes"))
        .arg(info.lastModified().toString(Qt::TextDate))
        .arg(statusStr);

    QMessageBox::information(this, tr("Properties"), message);
}

void ProjectBrowserWidget::onRevealInFileManager()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    QString dirPath = info.isDir() ? path : info.absolutePath();

#ifdef Q_OS_LINUX
    QProcess::startDetached(QStringLiteral("xdg-open"), {dirPath});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer"), {QDir::toNativeSeparators(dirPath)});
#elif defined(Q_OS_MAC)
    QProcess::startDetached(QStringLiteral("open"), {dirPath});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
#endif
}

}  // namespace hobbycad
