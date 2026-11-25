#pragma once
#include "gui/theme.hpp"
#include "lxcode/lsp/client.hpp"

#include <QListWidget>
#include <QPlainTextEdit>
#include <QString>
#include <QTextEdit>
#include <QWidget>

#include <filesystem>
#include <memory>
#include <set>
#include <vector>

class QLineEdit;
class QToolButton;

namespace lxcode::gui {

class CompletionPopup;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    ~CodeEditor() override;

    void setFilePath(const std::filesystem::path& path);
    std::filesystem::path filePath() const { return path_; }
    bool loadFile();
    bool saveFile();
    bool isModified() const { return document()->isModified(); }
    void applyTheme(const Theme& theme, int fontSize, int tabSize, bool spaces);

    void setDiagnostics(const std::vector<LspDiagnostic>& diagnostics);
    void setCompletionItems(const std::vector<CompletionItem>& items);
    void revealLine(int line, int column = 1);
    QString wordUnderCursor() const;

    static QString languageId(const std::filesystem::path& path);

signals:
    void modificationChanged(bool changed);
    void cursorPositionChanged(int line, int column);
    void completionRequested(int line, int column);
    void definitionRequested(int line, int column);
    void hoverRequested(int line, int column);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void updateLineNumbers();
    void highlightCurrentLineAndBrackets();
    void findNext();
    void replaceCurrent();
    void replaceAll();

private:
    friend class LineNumberArea;
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent* event);
    void toggleFold(int line);
    bool isFoldable(int line) const;
    void updateExtraSelections();
    void insertCompletion(const QString& text);
    void emitPosition();
    void applyIndentationOnNewline();

    std::filesystem::path path_;
    QWidget* lineNumberArea_ = nullptr;
    class Highlighter;
    std::unique_ptr<Highlighter> highlighter_;
    CompletionPopup* completion_ = nullptr;
    QWidget* searchBar_ = nullptr;
    QLineEdit* searchField_ = nullptr;
    QLineEdit* replaceField_ = nullptr;
    std::vector<LspDiagnostic> diagnostics_;
    std::set<int> foldedLines_;
    QList<QTextEdit::ExtraSelection> extraLine_;
    Theme theme_;
    int tabSize_ = 4;
    bool insertSpaces_ = true;
};

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return QSize(editor_->lineNumberWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintLineNumbers(event); }
    void mousePressEvent(QMouseEvent* event) override;

private:
    CodeEditor* editor_;
};

class CompletionPopup : public QListWidget {
    Q_OBJECT
public:
    explicit CompletionPopup(CodeEditor* editor);
    bool event(QEvent* event) override;

private:
    CodeEditor* editor_ = nullptr;

signals:
    void chosen(const QString& text);
};

} // namespace lxcode::gui
