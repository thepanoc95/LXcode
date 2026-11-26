#include "gui/mainwindow.hpp"
#include "gui/settingsdialog.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/strings.hpp"
#include "lxcode/diagnostics/issues.hpp"
#include "lxcode/project/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <thread>

namespace lxcode::gui {

namespace {

QString severityText(Issue::Severity severity) {
    switch (severity) {
    case Issue::Severity::Error: return QObject::tr("error");
    case Issue::Severity::Warning: return QObject::tr("warning");
    case Issue::Severity::Info: return QObject::tr("note");
    }
    return QObject::tr("note");
}

} // namespace

MainWindow::MainWindow(QString projectArgument, std::string resourcesDirectory, QWidget* parent)
    : QMainWindow(parent), resourcesDirectory_(std::move(resourcesDirectory)) {
    settings_.load();
    theme_ = Theme::load(settings_.string("theme", "dark"), resourcesDirectory_);
    toolchains_.detect();
    const auto configured = settings_.string("toolchain");
    toolchains_.setSelected(configured);
    if (!toolchains_.selected()) {
        for (const auto& toolchain : toolchains_.toolchains()) {
            if (toolchain.kind == ToolchainKind::OSXCross && toolchain.valid()) {
                toolchains_.setSelected(toolchain.name);
                break;
            }
        }
        if (!toolchains_.selected()) toolchains_.setSelected("System compiler");
    }

    buildUi();
    applyTheme();

    connect(this, &MainWindow::buildMessage, this, &MainWindow::appendBuildMessage,
            Qt::QueuedConnection);
    connect(this, &MainWindow::buildFinished, this, &MainWindow::finishBuild,
            Qt::QueuedConnection);
    connect(this, &MainWindow::gitStatusReady, this, &MainWindow::onGitStatusReady,
            Qt::QueuedConnection);
    connect(this, &MainWindow::gitBranchesReady, this, &MainWindow::onGitBranchesReady,
            Qt::QueuedConnection);
    connect(this, &MainWindow::completionReady, this, &MainWindow::onCompletionReady,
            Qt::QueuedConnection);
    connect(this, &MainWindow::definitionReady, this, &MainWindow::onDefinitionReady,
            Qt::QueuedConnection);
    connect(this, &MainWindow::debugOutput, this, &MainWindow::onDebugOutput,
            Qt::QueuedConnection);

    if (!projectArgument.isEmpty()) openPath(projectArgument);
    else {
        statusLabel_->setText(tr("Welcome to LXcode — open a .xcodeproj or .xcworkspace"));
        refreshDiagnostics();
        refreshGit();
    }
}

MainWindow::~MainWindow() {
    for (auto& [language, client] : languageServers_) {
        if (client) client->stop();
    }
    if (debugClient_) debugClient_->stop();
    if (buildController_) buildController_->cancel();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("LXcode"));
    resize(1440, 900);
    setMinimumSize(960, 620);
    setIconSize(QSize(18, 18));

    buildToolbar();

    editorTabs_ = new QTabWidget(this);
    editorTabs_->setTabsClosable(true);
    editorTabs_->setDocumentMode(true);
    editorTabs_->setMovable(true);
    setCentralWidget(editorTabs_);
    connect(editorTabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(editorTabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    buildNavigator();
    buildInspector();
    buildConsole();

    statusLabel_ = new QLabel(this);
    branchLabel_ = new QLabel(this);
    cursorLabel_ = new QLabel(this);
    issueLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(branchLabel_);
    statusBar()->addPermanentWidget(issueLabel_);
    statusBar()->addPermanentWidget(cursorLabel_);
    cursorLabel_->setText(tr("Ln 1, Col 1"));

    auto* fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(tr("New Project…"), QKeySequence::New, this, &MainWindow::createProjectDialog);
    fileMenu->addAction(tr("Open Project…"), QKeySequence::Open, this, &MainWindow::openProjectDialog);
    fileMenu->addAction(tr("Open Recent"), this, &MainWindow::openRecent);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Save"), QKeySequence::Save, this, &MainWindow::saveDocument);
    fileMenu->addAction(tr("Save All"), QKeySequence(tr("Ctrl+Shift+S")), this,
                        &MainWindow::saveAllDocuments);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Settings…"), this, &MainWindow::openSettings);

    auto* projectMenu = menuBar()->addMenu(tr("Project"));
    projectMenu->addAction(tr("Add Existing Files…"), this, &MainWindow::addExistingFiles);
    projectMenu->addAction(tr("New Group…"), this, &MainWindow::addGroup);
    projectMenu->addAction(tr("Generate CMake Toolchain File…"), this,
                           &MainWindow::generateToolchainFile);

    auto* buildMenu = menuBar()->addMenu(tr("Build"));
    buildMenu->addAction(tr("Build"), QKeySequence(tr("Ctrl+B")), this, &MainWindow::startBuild);
    buildMenu->addAction(tr("Clean"), this, &MainWindow::startClean);
    buildMenu->addAction(tr("Cancel Build"), this, &MainWindow::cancelBuild);

    auto* sourceMenu = menuBar()->addMenu(tr("Source Control"));
    sourceMenu->addAction(tr("Refresh Status"), this, &MainWindow::refreshGit);
    sourceMenu->addAction(tr("Commit…"), this, &MainWindow::commitChanges);
    sourceMenu->addAction(tr("Switch Branch…"), this, &MainWindow::switchBranch);

    auto* debugMenu = menuBar()->addMenu(tr("Debug"));
    debugMenu->addAction(tr("Start Debug Session…"), QKeySequence(tr("Ctrl+Shift+D")), this,
                         &MainWindow::startDebugSession);

    auto* viewMenu = menuBar()->addMenu(tr("View"));
    viewMenu->addAction(navigatorDock_->toggleViewAction());
    viewMenu->addAction(inspectorDock_->toggleViewAction());
    viewMenu->addAction(consoleDock_->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(toolbar_->toggleViewAction());

    auto* helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(tr("About LXcode"), this, &MainWindow::showAbout);
}

void MainWindow::buildToolbar() {
    toolbar_ = addToolBar(tr("Main"));
    toolbar_->setObjectName("MainToolbar");
    toolbar_->setMovable(false);

    toolbar_->addAction(QIcon(), tr("Open"), this, &MainWindow::openProjectDialog);
    toolbar_->addAction(QIcon(), tr("Save"), this, &MainWindow::saveDocument);
    toolbar_->addSeparator();
    toolbar_->addAction(QIcon(), tr("Build"), this, &MainWindow::startBuild);
    toolbar_->addAction(QIcon(), tr("Clean"), this, &MainWindow::startClean);
    toolbar_->addSeparator();

    targetCombo_ = new QComboBox(toolbar_);
    targetCombo_->setMinimumWidth(130);
    targetCombo_->setToolTip(tr("Build target"));
    configurationCombo_ = new QComboBox(toolbar_);
    configurationCombo_->addItems({"Debug", "Release"});
    architectureCombo_ = new QComboBox(toolbar_);
    architectureCombo_->addItems({"arm64", "x86_64"});
    toolchainCombo_ = new QComboBox(toolbar_);
    toolchainCombo_->setMinimumWidth(180);
    toolbar_->addWidget(new QLabel(tr("Target"), toolbar_));
    toolbar_->addWidget(targetCombo_);
    toolbar_->addWidget(new QLabel(tr("Config"), toolbar_));
    toolbar_->addWidget(configurationCombo_);
    toolbar_->addWidget(new QLabel(tr("Arch"), toolbar_));
    toolbar_->addWidget(architectureCombo_);
    toolbar_->addWidget(new QLabel(tr("Toolchain"), toolbar_));
    toolbar_->addWidget(toolchainCombo_);
    updateToolchainCombo();
}

void MainWindow::buildNavigator() {
    navigatorDock_ = new QDockWidget(tr("Navigator"), this);
    navigatorDock_->setObjectName("NavigatorDock");
    auto* container = new QWidget(navigatorDock_);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    navigatorFilter_ = new QLineEdit(container);
    navigatorFilter_->setPlaceholderText(tr("Filter files"));
    navigatorTree_ = new QTreeWidget(container);
    navigatorTree_->setHeaderLabels({tr("Name"), tr("Kind")});
    navigatorTree_->setColumnWidth(0, 340);
    navigatorTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    navigatorTree_->setUniformRowHeights(true);
    layout->addWidget(navigatorFilter_);
    layout->addWidget(navigatorTree_);
    navigatorDock_->setWidget(container);
    addDockWidget(Qt::LeftDockWidgetArea, navigatorDock_);
    connect(navigatorFilter_, &QLineEdit::textChanged, this, &MainWindow::filterNavigator);
    connect(navigatorTree_, &QTreeWidget::itemActivated, this, &MainWindow::onNavigatorActivated);
    connect(navigatorTree_, &QTreeWidget::currentItemChanged, this,
            &MainWindow::onNavigatorCurrentChanged);
    connect(navigatorTree_, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::onNavigatorContextMenu);
}

void MainWindow::buildInspector() {
    inspectorDock_ = new QDockWidget(tr("Inspector"), this);
    inspectorDock_->setObjectName("InspectorDock");
    inspectorTree_ = new QTreeWidget(inspectorDock_);
    inspectorTree_->setHeaderLabels({tr("Property"), tr("Value")});
    inspectorTree_->setColumnWidth(0, 160);
    inspectorDock_->setWidget(inspectorTree_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);
}

void MainWindow::buildConsole() {
    consoleDock_ = new QDockWidget(tr("Console"), this);
    consoleDock_->setObjectName("ConsoleDock");
    consoleTabs_ = new QTabWidget(consoleDock_);
    buildConsole_ = new QPlainTextEdit(consoleTabs_);
    issuesConsole_ = new QPlainTextEdit(consoleTabs_);
    debugConsole_ = new QPlainTextEdit(consoleTabs_);
    gitConsole_ = new QPlainTextEdit(consoleTabs_);
    for (auto* console : {buildConsole_, issuesConsole_, debugConsole_, gitConsole_}) {
        console->setReadOnly(true);
        console->setLineWrapMode(QPlainTextEdit::NoWrap);
        console->setMaximumBlockCount(5000);
    }
    consoleTabs_->addTab(buildConsole_, tr("Build"));
    consoleTabs_->addTab(issuesConsole_, tr("Issues"));
    consoleTabs_->addTab(debugConsole_, tr("Debug"));
    consoleTabs_->addTab(gitConsole_, tr("Source Control"));
    consoleDock_->setWidget(consoleTabs_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock_);
    resizeDocks({navigatorDock_, consoleDock_}, {300, 210}, Qt::Horizontal);
}

void MainWindow::applyTheme() {
    theme_ = Theme::load(settings_.string("theme", "dark"), resourcesDirectory_);
    qApp->setPalette(QPalette());
    theme_.apply(*qApp);
    const int fontSize = settings_.integer("editorFontSize", 12);
    const int tabSize = settings_.integer("editorTabSize", 4);
    const bool spaces = settings_.boolean("editorInsertSpaces", true);
    for (int i = 0; i < editorTabs_->count(); ++i) {
        if (auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(i)))
            editor->applyTheme(theme_, fontSize, tabSize, spaces);
    }
}

void MainWindow::updateWindowTitle() {
    const QString name = project_ ? QString::fromStdString(project_->name()) : tr("No Project");
    const bool modified = currentEditor() && currentEditor()->isModified();
    setWindowTitle(QString("%1%2 — LXcode").arg(name, modified ? " •" : ""));
}

void MainWindow::updateTargetCombo() {
    targetCombo_->clear();
    if (!project_) return;
    for (const auto& target : project_->targets()) {
        targetCombo_->addItem(QString::fromStdString(target.name),
                              QString::fromStdString(target.id));
    }
}

void MainWindow::updateToolchainCombo() {
    const auto previous = toolchainCombo_->currentText();
    toolchainCombo_->clear();
    for (const auto& toolchain : toolchains_.toolchains())
        toolchainCombo_->addItem(QString::fromStdString(toolchain.name));
    const int index = toolchainCombo_->findText(previous.isEmpty()
                                                    ? QString::fromStdString(toolchains_.selectedName())
                                                    : previous);
    toolchainCombo_->setCurrentIndex(std::max(0, index));
}

std::filesystem::path MainWindow::resolveProjectPath(const std::filesystem::path& path) const {
    std::error_code ec;
    if (!fs::exists(path)) return {};
    if (std::filesystem::is_directory(path, ec)) {
        if (path.extension() == ".xcworkspace") {
            auto workspace = XcWorkspace::open(path);
            if (workspace && !workspace->projectPaths().empty())
                return workspace->projectPaths().front() / "project.pbxproj";
        }
        if (path.extension() == ".xcodeproj") return path / "project.pbxproj";
        for (const auto& child : {"project.pbxproj", "contents.xcworkspacedata"}) {
            const auto candidate = path / child;
            if (fs::exists(candidate)) return resolveProjectPath(candidate);
        }
    }
    if (path.filename() == "contents.xcworkspacedata") {
        auto workspace = XcWorkspace::open(path.parent_path());
        if (workspace && !workspace->projectPaths().empty())
            return workspace->projectPaths().front() / "project.pbxproj";
    }
    if (path.filename() == "project.pbxproj") return path;

    auto current = path.is_absolute() ? path : std::filesystem::absolute(path);
    for (std::size_t depth = 0; depth < 8 && !current.empty() && current.has_parent_path(); ++depth) {
        current = current.parent_path();
        if (!std::filesystem::is_directory(current, ec)) break;
        for (std::filesystem::directory_iterator it(current, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            const auto ext = it->path().extension();
            if (ext == ".xcodeproj" || ext == ".xcworkspace")
                return resolveProjectPath(it->path());
        }
    }
    return {};
}

void MainWindow::openProjectDialog() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open Xcode Project"), QString(),
        tr("Xcode Projects (*.xcodeproj/*.project.pbxproj *.xcworkspace *.pbxproj);;All Files (*)"));
    if (!path.isEmpty()) openPath(path);
}

void MainWindow::openRecent() {
    const auto recent = settings_.recentProjects();
    if (recent.empty()) {
        QMessageBox::information(this, tr("Open Recent"), tr("No recent projects."));
        return;
    }
    QStringList items;
    for (const auto& path : recent) items.push_back(QString::fromStdString(path.string()));
    bool ok = false;
    const auto choice = QInputDialog::getItem(this, tr("Open Recent"), tr("Projects"),
                                              items, 0, false, &ok);
    if (ok && !choice.isEmpty()) openPath(choice);
}

void MainWindow::createProjectDialog() {
    const auto location = QFileDialog::getExistingDirectory(this, tr("Create Project In"));
    if (location.isEmpty()) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("New Project"), tr("Product name:"),
                                            QLineEdit::Normal, "Hello", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    auto project = XcodeProject::create(std::filesystem::path(location.toStdString()) /
                                            (name.trimmed().toStdString() + ".xcodeproj"),
                                        name.trimmed().toStdString());
    if (!project) {
        QMessageBox::critical(this, tr("Create Project"), QString::fromStdString(project.error()));
        return;
    }
    openPath(QString::fromStdString(project->path().string()));
}

void MainWindow::openPath(const QString& path) {
    const auto resolved = resolveProjectPath(path.toStdString());
    if (resolved.empty()) {
        QMessageBox::warning(this, tr("Open Project"),
                             tr("Cannot find an Xcode project at %1.").arg(path));
        return;
    }
    loadProject(resolved);
}

void MainWindow::loadProject(const std::filesystem::path& path) {
    if (!promptSaveModified()) return;
    auto project = XcodeProject::open(path);
    if (!project) {
        QMessageBox::critical(this, tr("Open Project"), QString::fromStdString(project.error()));
        return;
    }
    while (editorTabs_->count() > 0) {
        auto* widget = editorTabs_->widget(0);
        editorTabs_->removeTab(0);
        delete widget;
    }
    project_ = std::make_unique<XcodeProject>(std::move(*project));
    projectPath_ = path;
    settings_.addRecentProject(path.parent_path().parent_path());
    settings_.save();
    populateNavigator();
    updateTargetCombo();
    updateWindowTitle();
    statusLabel_->setText(tr("Opened %1").arg(QString::fromStdString(project_->name())));
    refreshDiagnostics();
    refreshGit();
}

void MainWindow::populateNavigator() {
    navigatorTree_->clear();
    if (!project_) return;
    auto* root = new QTreeWidgetItem(navigatorTree_);
    root->setText(0, QString::fromStdString(project_->name()));
    root->setText(1, tr("Project"));
    root->setData(0, Qt::UserRole, "project");
    root->setData(0, Qt::UserRole + 2, QString::fromStdString(project_->path().string()));
    root->setExpanded(true);
    addNavigatorItems(root, project_->navigator());
    filterNavigator(navigatorFilter_->text());
}

void MainWindow::addNavigatorItems(QTreeWidgetItem* parent,
                                   const std::vector<NavigatorItem>& items) {
    for (const auto& item : items) {
        auto* child = new QTreeWidgetItem(parent);
        child->setText(0, QString::fromStdString(item.name));
        child->setText(1, QString::fromStdString(item.group ? "Group" : item.kind));
        child->setData(0, Qt::UserRole, item.group ? "group" : "file");
        child->setData(0, Qt::UserRole + 1, QString::fromStdString(item.fileRefId));
        child->setData(0, Qt::UserRole + 2, QString::fromStdString(item.path));
        if (item.group) child->setExpanded(true);
        addNavigatorItems(child, item.children);
    }
}

void MainWindow::filterNavigator(const QString& text) {
    const auto needle = text.trimmed().toLower();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) {
        bool visible = item->text(0).toLower().contains(needle) || needle.isEmpty();
        for (int i = 0; i < item->childCount(); ++i) visible |= apply(item->child(i));
        item->setHidden(!visible);
        return visible;
    };
    for (int i = 0; i < navigatorTree_->topLevelItemCount(); ++i)
        apply(navigatorTree_->topLevelItem(i));
}

void MainWindow::onNavigatorActivated(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item || item->data(0, Qt::UserRole).toString() != "file") return;
    openFile(std::filesystem::path(item->data(0, Qt::UserRole + 2).toString().toStdString()));
}

void MainWindow::onNavigatorCurrentChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous) {
    Q_UNUSED(previous);
    updateInspector(current);
}

void MainWindow::onNavigatorContextMenu(const QPoint& point) {
    auto* item = navigatorTree_->itemAt(point);
    QMenu menu(this);
    auto* addFiles = menu.addAction(tr("Add Existing Files…"));
    auto* addGroupAction = menu.addAction(tr("New Group…"));
    auto* rename = menu.addAction(tr("Rename…"));
    auto* remove = menu.addAction(tr("Remove from Project"));
    rename->setEnabled(item && item->data(0, Qt::UserRole).toString() == "file");
    remove->setEnabled(item && item->data(0, Qt::UserRole).toString() == "file");
    auto* chosen = menu.exec(navigatorTree_->viewport()->mapToGlobal(point));
    if (chosen == addFiles) addExistingFiles();
    else if (chosen == addGroupAction) addGroup();
    else if (chosen == rename) renameNavigatorItem();
    else if (chosen == remove) removeNavigatorItem();
}

void MainWindow::addExistingFiles() {
    if (!project_) return;
    const auto files = QFileDialog::getOpenFileNames(this, tr("Add Files to Project"));
    if (files.isEmpty()) return;
    auto* item = navigatorTree_->currentItem();
    const auto groupId = item && item->data(0, Qt::UserRole).toString() == "group"
                             ? item->data(0, Qt::UserRole + 1).toString().toStdString()
                             : std::string();
    for (const auto& file : files) {
        auto added = project_->addFile(file.toStdString(), groupId, currentTargetId());
        if (!added) QMessageBox::warning(this, tr("Add Files"), QString::fromStdString(added.error()));
    }
    if (project_->save()) populateNavigator();
    refreshDiagnostics();
}

void MainWindow::addGroup() {
    if (!project_) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("New Group"), tr("Group name:"),
                                            QLineEdit::Normal, "Sources", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    auto* item = navigatorTree_->currentItem();
    const auto parent = item && item->data(0, Qt::UserRole).toString() == "group"
                            ? item->data(0, Qt::UserRole + 1).toString().toStdString()
                            : std::string();
    auto result = project_->addGroup(name.trimmed().toStdString(), parent);
    if (!result) QMessageBox::warning(this, tr("New Group"), QString::fromStdString(result.error()));
    else if (project_->save()) populateNavigator();
}

void MainWindow::renameNavigatorItem() {
    auto* item = navigatorTree_->currentItem();
    if (!project_ || !item || item->data(0, Qt::UserRole).toString() != "file") return;
    const auto id = item->data(0, Qt::UserRole + 1).toString().toStdString();
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                            QLineEdit::Normal, item->text(0), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    auto result = project_->renameFile(id, name.trimmed().toStdString(), true);
    if (!result) QMessageBox::warning(this, tr("Rename"), QString::fromStdString(result.error()));
    else if (project_->save()) populateNavigator();
}

void MainWindow::removeNavigatorItem() {
    auto* item = navigatorTree_->currentItem();
    if (!project_ || !item || item->data(0, Qt::UserRole).toString() != "file") return;
    const auto answer = QMessageBox::question(
        this, tr("Remove File"), tr("Remove %1 from the project? The file on disk is kept.")
            .arg(item->text(0)));
    if (answer != QMessageBox::Yes) return;
    auto result = project_->removeFile(item->data(0, Qt::UserRole + 1).toString().toStdString());
    if (!result) QMessageBox::warning(this, tr("Remove File"), QString::fromStdString(result.error()));
    else if (project_->save()) populateNavigator();
}

void MainWindow::openFileFromArgument(const QString& path) {
    if (!path.isEmpty()) openFile(std::filesystem::path(path.toStdString()));
}

void MainWindow::openFile(const std::filesystem::path& path, int line, int column) {
    if (path.empty() || !fs::exists(path)) return;
    const auto key = QString::fromStdString(path.lexically_normal().string());
    for (int i = 0; i < editorTabs_->count(); ++i) {
        auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(i));
        if (editor && QString::fromStdString(editor->filePath().string()) == key) {
            editorTabs_->setCurrentIndex(i);
            if (line > 0) editor->revealLine(line, column);
            return;
        }
    }
    auto* editor = new CodeEditor(editorTabs_);
    editor->setFilePath(path);
    if (!editor->loadFile()) {
        delete editor;
        QMessageBox::warning(this, tr("Open File"), tr("Cannot read %1").arg(key));
        return;
    }
    editor->applyTheme(theme_, settings_.integer("editorFontSize", 12),
                       settings_.integer("editorTabSize", 4),
                       settings_.boolean("editorInsertSpaces", true));
    const int index = editorTabs_->addTab(editor, QString::fromStdString(path.filename().string()));
    editorTabs_->setTabToolTip(index, key);
    editorTabs_->setCurrentIndex(index);
    connect(editor, &CodeEditor::modificationChanged, this, &MainWindow::onDocumentModified);
    connect(editor, &CodeEditor::cursorPositionChanged, this, &MainWindow::onCursorPosition);
    connect(editor, &CodeEditor::completionRequested, this, &MainWindow::requestCompletion);
    connect(editor, &CodeEditor::definitionRequested, this, &MainWindow::requestDefinition);
    if (auto client = languageClient(languageIdForPath(path)))
        client->didOpen(path, editor->toPlainText().toStdString());
    if (line > 0) editor->revealLine(line, column);
    updateWindowTitle();
}

CodeEditor* MainWindow::currentEditor() const {
    return qobject_cast<CodeEditor*>(editorTabs_->currentWidget());
}

CodeEditor* MainWindow::editorForPath(const std::filesystem::path& path) const {
    const auto key = QString::fromStdString(path.lexically_normal().string());
    for (int i = 0; i < editorTabs_->count(); ++i) {
        auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(i));
        if (editor && QString::fromStdString(editor->filePath().string()) == key) return editor;
    }
    return nullptr;
}

void MainWindow::updateInspector(QTreeWidgetItem* item) {
    inspectorTree_->clear();
    auto addRow = [this](const QString& key, const QString& value) {
        auto* row = new QTreeWidgetItem(inspectorTree_);
        row->setText(0, key);
        row->setText(1, value);
    };
    if (!project_) return;
    if (item && item->data(0, Qt::UserRole).toString() == "file") {
        const auto path = item->data(0, Qt::UserRole + 2).toString().toStdString();
        addRow(tr("Name"), QString::fromStdString(std::filesystem::path(path).filename().string()));
        addRow(tr("Path"), QString::fromStdString(path));
        addRow(tr("File type"), QString::fromStdString(project_->fileType(path)));
        addRow(tr("Reference ID"), item->data(0, Qt::UserRole + 1).toString());
    } else {
        addRow(tr("Project"), QString::fromStdString(project_->name()));
        addRow(tr("File"), QString::fromStdString(project_->path().string()));
        for (const auto& target : project_->targets()) {
            addRow(tr("Target"), QString::fromStdString(target.name));
            addRow(tr("Product type"), QString::fromStdString(target.productType));
            const auto settings = project_->buildSettings(target.id,
                                                          configurationCombo_->currentText().toStdString());
            for (const auto& key : {"PRODUCT_BUNDLE_IDENTIFIER", "SDKROOT",
                                    "MACOSX_DEPLOYMENT_TARGET", "SWIFT_VERSION"}) {
                const auto it = settings.find(key);
                if (it != settings.end()) addRow(QString::fromStdString(key),
                                                 QString::fromStdString(it->second));
            }
        }
    }
    inspectorTree_->expandAll();
}

void MainWindow::onTabCloseRequested(int index) {
    auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(index));
    if (!editor) return;
    if (editor->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Unsaved Changes"),
            tr("Save changes to %1 before closing?").arg(editorTabs_->tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save && !editor->saveFile()) return;
    }
    if (auto client = languageClient(languageIdForPath(editor->filePath())))
        client->didClose(editor->filePath());
    editorTabs_->removeTab(index);
    delete editor;
    updateWindowTitle();
}

void MainWindow::onTabChanged(int index) {
    Q_UNUSED(index);
    updateWindowTitle();
    if (auto* editor = currentEditor()) {
        auto cursor = editor->textCursor();
        onCursorPosition(cursor.blockNumber() + 1, cursor.columnNumber());
        startLanguageServer(languageIdForPath(editor->filePath()));
    }
}

void MainWindow::onDocumentModified(bool modified) {
    Q_UNUSED(modified);
    updateWindowTitle();
}

void MainWindow::onCursorPosition(int line, int column) {
    cursorLabel_->setText(tr("Ln %1, Col %2").arg(line).arg(column));
}

void MainWindow::startLanguageServer(const std::string& languageId) {
    if (languageId.empty() || languageId == "plaintext") return;
    if (languageServers_.count(languageId)) return;
    const auto command = settings_.languageServerCommand(languageId);
    if (command.empty()) return;
    auto client = std::make_shared<LspClient>(languageId, command,
                                              project_ ? project_->directory().parent_path()
                                                       : std::filesystem::path());
    client->setDiagnosticHandler([this](const std::vector<LspDiagnostic>& diagnostics) {
        QStringList entries;
        for (const auto& diagnostic : diagnostics) {
            entries.push_back(QString("%1:%2:%3: %4")
                                  .arg(QString::fromStdString(diagnostic.file))
                                  .arg(diagnostic.line)
                                  .arg(diagnostic.column)
                                  .arg(QString::fromStdString(diagnostic.message)));
            if (auto* editor = editorForPath(diagnostic.file))
                editor->setDiagnostics(diagnostics);
        }
        QMetaObject::invokeMethod(this, [this, entries] {
            diagnosticEntries_ = entries;
            refreshDiagnostics();
        }, Qt::QueuedConnection);
    });
    if (client->start()) {
        languageServers_[languageId] = client;
        statusLabel_->setText(tr("Started %1 language server").arg(
            QString::fromStdString(languageId)));
    } else {
        statusLabel_->setText(tr("Could not start language server: %1")
                                  .arg(QString::fromStdString(command)));
    }
}

std::shared_ptr<LspClient> MainWindow::languageClient(const std::string& languageId) {
    startLanguageServer(languageId);
    auto it = languageServers_.find(languageId);
    return it == languageServers_.end() ? nullptr : it->second;
}

void MainWindow::requestCompletion(int line, int column) {
    auto* editor = currentEditor();
    if (!editor) return;
    auto client = languageClient(languageIdForPath(editor->filePath()));
    if (!client) return;
    const auto path = editor->filePath();
    std::thread([this, client, path, line, column] {
        auto result = client->completion(path, line, column);
        QStringList items;
        if (result) {
            for (const auto& item : *result) {
                items.push_back(item.insertText.empty()
                                    ? QString::fromStdString(item.label)
                                    : QString::fromStdString(item.insertText));
            }
        }
        emit completionReady(items);
    }).detach();
}

void MainWindow::requestDefinition(int line, int column) {
    auto* editor = currentEditor();
    if (!editor) return;
    auto client = languageClient(languageIdForPath(editor->filePath()));
    if (!client) return;
    const auto path = editor->filePath();
    std::thread([this, client, path, line, column] {
        auto result = client->definition(path, line, column);
        if (result && !result->empty()) {
            const auto& location = result->front();
            emit definitionReady(QString::fromStdString(location.file), location.line,
                                 location.column);
        } else {
            emit definitionReady(QString(), 0, 0);
        }
    }).detach();
}

void MainWindow::onCompletionReady(QStringList items) {
    if (auto* editor = currentEditor()) editor->setCompletionItems(
        std::vector<CompletionItem>{});
    if (!items.isEmpty() && currentEditor()) {
        std::vector<CompletionItem> completions;
        for (const auto& item : items) {
            CompletionItem completion;
            completion.label = item.toStdString();
            completion.insertText = item.toStdString();
            completions.push_back(std::move(completion));
        }
        currentEditor()->setCompletionItems(completions);
    }
}

void MainWindow::onDefinitionReady(QString path, int line, int column) {
    if (path.isEmpty()) {
        statusLabel_->setText(tr("No definition found"));
        return;
    }
    openFile(std::filesystem::path(path.toStdString()), line, column);
}

std::string MainWindow::currentTargetId() const {
    return targetCombo_->currentData().toString().toStdString();
}

Toolchain MainWindow::currentToolchain() const {
    const auto name = toolchainCombo_->currentText().toStdString();
    if (auto* toolchain = toolchains_.find(name)) return *toolchain;
    return Toolchain{};
}

void MainWindow::startBuild() { buildProject(false); }

void MainWindow::startClean() { buildProject(true); }

void MainWindow::cancelBuild() {
    if (buildController_) buildController_->cancel();
}

void MainWindow::buildProject(bool clean) {
    if (!project_) {
        QMessageBox::information(this, tr("Build"), tr("Open a project first."));
        return;
    }
    if (buildController_ && buildController_->running()) {
        QMessageBox::information(this, tr("Build"), tr("A build is already running."));
        return;
    }
    const auto configuration = configurationCombo_->currentText().toStdString();
    const auto architecture = architectureCombo_->currentText().toStdString();
    const auto toolchain = currentToolchain();
    auto graph = BuildController::buildGraphForProject(*project_, currentTargetId(), configuration,
                                                       architecture, toolchain,
                                                       project_->directory() / "build" /
                                                           configuration / architecture);
    if (!graph) {
        QMessageBox::critical(this, tr("Build"), QString::fromStdString(graph.error()));
        return;
    }

    BuildRequest request;
    request.project = project_->path();
    request.configuration = configuration;
    request.architecture = architecture;
    request.toolchain = toolchain;
    request.clean = clean;
    request.buildDirectory = project_->directory() / "build" / configuration / architecture;
    buildConsole_->clear();
    consoleTabs_->setCurrentWidget(buildConsole_);
    emit buildMessage(tr("Toolchain: %1 · SDK: %2").arg(
                          QString::fromStdString(toolchain.name),
                          QString::fromStdString(toolchain.sdk.string())), 0, 0);

    buildController_ = std::make_unique<BuildController>(request);
    buildController_->graph() = *graph;
    buildController_->start(
        [this](const BuildEvent& event) {
            if (event.type == BuildEvent::Type::Diagnostic) {
                emit buildMessage(QString("[%1] %2").arg(
                    QString::fromStdString(event.diagnostic.severityName()),
                    QString::fromStdString(event.text)), event.commandIndex, event.commandCount);
            } else if (event.type == BuildEvent::Type::Started ||
                       event.type == BuildEvent::Type::Command ||
                       event.type == BuildEvent::Type::Output ||
                       event.type == BuildEvent::Type::Cancelled) {
                emit buildMessage(QString::fromStdString(event.text), event.commandIndex,
                                  event.commandCount);
            }
        },
        [this](const BuildResult& result) {
            emit buildFinished(result.success, result.commandCount,
                               static_cast<int>(result.duration.count()));
        });
    statusLabel_->setText(tr("Build started…"));
}

void MainWindow::appendBuildMessage(QString text, int index, int total) {
    const auto prefix = total > 0 ? QString("[%1/%2] ").arg(index).arg(total) : QString();
    buildConsole_->appendPlainText(prefix + text.trimmed());
}

void MainWindow::finishBuild(bool success, int commands, int milliseconds) {
    buildConsole_->appendPlainText(
        tr("Build %1 — %2 commands, %3 ms")
            .arg(success ? tr("succeeded") : tr("failed"))
            .arg(commands)
            .arg(milliseconds));
    statusLabel_->setText(success ? tr("Build succeeded") : tr("Build failed"));
    buildController_.reset();
    refreshGit();
}

void MainWindow::refreshDiagnostics() {
    issuesConsole_->clear();
    int errors = 0;
    int warnings = 0;
    if (project_) {
        for (const auto& issue : inspectProject(*project_)) {
            issuesConsole_->appendPlainText(
                QString("[%1] %2: %3")
                    .arg(severityText(issue.severity))
                    .arg(QString::fromStdString(issue.category))
                    .arg(QString::fromStdString(issue.message)));
            if (issue.severity == Issue::Severity::Error) ++errors;
            if (issue.severity == Issue::Severity::Warning) ++warnings;
        }
    }
    for (const auto& toolchain : toolchains_.toolchains()) {
        for (const auto& issue : inspectToolchain(toolchain)) {
            if (issue.severity == Issue::Severity::Info) continue;
            issuesConsole_->appendPlainText(
                QString("[%1] %2: %3")
                    .arg(severityText(issue.severity))
                    .arg(QString::fromStdString(issue.category))
                    .arg(QString::fromStdString(issue.message)));
            if (issue.severity == Issue::Severity::Error) ++errors;
            else ++warnings;
        }
    }
    for (const auto& entry : diagnosticEntries_) issuesConsole_->appendPlainText(entry);
    issueLabel_->setText(tr("%1 errors, %2 warnings").arg(errors).arg(warnings));
}

void MainWindow::refreshGit() {
    if (!project_) {
        branchLabel_->setText(tr("no repository"));
        return;
    }
    const auto directory = project_->directory();
    std::thread([this, directory] {
        auto repository = GitRepository::open(directory);
        if (!repository) {
            emit gitStatusReady(QStringList{QString::fromStdString(repository.error())});
            emit gitBranchesReady(QStringList{}, QString());
            return;
        }
        QStringList lines;
        auto status = repository->status();
        if (status) {
            for (const auto& entry : *status)
                lines.push_back(QString("%1  %2  %3")
                                    .arg(QString(QChar(entry.indexStatus)))
                                    .arg(QString(QChar(entry.worktreeStatus)))
                                    .arg(QString::fromStdString(entry.path)));
        }
        QStringList branches;
        QString current;
        auto branchList = repository->branches();
        if (branchList) {
            for (const auto& branch : *branchList) {
                branches.push_back(QString::fromStdString(branch.name));
                if (branch.current) current = QString::fromStdString(branch.name);
            }
        }
        emit gitStatusReady(lines);
        emit gitBranchesReady(branches, current);
    }).detach();
}

void MainWindow::onGitStatusReady(QStringList entries) {
    gitConsole_->clear();
    if (entries.isEmpty()) gitConsole_->appendPlainText(tr("Working tree clean"));
    else for (const auto& entry : entries) gitConsole_->appendPlainText(entry);
}

void MainWindow::onGitBranchesReady(QStringList branches, QString current) {
    Q_UNUSED(branches);
    branchLabel_->setText(current.isEmpty() ? tr("no branch") : tr("⑂ %1").arg(current));
}

void MainWindow::commitChanges() {
    if (!project_) return;
    bool ok = false;
    const auto message = QInputDialog::getMultiLineText(this, tr("Commit"), tr("Message:"),
                                                        QString(), &ok);
    if (!ok || message.trimmed().isEmpty()) return;
    const auto directory = project_->directory();
    std::thread([this, directory, message] {
        auto repository = GitRepository::open(directory);
        if (!repository) { emit debugOutput(QString::fromStdString(repository.error())); return; }
        auto result = repository->commit(message.toStdString());
        emit debugOutput(result ? tr("Commit created") : QString::fromStdString(result.error()));
        refreshGit();
    }).detach();
}

void MainWindow::switchBranch() {
    if (!project_) return;
    bool ok = false;
    const auto branch = QInputDialog::getText(this, tr("Switch Branch"), tr("Branch:"),
                                              QLineEdit::Normal, QString(), &ok);
    if (!ok || branch.trimmed().isEmpty()) return;
    const auto directory = project_->directory();
    std::thread([this, directory, branch] {
        auto repository = GitRepository::open(directory);
        if (!repository) { emit debugOutput(QString::fromStdString(repository.error())); return; }
        auto result = repository->checkout(branch.toStdString());
        emit debugOutput(result ? tr("Switched branch") : QString::fromStdString(result.error()));
        refreshGit();
    }).detach();
}

void MainWindow::openSettings() {
    SettingsDialog dialog(settings_, resourcesDirectory_, this);
    if (dialog.exec() == QDialog::Accepted) applySettings();
}

void MainWindow::applySettings() {
    applyTheme();
    for (auto& [language, client] : languageServers_) {
        if (client) client->stop();
    }
    languageServers_.clear();
    if (auto* editor = currentEditor()) startLanguageServer(languageIdForPath(editor->filePath()));
    updateToolchainCombo();
    refreshDiagnostics();
}

void MainWindow::refreshToolchains() {
    toolchains_.detect();
    updateToolchainCombo();
    refreshDiagnostics();
}

void MainWindow::generateToolchainFile() {
    const auto path = QFileDialog::getSaveFileName(this, tr("CMake Toolchain File"),
                                                   "osxcross-toolchain.cmake",
                                                   tr("CMake files (*.cmake);;All Files (*)"));
    if (path.isEmpty()) return;
    auto result = generateOsxcrossToolchainFile(currentToolchain(),
                                                architectureCombo_->currentText().toStdString(),
                                                path.toStdString());
    if (!result) QMessageBox::critical(this, tr("Toolchain File"),
                                       QString::fromStdString(result.error()));
    else statusLabel_->setText(tr("Generated %1").arg(path));
}

void MainWindow::startDebugSession() {
    const auto command = settings_.string("debugAdapter");
    if (command.empty()) {
        QMessageBox::information(
            this, tr("Debug"),
            tr("No debug adapter configured. Install lldb-dap or another DAP adapter and set it in Settings."));
        return;
    }
    const auto program = QFileDialog::getOpenFileName(this, tr("Executable to Debug"));
    if (program.isEmpty()) return;
    debugClient_ = std::make_unique<DapClient>(command,
        project_ ? project_->directory().parent_path() : std::filesystem::path());
    debugClient_->setOutputHandler([this](const std::string& text) {
        emit debugOutput(QString::fromStdString(text));
    });
    debugClient_->setStoppedHandler([this](const nlohmann::json& body) {
        emit debugOutput(tr("Stopped: %1")
                             .arg(QString::fromStdString(body.value("reason", std::string("unknown")))));
    });
    if (!debugClient_->start()) {
        QMessageBox::critical(this, tr("Debug"), tr("Could not start the debug adapter."));
        debugClient_.reset();
        return;
    }
    consoleTabs_->setCurrentWidget(debugConsole_);
    auto initialized = debugClient_->initialize();
    if (!initialized) {
        emit debugOutput(QString::fromStdString(initialized.error()));
        return;
    }
    if (auto* editor = currentEditor()) {
        auto cursor = editor->textCursor();
        auto breakpoints = debugClient_->setBreakpoints(editor->filePath(),
                                                        {cursor.blockNumber() + 1});
        if (!breakpoints) emit debugOutput(QString::fromStdString(breakpoints.error()));
    }
    auto launched = debugClient_->launch(std::filesystem::path(program.toStdString()), {},
                                         project_ ? project_->directory().parent_path()
                                                  : std::filesystem::path());
    if (!launched) emit debugOutput(QString::fromStdString(launched.error()));
    auto done = debugClient_->configurationDone();
    if (!done) emit debugOutput(QString::fromStdString(done.error()));
    statusLabel_->setText(tr("Debug session started"));
}

void MainWindow::onDebugOutput(QString text) {
    debugConsole_->appendPlainText(text.trimmed());
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About LXcode"),
        tr("<b>LXcode %1</b><br><br>"
           "An independent, open-source, Linux-native IDE for Xcode projects.<br>"
           "LXcode is not affiliated with, endorsed by, or derived from Apple Inc.<br><br>"
           "Xcode is a trademark of Apple Inc.").arg(QLatin1String("0.1.0")));
}

bool MainWindow::promptSaveModified() {
    for (int i = 0; i < editorTabs_->count(); ++i) {
        auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(i));
        if (!editor || !editor->isModified()) continue;
        const auto answer = QMessageBox::question(
            this, tr("Unsaved Changes"),
            tr("Save changes to %1?").arg(editorTabs_->tabText(i)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save && !editor->saveFile()) return false;
    }
    return true;
}

void MainWindow::saveDocument() {
    if (auto* editor = currentEditor()) {
        if (editor->saveFile()) statusLabel_->setText(tr("Saved %1").arg(
            QString::fromStdString(editor->filePath().filename().string())));
    }
}

void MainWindow::saveAllDocuments() {
    for (int i = 0; i < editorTabs_->count(); ++i) {
        if (auto* editor = qobject_cast<CodeEditor*>(editorTabs_->widget(i)))
            if (editor->isModified()) editor->saveFile();
    }
    statusLabel_->setText(tr("All documents saved"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!promptSaveModified()) {
        event->ignore();
        return;
    }
    if (buildController_) buildController_->cancel();
    for (auto& [language, client] : languageServers_) {
        if (client) client->stop();
    }
    languageServers_.clear();
    if (debugClient_) debugClient_->stop();
    settings_.save();
    event->accept();
}

} // namespace lxcode::gui
