#include "gui/theme.hpp"

#include "lxcode/core/filesystem.hpp"

#include <nlohmann/json.hpp>

#include <QFileInfo>
#include <QFont>
#include <QPalette>

#include <algorithm>

namespace lxcode::gui {

namespace {

QColor color(const nlohmann::json& json, const char* key, const char* fallback) {
    return QColor(QString::fromStdString(json.value(key, std::string(fallback))));
}

} // namespace

Theme Theme::load(const std::string& name, const std::string& resourcesDirectory) {
    Theme theme;
    std::filesystem::path path;
    if (!resourcesDirectory.empty())
        path = std::filesystem::path(resourcesDirectory) / "themes" / (name + ".json");
    if (path.empty() || !fs::exists(path)) {
        path = std::filesystem::path("resources/themes") / (name + ".json");
    }
    if (!fs::exists(path)) {
        // Built-in fallback mirrors dark.json; the application remains usable
        // even when installed without resource files.
        path = "resources/themes/dark.json";
    }
    auto text = fs::readText(path);
    if (!text) return theme;
    try {
        const auto json = nlohmann::json::parse(*text);
        theme.name = json.value("name", name);
        theme.dark = json.value("appearance", std::string("dark")) == "dark";
        theme.window = color(json, "window", "#1d1f24");
        theme.panel = color(json, "panel", "#24262d");
        theme.panelAlt = color(json, "panelAlt", "#2b2e36");
        theme.border = color(json, "border", "#383c47");
        theme.text = color(json, "text", "#e2e4e9");
        theme.muted = color(json, "muted", "#9aa0ad");
        theme.accent = color(json, "accent", "#4f9dff");
        theme.selection = color(json, "selection", "#3a5f8a");
        const auto& editor = json.value("editor", nlohmann::json::object());
        theme.editor.background = color(editor, "background", "#1e2026");
        theme.editor.foreground = color(editor, "foreground", "#d8dce5");
        theme.editor.lineNumber = color(editor, "lineNumber", "#6d7482");
        theme.editor.currentLine = color(editor, "currentLine", "#262a33");
        theme.editor.selection = color(editor, "selection", "#3a5f8a");
        theme.editor.keyword = color(editor, "keyword", "#c586c0");
        theme.editor.type = color(editor, "type", "#4ec9b0");
        theme.editor.function = color(editor, "function", "#dcdcaa");
        theme.editor.string = color(editor, "string", "#ce9178");
        theme.editor.number = color(editor, "number", "#b5cea8");
        theme.editor.comment = color(editor, "comment", "#6a9955");
        theme.editor.preprocessor = color(editor, "preprocessor", "#c586c0");
        theme.editor.error = color(editor, "error", "#f14c4c");
        theme.editor.warning = color(editor, "warning", "#cca700");
    } catch (...) {
        // Keep the fallback colors on malformed user themes.
    }
    return theme;
}

std::vector<std::string> Theme::available(const std::string& resourcesDirectory) {
    std::vector<std::string> result;
    if (resourcesDirectory.empty()) return {"dark", "light"};
    for (const auto& path : fs::filesWithExtension(resourcesDirectory + "/themes", "json", 1))
        result.push_back(path.stem().string());
    if (result.empty()) result = {"dark", "light"};
    std::sort(result.begin(), result.end());
    return result;
}

QTextCharFormat Theme::format(TokenKind kind, int fontSize, bool bold) const {
    QTextCharFormat format;
    format.setFontFamilies({"JetBrains Mono", "DejaVu Sans Mono", "monospace"});
    format.setFontPointSize(fontSize);
    if (bold) format.setFontWeight(QFont::Bold);
    switch (kind) {
    case TokenKind::Plain: format.setForeground(editor.foreground); break;
    case TokenKind::Keyword: format.setForeground(editor.keyword); format.setFontWeight(QFont::Bold); break;
    case TokenKind::Type: format.setForeground(editor.type); break;
    case TokenKind::Function: format.setForeground(editor.function); break;
    case TokenKind::String: format.setForeground(editor.string); break;
    case TokenKind::Number: format.setForeground(editor.number); break;
    case TokenKind::Comment: format.setForeground(editor.comment); format.setFontItalic(true); break;
    case TokenKind::Preprocessor: format.setForeground(editor.preprocessor); break;
    }
    return format;
}

QString Theme::stylesheet() const {
    return QString(R"(
QWidget { background: %1; color: %2; font-size: 12px; }
QMainWindow::separator { background: %3; width: 1px; height: 1px; }
QToolBar { background: %4; border-bottom: 1px solid %5; spacing: 4px; padding: 4px; }
QToolBar QToolButton { border-radius: 4px; padding: 4px 7px; }
QToolBar QToolButton:hover { background: %6; }
QToolBar QToolButton:checked { background: %7; color: %2; }
QTreeView, QListView, QTableView { background: %4; alternate-background-color: %8;
  border: 0; padding: 2px; selection-background-color: %7; }
QTreeView::item:hover, QListView::item:hover { background: %8; }
QTabWidget::pane { border: 0; background: %1; }
QTabBar::tab { background: %4; color: %9; padding: 7px 14px; border: 0; }
QTabBar::tab:selected { background: %1; color: %2; border-bottom: 2px solid %10; }
QTabBar::tab:hover { background: %8; }
QPushButton { background: %4; border: 1px solid %5; border-radius: 4px; padding: 5px 10px; }
QPushButton:hover { background: %8; }
QPushButton:pressed { background: %7; }
QLineEdit, QComboBox, QSpinBox { background: %1; border: 1px solid %5;
  border-radius: 4px; padding: 4px 6px; selection-background-color: %7; }
QComboBox::drop-down { border: 0; }
QStatusBar { background: %4; color: %9; }
QDockWidget::title { background: %4; padding: 5px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: %5; border-radius: 5px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QMenu { background: %4; border: 1px solid %5; }
QMenu::item:selected { background: %7; }
QToolTip { background: %4; color: %2; border: 1px solid %5; }
QSplitter::handle { background: %3; }
)")
        .arg(panelAlt.name(), text.name(), border.name(), panel.name(), border.name(),
             panelAlt.name(), selection.name(), panel.name(), muted.name(), accent.name());
}

void Theme::apply(QApplication& application) const {
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, panelAlt);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, panel);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::ToolTipBase, panel);
    palette.setColor(QPalette::ToolTipText, text);
    application.setPalette(palette);
    application.setStyleSheet(stylesheet());
}

} // namespace lxcode::gui
