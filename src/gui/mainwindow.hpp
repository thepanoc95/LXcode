#pragma once
#include "gui/nvimeditor.hpp"
#include "gui/theme.hpp"
#include "lxcode/build/buildsystem.hpp"
#include "lxcode/config/settings.hpp"
#include "lxcode/debug/dapclient.hpp"
#include "lxcode/lsp/client.hpp"
#include "lxcode/project/xcodeproject.hpp"
#include "lxcode/sourcecontrol/git.hpp"
#include "lxcode/toolchains/toolchain.hpp"

#include <QCloseEvent>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace lxcode::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QString projectArgument, std::string resourcesDirectory,
               QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void openProjectDialog();
    void openPath(const QString& path);
    void openRecent();
    void createProjectDialog();
    void saveDocument();
    void saveAllDocuments();
    void closeEvent(QCloseEvent* event) override;

    void onNavigatorActivated(QTreeWidgetItem* item, int column);
    void onNavigatorCurrentChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onNavigatorContextMenu(const QPoint& point);
    void filterNavigator(const QString& text);
    void addExistingFiles();
    void addGroup();
    void renameNavigatorItem();
    void removeNavigatorItem();

    void onTabCloseRequested(int index);
    void onTabChanged(int index);
    void onDocumentModified(bool modified);
    void onCursorPosition(int line, int column);
    void requestCompletion(int line, int column);
    void requestDefinition(int line, int column);
    void onCompletionReady(QStringList items);
    void onDefinitionReady(QString path, int line, int column);

    void buildProject(bool clean);
    void startBuild();
    void startClean();
    void cancelBuild();
    void appendBuildMessage(QString text, int index, int total);
    void finishBuild(bool success, int commands, int milliseconds);
    void refreshDiagnostics();

    void refreshGit();
    void onGitStatusReady(QStringList entries);
    void onGitBranchesReady(QStringList branches, QString current);
    void commitChanges();
    void switchBranch();

    void openFileFromArgument(const QString& path);
    void openSettings();
    void applySettings();
    void refreshToolchains();
    void generateToolchainFile();
    void startDebugSession();
    void onDebugOutput(QString text);
    void showAbout();

signals:
    void buildMessage(QString text, int index, int total);
    void buildFinished(bool success, int commands, int milliseconds);
    void gitStatusReady(QStringList entries);
    void gitBranchesReady(QStringList branches, QString current);
    void completionReady(QStringList items);
    void definitionReady(QString path, int line, int column);
    void debugOutput(QString text);

private:
    void buildUi();
    void buildToolbar();
    void buildNavigator();
    void buildInspector();
    void buildConsole();
    void applyTheme();
    void updateWindowTitle();
    void updateTargetCombo();
    void updateToolchainCombo();
    void loadProject(const std::filesystem::path& path);
    void populateNavigator();
    void addNavigatorItems(QTreeWidgetItem* parent,
                           const std::vector<NavigatorItem>& items);
    void openFile(const std::filesystem::path& path, int line = 0, int column = 0);
    NeovimEditor* currentEditor() const;
    NeovimEditor* editorForPath(const std::filesystem::path& path) const;
    void updateInspector(QTreeWidgetItem* item);
    void startLanguageServer(const std::string& languageId);
    std::shared_ptr<LspClient> languageClient(const std::string& languageId);
    std::filesystem::path resolveProjectPath(const std::filesystem::path& path) const;
    Toolchain currentToolchain() const;
    std::string currentTargetId() const;
    bool promptSaveModified();

    Settings settings_;
    Theme theme_;
    std::string resourcesDirectory_;
    std::unique_ptr<XcodeProject> project_;
    std::filesystem::path projectPath_;
    ToolchainRegistry toolchains_;
    std::unique_ptr<BuildController> buildController_;
    std::map<std::string, std::shared_ptr<LspClient>> languageServers_;
    std::unique_ptr<DapClient> debugClient_;

    QToolBar* toolbar_ = nullptr;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* inspectorDock_ = nullptr;
    QDockWidget* consoleDock_ = nullptr;
    QTreeWidget* navigatorTree_ = nullptr;
    QLineEdit* navigatorFilter_ = nullptr;
    QTreeWidget* inspectorTree_ = nullptr;
    QTabWidget* editorTabs_ = nullptr;
    QTabWidget* consoleTabs_ = nullptr;
    QPlainTextEdit* buildConsole_ = nullptr;
    QPlainTextEdit* issuesConsole_ = nullptr;
    QPlainTextEdit* debugConsole_ = nullptr;
    QPlainTextEdit* gitConsole_ = nullptr;
    QComboBox* targetCombo_ = nullptr;
    QComboBox* configurationCombo_ = nullptr;
    QComboBox* architectureCombo_ = nullptr;
    QComboBox* toolchainCombo_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* branchLabel_ = nullptr;
    QLabel* cursorLabel_ = nullptr;
    QLabel* issueLabel_ = nullptr;
    QStringList diagnosticEntries_;
};

} // namespace lxcode::gui
