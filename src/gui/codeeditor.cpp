#include "gui/codeeditor.hpp"

#include <tree_sitter/api.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>
#include <functional>
#include <set>

extern "C" const TSLanguage* tree_sitter_c(void);

namespace lxcode::gui {

namespace {

bool isKeyword(const std::string& word) {
    static const std::set<std::string> words = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
        "return", "goto", "sizeof", "typedef", "struct", "enum", "union", "class", "namespace",
        "using", "template", "typename", "public", "private", "protected", "virtual", "override",
        "const", "constexpr", "static", "inline", "extern", "register", "volatile", "mutable",
        "auto", "void", "bool", "char", "short", "int", "long", "float", "double", "signed",
        "unsigned", "nullptr", "true", "false", "try", "catch", "throw", "new", "delete", "this",
        "operator", "decltype", "noexcept", "concept", "requires"};
    return words.count(word) != 0;
}

TokenKind classify(const std::string& type, const std::string& text,
                   const std::string& parentType) {
    if (type == "comment") return TokenKind::Comment;
    if (type == "string_literal" || type == "char_literal" || type == "string" ||
        type == "concatenated_string")
        return TokenKind::String;
    if (type == "number_literal" || type == "int_literal" || type == "float_literal")
        return TokenKind::Number;
    if (type == "preproc_include" || type == "preproc_def" || type == "preproc_function_def" ||
        type == "preproc_call" || type == "preproc_arg")
        return TokenKind::Preprocessor;
    if (type == "type_identifier" || type == "primitive_type" || type == "struct_specifier" ||
        type == "enum_specifier" || type == "union_specifier" || type == "class_specifier" ||
        type == "type_definition")
        return TokenKind::Type;
    if (type == "identifier" &&
        (parentType == "call_expression" || parentType == "function_declarator" ||
         parentType == "function_definition"))
        return TokenKind::Function;
    if (isKeyword(text)) return TokenKind::Keyword;
    return TokenKind::Plain;
}

} // namespace

class CodeEditor::Highlighter : public QSyntaxHighlighter {
public:
    Highlighter(QTextDocument* document, CodeEditor* editor, const Theme& theme)
        : QSyntaxHighlighter(document), editor_(editor), theme_(theme) {
        parser_ = ts_parser_new();
        tree_ = nullptr;
        dirty_ = true;
    }

    ~Highlighter() override {
        if (tree_) ts_tree_delete(tree_);
        ts_parser_delete(parser_);
    }

    void setTheme(const Theme& theme) { theme_ = theme; }
    // QSyntaxHighlighter::rehighlight() emits document changes, which in turn
    // emit textChanged. Calling rehighlight() directly from a textChanged
    // handler recurses until the stack overflows, so all callers go through
    // this re-entrancy guard.
    void refresh() {
        if (refreshing_) return;
        refreshing_ = true;
        rehighlight();
        refreshing_ = false;
    }
    // QSyntaxHighlighter's destructor rehighlights while this derived object
    // is being torn down, so CodeEditor disables refresh before destruction.
    void setEnabled(bool value) { enabled_ = value; }
    bool shouldRefresh() const { return enabled_ && !refreshing_; }
    void setLanguage(const QString& language) {
        const TSLanguage* languagePtr = nullptr;
        if (language == "c" || language == "cpp" || language == "objective-c" ||
            language == "objective-cpp")
            languagePtr = tree_sitter_c();
        ts_parser_set_language(parser_, languagePtr);
        dirty_ = true;
        refresh();
    }
    void markDirty() { dirty_ = true; }

protected:
    void highlightBlock(const QString& text) override {
        if (dirty_) parseDocument();
        const int start = currentBlock().position();
        const int end = start + text.size();
        for (const auto& range : ranges_) {
            if (range.end <= start || range.start >= end) continue;
            const int from = std::max(0, range.start - start);
            const int to = std::min(static_cast<int>(text.size()), range.end - start);
            if (to > from) setFormat(from, to - from, theme_.format(range.kind));
        }
        Q_UNUSED(currentBlockState());
    }

private:
    struct Range {
        int start = 0;
        int end = 0;
        TokenKind kind = TokenKind::Plain;
    };

    void parseDocument() {
        dirty_ = false;
        ranges_.clear();
        if (!parser_ || !ts_parser_language(parser_)) return;
        const QByteArray source = editor_->toPlainText().toUtf8();
        TSTree* next = ts_parser_parse_string(parser_, nullptr, source.constData(),
                                              static_cast<uint32_t>(source.size()));
        if (tree_) ts_tree_delete(tree_);
        tree_ = next;
        if (!tree_) return;

        offsets_.assign(static_cast<std::size_t>(source.size()) + 1, 0);
        std::size_t units = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(source.size()); ++i) {
            offsets_[i] = static_cast<int>(units);
            const unsigned char c = static_cast<unsigned char>(source[static_cast<int>(i)]);
            if ((c & 0xc0) != 0x80) ++units;
        }
        offsets_[source.size()] = static_cast<int>(units);

        std::function<void(TSNode, const std::string&)> walk = [&](TSNode node,
                                                                   const std::string& parentType) {
            const std::string type = ts_node_type(node);
            const auto startByte = ts_node_start_byte(node);
            const auto endByte = ts_node_end_byte(node);
            const std::string text = source.mid(static_cast<int>(startByte),
                                                static_cast<int>(endByte - startByte)).toStdString();
            const auto kind = classify(type, text, parentType);
            if (kind != TokenKind::Plain && startByte < offsets_.size() && endByte < offsets_.size()) {
                ranges_.push_back({offsets_[startByte], offsets_[endByte], kind});
            }
            const auto count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) walk(ts_node_child(node, i), type);
        };
        walk(ts_tree_root_node(tree_), {});
    }

    CodeEditor* editor_;
    Theme theme_;
    TSParser* parser_ = nullptr;
    TSTree* tree_ = nullptr;
    bool refreshing_ = false;
    bool enabled_ = true;
    bool dirty_ = true;
    std::vector<Range> ranges_;
    std::vector<int> offsets_;
};

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);
    highlighter_ = std::make_unique<Highlighter>(document(), this, theme_);

    completion_ = new CompletionPopup(this);
    completion_->hide();

    searchBar_ = new QWidget(this);
    auto* layout = new QHBoxLayout(searchBar_);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    searchField_ = new QLineEdit(searchBar_);
    searchField_->setPlaceholderText(tr("Find"));
    replaceField_ = new QLineEdit(searchBar_);
    replaceField_->setPlaceholderText(tr("Replace"));
    auto* findButton = new QPushButton(tr("Find"), searchBar_);
    auto* replaceButton = new QPushButton(tr("Replace"), searchBar_);
    auto* replaceAllButton = new QPushButton(tr("All"), searchBar_);
    auto* closeButton = new QToolButton(searchBar_);
    closeButton->setText("×");
    layout->addWidget(searchField_);
    layout->addWidget(replaceField_);
    layout->addWidget(findButton);
    layout->addWidget(replaceButton);
    layout->addWidget(replaceAllButton);
    layout->addWidget(closeButton);
    searchBar_->hide();
    connect(findButton, &QPushButton::clicked, this, &CodeEditor::findNext);
    connect(replaceButton, &QPushButton::clicked, this, &CodeEditor::replaceCurrent);
    connect(replaceAllButton, &QPushButton::clicked, this, &CodeEditor::replaceAll);
    connect(closeButton, &QToolButton::clicked, this, [this] { searchBar_->hide(); });
    connect(searchField_, &QLineEdit::returnPressed, this, &CodeEditor::findNext);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumbers);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect&, int) {
        lineNumberArea_->update();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        highlightCurrentLineAndBrackets();
        emitPosition();
        completion_->hide();
    });
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        if (!highlighter_->shouldRefresh()) return;
        highlighter_->markDirty();
        highlighter_->refresh();
        updateExtraSelections();
    });
    connect(this, &QPlainTextEdit::modificationChanged, this, &CodeEditor::modificationChanged);
    connect(completion_, &CompletionPopup::chosen, this, &CodeEditor::insertCompletion);

    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);
    setMouseTracking(true);
    updateLineNumbers();
    highlightCurrentLineAndBrackets();
}

CodeEditor::~CodeEditor() {
    if (highlighter_) highlighter_->setEnabled(false);
}

void CodeEditor::setFilePath(const std::filesystem::path& path) {
    path_ = path;
    highlighter_->setLanguage(languageId(path));
}

QString CodeEditor::languageId(const std::filesystem::path& path) {
    return QString::fromStdString(lxcode::languageIdForPath(path));
}

bool CodeEditor::loadFile() {
    QFile file(QString::fromStdString(path_.string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    setPlainText(QString::fromUtf8(file.readAll()));
    document()->setModified(false);
    highlighter_->markDirty();
    highlighter_->refresh();
    return true;
}

bool CodeEditor::saveFile() {
    if (path_.empty()) return false;
    QFile file(QString::fromStdString(path_.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    file.write(toPlainText().toUtf8());
    document()->setModified(false);
    return true;
}

void CodeEditor::applyTheme(const Theme& theme, int fontSize, int tabSize, bool spaces) {
    theme_ = theme;
    tabSize_ = std::max(1, tabSize);
    insertSpaces_ = spaces;
    highlighter_->setTheme(theme);
    QFont font;
    font.setFamilies({"JetBrains Mono", "DejaVu Sans Mono", "monospace"});
    font.setPointSize(fontSize);
    setFont(font);
    QTextCharFormat plain = theme.format(TokenKind::Plain, fontSize);
    setCurrentCharFormat(plain);
    highlighter_->refresh();
    updateExtraSelections();
    lineNumberArea_->update();
    viewport()->update();
}

void CodeEditor::setDiagnostics(const std::vector<LspDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    updateExtraSelections();
}

void CodeEditor::setCompletionItems(const std::vector<CompletionItem>& items) {
    completion_->clear();
    for (const auto& item : items) {
        auto* entry = new QListWidgetItem(completion_);
        entry->setText(QString::fromStdString(item.label));
        if (!item.detail.empty()) entry->setToolTip(QString::fromStdString(item.detail));
        entry->setData(Qt::UserRole, QString::fromStdString(
            item.insertText.empty() ? item.label : item.insertText));
    }
    if (completion_->count() > 0) {
        completion_->setCurrentRow(0);
        completion_->show();
        completion_->raise();
    }
}

void CodeEditor::revealLine(int line, int column) {
    QTextCursor cursor(document()->findBlockByNumber(std::max(0, line - 1)));
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, std::max(0, column - 1));
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

QString CodeEditor::wordUnderCursor() const {
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
    cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    if (completion_->isVisible()) {
        if (event->key() == Qt::Key_Down) { completion_->setCurrentRow(completion_->currentRow() + 1); return; }
        if (event->key() == Qt::Key_Up) { completion_->setCurrentRow(completion_->currentRow() - 1); return; }
        if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return || event->key() == Qt::Key_Tab) {
            auto* item = completion_->currentItem();
            if (item) { insertCompletion(item->data(Qt::UserRole).toString()); return; }
        }
        if (event->key() == Qt::Key_Escape) { completion_->hide(); return; }
    }
    if (event->key() == Qt::Key_F && event->modifiers() & Qt::ControlModifier) {
        searchBar_->show();
        searchField_->setFocus();
        searchField_->selectAll();
        return;
    }
    if (event->key() == Qt::Key_Space && event->modifiers() & Qt::ControlModifier) {
        auto cursor = textCursor();
        emit completionRequested(cursor.blockNumber() + 1, cursor.columnNumber());
        return;
    }
    if (event->key() == Qt::Key_F12) {
        auto cursor = textCursor();
        emit definitionRequested(cursor.blockNumber() + 1, cursor.columnNumber());
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        applyIndentationOnNewline();
        return;
    }
    if (event->key() == Qt::Key_Tab && insertSpaces_) {
        insertPlainText(QString(tabSize_, ' '));
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    if (lineNumberArea_) {
        const QRect rect = contentsRect();
        lineNumberArea_->setGeometry(QRect(rect.left(), rect.top(), lineNumberWidth(), rect.height()));
    }
}

void CodeEditor::focusInEvent(QFocusEvent* event) {
    QPlainTextEdit::focusInEvent(event);
    emitPosition();
}

void CodeEditor::mouseMoveEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseMoveEvent(event);
    const int position = cursorForPosition(event->pos()).position();
    if (position < 0) {
        QToolTip::hideText();
        return;
    }
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.file != path_.string() || diagnostic.line <= 0) continue;
        const auto block = document()->findBlockByNumber(diagnostic.line - 1);
        if (position >= block.position() && position < block.position() + block.length()) {
            QToolTip::showText(event->globalPosition().toPoint(),
                               QString::fromStdString(diagnostic.message), this);
            return;
        }
    }
    QToolTip::hideText();
}

void CodeEditor::applyIndentationOnNewline() {
    QTextCursor cursor = textCursor();
    const QString line = cursor.block().text();
    QString indent;
    for (int i = 0; i < line.size() && (line[i] == ' ' || line[i] == '\t'); ++i) indent.push_back(line[i]);
    int depth = 0;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '{') ++depth;
        if (line[i] == '}') --depth;
    }
    if (depth > 0) indent += insertSpaces_ ? QString(tabSize_, ' ') : QString("\t");
    insertPlainText("\n" + indent);
}

void CodeEditor::insertCompletion(const QString& text) {
    completion_->hide();
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

int CodeEditor::lineNumberWidth() const {
    int digits = 1;
    int maxLine = std::max(1, blockCount());
    while (maxLine >= 10) { maxLine /= 10; ++digits; }
    return 16 + digits * fontMetrics().horizontalAdvance('9') + 12;
}

void CodeEditor::updateLineNumbers() {
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
    if (lineNumberArea_) lineNumberArea_->update();
}

void CodeEditor::paintLineNumbers(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), theme_.panel);
    painter.setPen(theme_.border);
    painter.drawLine(event->rect().right(), event->rect().top(), event->rect().right(),
                     event->rect().bottom());
    QTextBlock block = firstVisibleBlock();
    int line = block.blockNumber();
    const int spacing = fontMetrics().height();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && top >= event->rect().top()) {
            const bool current = block.contains(textCursor().position());
            painter.setPen(current ? theme_.text : theme_.editor.lineNumber);
            QFont font = painter.font();
            font.setBold(current);
            painter.setFont(font);
            const int width = lineNumberWidth() - 20;
            painter.drawText(0, top, width, spacing, Qt::AlignRight | Qt::AlignTop,
                             QString::number(line + 1));
            if (isFoldable(line)) {
                painter.setPen(theme_.muted);
                painter.drawText(width + 4, top, 12, spacing, Qt::AlignLeft | Qt::AlignTop,
                                 foldedLines_.count(line) ? "▸" : "▾");
            }
        }
        top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).bottom());
        block = block.next();
        ++line;
    }
}

bool CodeEditor::isFoldable(int line) const {
    const auto block = document()->findBlockByNumber(line);
    if (!block.isValid()) return false;
    const QString text = block.text();
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        if (text[i] == '}') --depth;
    }
    return depth > 0;
}

void CodeEditor::toggleFold(int line) {
    const auto block = document()->findBlockByNumber(line);
    if (block.isValid() && isFoldable(line)) {
        if (foldedLines_.count(line)) foldedLines_.erase(line);
        else foldedLines_.insert(line);
        updateLineNumbers();
        viewport()->update();
    }
}

void CodeEditor::highlightCurrentLineAndBrackets() {
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(theme_.editor.currentLine);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extraLine_.clear();
    extraLine_.push_back(selection);
    updateExtraSelections();
}

void CodeEditor::updateExtraSelections() {
    auto selections = extraLine_;
    QTextCursor cursor = textCursor();
    const int position = cursor.position();
    const QString text = toPlainText();
    auto matching = [](QChar c) -> QChar {
        switch (c.unicode()) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return {};
        }
    };
    if (position > 0 && position <= text.size()) {
        const QChar left = text[position - 1];
        const QChar match = matching(left);
        if (!match.isNull()) {
            int depth = 0;
            const bool forward = left == '(' || left == '[' || left == '{';
            for (int i = position - 1; i >= 0 && i < text.size(); forward ? ++i : --i) {
                if (text[i] == left) ++depth;
                if (text[i] == match && --depth == 0) {
                    QTextEdit::ExtraSelection bracket;
                    bracket.format.setForeground(theme_.accent);
                    bracket.format.setFontWeight(QFont::Bold);
                    bracket.cursor = cursor;
                    bracket.cursor.setPosition(i);
                    bracket.cursor.setPosition(i + 1, QTextCursor::KeepAnchor);
                    selections.push_back(bracket);
                    QTextEdit::ExtraSelection current;
                    current.format = bracket.format;
                    current.cursor = textCursor();
                    current.cursor.setPosition(position - 1);
                    current.cursor.setPosition(position, QTextCursor::KeepAnchor);
                    selections.push_back(current);
                    break;
                }
            }
        }
    }
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.file != path_.string() || diagnostic.line <= 0) continue;
        auto block = document()->findBlockByNumber(diagnostic.line - 1);
        if (!block.isValid()) continue;
        QTextEdit::ExtraSelection item;
        QTextCharFormat format;
        const bool isError = diagnostic.severity == "1" || diagnostic.severity == "error";
        QColor color = isError ? theme_.editor.error : theme_.editor.warning;
        format.setUnderlineStyle(isError ? QTextCharFormat::WaveUnderline
                                         : QTextCharFormat::DashUnderline);
        format.setUnderlineColor(color);
        item.format = format;
        item.cursor = QTextCursor(block);
        const int length = std::max(1, std::min(block.length() - 1, std::max(0, diagnostic.column - 1)));
        item.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, length);
        selections.push_back(item);
    }
    setExtraSelections(selections);
}

void CodeEditor::findNext() {
    const QString needle = searchField_->text();
    if (needle.isEmpty()) return;
    QTextDocument::FindFlags flags;
    if (searchField_->property("caseSensitive").toBool()) flags |= QTextDocument::FindCaseSensitively;
    auto found = document()->find(needle, textCursor(), flags);
    if (found.isNull()) found = document()->find(needle, 0, flags);
    if (!found.isNull()) setTextCursor(found);
}

void CodeEditor::replaceCurrent() {
    if (searchField_->text().isEmpty()) return;
    if (!textCursor().selectedText().isEmpty() && textCursor().selectedText() == searchField_->text())
        textCursor().insertText(replaceField_->text());
    findNext();
}

void CodeEditor::replaceAll() {
    if (searchField_->text().isEmpty()) return;
    auto cursor = textCursor();
    cursor.beginEditBlock();
    auto found = document()->find(searchField_->text(), 0);
    while (!found.isNull()) {
        found.insertText(replaceField_->text());
        found = document()->find(searchField_->text(), found);
    }
    cursor.endEditBlock();
}

void CodeEditor::emitPosition() {
    auto cursor = textCursor();
    emit cursorPositionChanged(cursor.blockNumber() + 1, cursor.columnNumber());
}

void LineNumberArea::mousePressEvent(QMouseEvent* event) {
    editor_->toggleFold(editor_->cursorForPosition(QPoint(0, event->pos().y())).blockNumber());
}

CompletionPopup::CompletionPopup(CodeEditor* editor) : QListWidget(editor), editor_(editor) {
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setFocusPolicy(Qt::NoFocus);
    setMaximumHeight(220);
    setMinimumWidth(220);
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) emit chosen(item->data(Qt::UserRole).toString());
    });
}

bool CompletionPopup::event(QEvent* event) {
    if (event->type() == QEvent::Show) {
        QPoint bottom = editor_->mapToGlobal(editor_->rect().bottomLeft());
        move(bottom + QPoint(editor_->viewport()->x() + 20, -height()));
    }
    return QListWidget::event(event);
}

} // namespace lxcode::gui
