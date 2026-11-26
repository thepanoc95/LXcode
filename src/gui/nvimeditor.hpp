#pragma once
#include "gui/nvimrpc.hpp"
#include "gui/theme.hpp"
#include "lxcode/lsp/client.hpp"

#include <QColor>
#include <QFont>
#include <QPair>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <vector>

class QKeyEvent;
class QListWidget;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace lxcode::gui {

class NvimCompletionPopup;

// A Qt widget backed by a real, headless Neovim instance. Neovim owns all
// editing, key handling and syntax; this class renders its UI grid and
// translates Qt input into Neovim key sequences.
class NeovimEditor : public QWidget {
    Q_OBJECT
public:
    explicit NeovimEditor(QWidget* parent = nullptr);
    ~NeovimEditor() override;

    static QString findExecutable();
    bool start(const QString& executable, QString* error = nullptr);
    bool isRunning() const { return rpc_.isRunning(); }

    void setFilePath(const std::filesystem::path& path);
    std::filesystem::path filePath() const { return path_; }
    bool loadFile();
    bool saveFile();
    bool isModified() const { return modified_; }
    void applyTheme(const Theme& theme, int fontSize, int tabSize, bool spaces);

    void setDiagnostics(const std::vector<LspDiagnostic>& diagnostics);
    void setCompletionItems(const std::vector<CompletionItem>& items);
    void revealLine(int line, int column = 1);
    QString wordUnderCursor() const;
    QString toPlainText() const;
    QPair<int, int> cursorPosition() const;
    static QString languageId(const std::filesystem::path& path);

signals:
    void modificationChanged(bool changed);
    void cursorPositionChanged(int line, int column);
    void completionRequested(int line, int column);
    void definitionRequested(int line, int column);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void insertCompletion(const QString& text);

private:
    struct Cell {
        QString text;
        QColor foreground;
        QColor background;
        bool bold = false;
        bool italic = false;
        bool underline = false;
    };

    void handleNotification(const QByteArray& method, const MsgPackValue& params);
    void handleRedraw(const MsgPackValue& events);
    void applyGridLine(const MsgPackValue& arguments);
    void applyPut(const MsgPackValue& arguments);
    void clearGrid();
    void resizeGrid(int columns, int rows);
    void scrollRegion(int count);
    void syncViewportSize();
    void updateHighlights();
    QString keySequence(QKeyEvent* event) const;
    void execLua(const QByteArray& code, const MsgPackValue& arguments = MsgPackValue::nil());
    QColor colorFrom(const MsgPackValue* value, const QColor& fallback) const;
    MsgPackValue stringArray(const QStringList& lines) const;
    void clearCell(int row, int column);

    NvimRpc rpc_;
    std::filesystem::path path_;
    Theme theme_;
    int fontSize_ = 12;
    int tabSize_ = 4;
    bool insertSpaces_ = true;
    QFont font_;
    int cellWidth_ = 9;
    int cellHeight_ = 18;

    int columns_ = 0;
    int rows_ = 0;
    int cursorRow_ = 0;
    int cursorColumn_ = 0;
    int scrollTop_ = 0;
    int scrollBottom_ = 0;
    int scrollLeft_ = 0;
    int scrollRight_ = 0;
    QColor defaultForeground_;
    QColor defaultBackground_;
    std::vector<std::vector<Cell>> grid_;
    std::vector<MsgPackValue> highlights_;

    NvimCompletionPopup* completion_ = nullptr;
    bool modified_ = false;
    bool trailingNewline_ = true;
};

} // namespace lxcode::gui
