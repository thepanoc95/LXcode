#include "gui/nvimeditor.hpp"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFile>
#include <QFontMetrics>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace lxcode::gui {

namespace {

// A small completion popup that stays out of the focus order so Neovim keeps
// receiving keystrokes while it is visible.
class NvimCompletionPopup : public QListWidget {
public:
    explicit NvimCompletionPopup(NeovimEditor* editor) : QListWidget(editor), editor_(editor) {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        setFocusPolicy(Qt::NoFocus);
        setMaximumHeight(220);
        setMinimumWidth(220);
    }

    bool event(QEvent* event) override {
        if (event->type() == QEvent::Show) {
            const QPoint bottom = editor_->mapToGlobal(rect().bottomLeft());
            move(bottom + QPoint(20, -height()));
        }
        return QListWidget::event(event);
    }

private:
    NeovimEditor* editor_;
};

QByteArray hexColor(const QColor& color) {
    return QStringLiteral("#%1")
        .arg(static_cast<uint>(color.rgb() & 0xffffffu), 6, 16, QLatin1Char('0'))
        .toUtf8();
}

} // namespace

QString NeovimEditor::findExecutable() {
    const auto fromEnvironment = qgetenv("NVIM");
    if (!fromEnvironment.isEmpty() && QFile::exists(QString::fromLocal8Bit(fromEnvironment)))
        return QString::fromLocal8Bit(fromEnvironment);
    return QStandardPaths::findExecutable("nvim");
}

NeovimEditor::NeovimEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("NeovimEditor");
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    completion_ = new NvimCompletionPopup(this);
    completion_->hide();
    connect(completion_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) insertCompletion(item->text());
    });
    rpc_.setNotificationHandler([this](const QByteArray& method, const MsgPackValue& params) {
        handleNotification(method, params);
    });
    defaultForeground_ = QColor("#d0d0d0");
    defaultBackground_ = QColor("#101014");
}

NeovimEditor::~NeovimEditor() {
    rpc_.stop();
}

bool NeovimEditor::start(const QString& executable, QString* error) {
    QStringList arguments{"--headless", "--embed", "-n"};
    const auto config = qgetenv("LXCODE_NVIM_CONFIG");
    if (!config.isEmpty())
        arguments << "-u" << QString::fromLocal8Bit(config);
    else
        arguments << "--clean";

    if (!rpc_.start(executable, arguments, error)) return false;

    const auto options = MsgPackValue::fromMap({
        {MsgPackValue::fromString("ext_linegrid"), MsgPackValue::fromBool(true)},
        {MsgPackValue::fromString("rgb"), MsgPackValue::fromBool(true)},
    });
    rpc_.call("nvim_ui_attach", MsgPackValue::fromArray({
        MsgPackValue::fromInt(std::max(1, columns_)),
        MsgPackValue::fromInt(std::max(1, rows_)),
        options,
    }));

    execLua(R"LUA(
local group = vim.api.nvim_create_augroup('lxcode_ui', { clear = true })
vim.api.nvim_create_autocmd({ 'CursorMoved', 'CursorMovedI', 'TextChanged', 'TextChangedI' }, {
  group = group,
  callback = function()
    local cursor = vim.api.nvim_win_get_cursor(0)
    vim.rpcnotify(0, 'lxcode_state', vim.bo.modified, cursor[1], cursor[2])
  end,
})
)LUA");
    applyTheme(theme_, fontSize_, tabSize_, insertSpaces_);
    syncViewportSize();
    return true;
}

void NeovimEditor::setFilePath(const std::filesystem::path& path) {
    path_ = path;
    const auto language = languageId(path);
    if (rpc_.isRunning() && !language.isEmpty()) {
        rpc_.call("nvim_buf_set_option", MsgPackValue::fromArray({
            MsgPackValue::fromInt(0), MsgPackValue::fromString("filetype"),
            MsgPackValue::fromString(language.toUtf8()),
        }));
    }
}

QString NeovimEditor::languageId(const std::filesystem::path& path) {
    return QString::fromStdString(lxcode::languageIdForPath(path));
}

bool NeovimEditor::loadFile() {
    if (!rpc_.isRunning()) return false;
    QFile file(QString::fromStdString(path_.string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QByteArray contents = file.readAll();
    trailingNewline_ = contents.endsWith('\n');
    auto lines = QString::fromUtf8(contents).split('\n');
    if (lines.size() > 1 && lines.last().isEmpty()) lines.removeLast();

    rpc_.call("nvim_buf_set_lines", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromInt(0), MsgPackValue::fromInt(-1),
        MsgPackValue::fromBool(false), stringArray(lines),
    }));
    rpc_.call("nvim_buf_set_option", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromString("modified"), MsgPackValue::fromBool(false),
    }));
    const auto language = languageId(path_);
    if (!language.isEmpty()) {
        rpc_.call("nvim_buf_set_option", MsgPackValue::fromArray({
            MsgPackValue::fromInt(0), MsgPackValue::fromString("filetype"),
            MsgPackValue::fromString(language.toUtf8()),
        }));
    }
    rpc_.call("nvim_win_set_cursor", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromArray({MsgPackValue::fromInt(1),
                                                           MsgPackValue::fromInt(0)}),
    }));
    modified_ = false;
    emit modificationChanged(false);
    return true;
}

bool NeovimEditor::saveFile() {
    if (!rpc_.isRunning() || path_.empty()) return false;
    const auto result = rpc_.call("nvim_buf_get_lines", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromInt(0), MsgPackValue::fromInt(-1),
        MsgPackValue::fromBool(false),
    }));
    QStringList lines;
    if (result.isArray()) {
        for (const auto& line : result.array) lines << QString::fromUtf8(line.toByteArray());
    }
    QFile file(QString::fromStdString(path_.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    file.write(lines.join('\n').toUtf8());
    if (trailingNewline_) file.write("\n");
    if (file.error() != QFileDevice::NoError) return false;
    rpc_.call("nvim_buf_set_option", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromString("modified"), MsgPackValue::fromBool(false),
    }));
    modified_ = false;
    emit modificationChanged(false);
    return true;
}

void NeovimEditor::applyTheme(const Theme& theme, int fontSize, int tabSize, bool spaces) {
    theme_ = theme;
    fontSize_ = std::max(6, fontSize);
    tabSize_ = std::max(1, tabSize);
    insertSpaces_ = spaces;

    font_.setFamilies({"JetBrains Mono", "DejaVu Sans Mono", "monospace"});
    font_.setPointSize(fontSize_);
    const QFontMetrics metrics(font_);
    cellWidth_ = std::max(4, metrics.horizontalAdvance(QLatin1Char('M')));
    cellHeight_ = std::max(6, metrics.height());

    if (rpc_.isRunning()) {
        const auto colors = MsgPackValue::fromMap({
            {MsgPackValue::fromString("fg"), MsgPackValue::fromString(hexColor(theme.editor.foreground))},
            {MsgPackValue::fromString("bg"), MsgPackValue::fromString(hexColor(theme.editor.background))},
            {MsgPackValue::fromString("comment"), MsgPackValue::fromString(hexColor(theme.editor.comment))},
            {MsgPackValue::fromString("keyword"), MsgPackValue::fromString(hexColor(theme.editor.keyword))},
            {MsgPackValue::fromString("type"), MsgPackValue::fromString(hexColor(theme.editor.type))},
            {MsgPackValue::fromString("function"), MsgPackValue::fromString(hexColor(theme.editor.function))},
            {MsgPackValue::fromString("string"), MsgPackValue::fromString(hexColor(theme.editor.string))},
            {MsgPackValue::fromString("number"), MsgPackValue::fromString(hexColor(theme.editor.number))},
            {MsgPackValue::fromString("error"), MsgPackValue::fromString(hexColor(theme.editor.error))},
            {MsgPackValue::fromString("warning"), MsgPackValue::fromString(hexColor(theme.editor.warning))},
        });
        execLua(R"LUA(
local c = ...
vim.api.nvim_set_hl(0, 'Normal', { fg = c.fg, bg = c.bg })
vim.api.nvim_set_hl(0, 'Comment', { fg = c.comment, italic = true })
vim.api.nvim_set_hl(0, 'Keyword', { fg = c.keyword, bold = true })
vim.api.nvim_set_hl(0, 'Type', { fg = c.type })
vim.api.nvim_set_hl(0, 'Function', { fg = c.function })
vim.api.nvim_set_hl(0, 'String', { fg = c.string })
vim.api.nvim_set_hl(0, 'Number', { fg = c.number })
vim.api.nvim_set_hl(0, 'DiagnosticError', { fg = c.error, underline = true })
vim.api.nvim_set_hl(0, 'DiagnosticWarn', { fg = c.warning, underline = true })
)LUA", colors);
        // The theme is one argument; set indentation options separately.
        rpc_.call("nvim_set_option_value", MsgPackValue::fromArray({
            MsgPackValue::fromString("tabstop"), MsgPackValue::fromInt(tabSize_),
            MsgPackValue::fromMap({{MsgPackValue::fromString("scope"),
                                    MsgPackValue::fromString("global")}}),
        }));
        rpc_.call("nvim_set_option_value", MsgPackValue::fromArray({
            MsgPackValue::fromString("shiftwidth"), MsgPackValue::fromInt(tabSize_),
            MsgPackValue::fromMap({{MsgPackValue::fromString("scope"),
                                    MsgPackValue::fromString("global")}}),
        }));
        rpc_.call("nvim_set_option_value", MsgPackValue::fromArray({
            MsgPackValue::fromString("expandtab"), MsgPackValue::fromBool(insertSpaces_),
            MsgPackValue::fromMap({{MsgPackValue::fromString("scope"),
                                    MsgPackValue::fromString("global")}}),
        }));
        defaultForeground_ = theme.editor.foreground;
        defaultBackground_ = theme.editor.background;
    }
    update();
    syncViewportSize();
}

void NeovimEditor::setDiagnostics(const std::vector<LspDiagnostic>& diagnostics) {
    if (!rpc_.isRunning()) return;
    std::vector<MsgPackValue> entries;
    for (const auto& diagnostic : diagnostics) {
        const auto severity = diagnostic.severity == "warning" ? "DiagnosticWarn" : "DiagnosticError";
        entries.push_back(MsgPackValue::fromMap({
            {MsgPackValue::fromString("line"), MsgPackValue::fromInt(std::max(0, diagnostic.line - 1))},
            {MsgPackValue::fromString("col"), MsgPackValue::fromInt(std::max(0, diagnostic.column))},
            {MsgPackValue::fromString("text"), MsgPackValue::fromString(diagnostic.message.c_str())},
            {MsgPackValue::fromString("group"), MsgPackValue::fromString(severity)},
        }));
    }
    execLua(R"LUA(
local ns = vim.api.nvim_create_namespace('lxcode_diagnostics')
vim.api.nvim_buf_clear_namespace(0, ns, 0, -1)
for _, d in ipairs(...) do
  vim.api.nvim_buf_set_extmark(0, ns, d.line, d.col, {
    virt_text = { { d.text, d.group } }, virt_text_pos = 'eol',
  })
end
)LUA", MsgPackValue::fromArray(std::move(entries)));
}

void NeovimEditor::setCompletionItems(const std::vector<CompletionItem>& items) {
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

void NeovimEditor::insertCompletion(const QString& text) {
    completion_->hide();
    rpc_.input(text.toUtf8());
}

void NeovimEditor::revealLine(int line, int column) {
    if (!rpc_.isRunning()) return;
    rpc_.call("nvim_win_set_cursor", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0),
        MsgPackValue::fromArray({MsgPackValue::fromInt(std::max(1, line)),
                                 MsgPackValue::fromInt(std::max(0, column - 1))}),
    }));
    rpc_.call("nvim_command", MsgPackValue::fromArray({
        MsgPackValue::fromString("normal! zz"),
    }));
    setFocus();
}

QString NeovimEditor::wordUnderCursor() const {
    if (!rpc_.isRunning()) return {};
    return rpc_.call("nvim_eval", MsgPackValue::fromArray({
        MsgPackValue::fromString("expand('<cword>')"),
    })).toString();
}

QString NeovimEditor::toPlainText() const {
    if (!rpc_.isRunning()) return {};
    const auto result = rpc_.call("nvim_buf_get_lines", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0), MsgPackValue::fromInt(0), MsgPackValue::fromInt(-1),
        MsgPackValue::fromBool(false),
    }));
    QStringList lines;
    if (result.isArray()) {
        for (const auto& line : result.array) lines << QString::fromUtf8(line.toByteArray());
    }
    return lines.join('\n');
}

QPair<int, int> NeovimEditor::cursorPosition() const {
    const auto result = rpc_.call("nvim_win_get_cursor", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0),
    }));
    const auto* row = result.at(0);
    const auto* column = result.at(1);
    return qMakePair(row ? static_cast<int>(row->toInt(1)) : 1,
                     column ? static_cast<int>(column->toInt(0)) : 0);
}

void NeovimEditor::execLua(const QByteArray& code, const MsgPackValue& arguments) {
    if (!rpc_.isRunning()) return;
    const auto supplied = arguments.isNil() ? MsgPackValue::fromArray({}) : arguments;
    rpc_.call("nvim_exec_lua", MsgPackValue::fromArray({
        MsgPackValue::fromString(code), supplied,
    }));
}

MsgPackValue NeovimEditor::stringArray(const QStringList& lines) const {
    std::vector<MsgPackValue> values;
    values.reserve(static_cast<std::size_t>(lines.size()));
    for (const auto& line : lines) values.push_back(MsgPackValue::fromString(line.toUtf8()));
    return MsgPackValue::fromArray(std::move(values));
}

QColor NeovimEditor::colorFrom(const MsgPackValue* value, const QColor& fallback) const {
    if (!value || !value->isInt()) return fallback;
    const auto rgb = value->toInt(-1);
    if (rgb < 0) return fallback;
    return QColor::fromRgb(static_cast<int>(rgb & 0xffffffu));
}

void NeovimEditor::handleNotification(const QByteArray& method, const MsgPackValue& params) {
    if (method == "redraw") {
        if (const auto* events = params.at(0)) handleRedraw(*events);
        return;
    }
    if (method == "lxcode_state") {
        const bool modified = params.at(0) ? params.at(0)->toBool(false) : false;
        const int row = params.at(1) ? static_cast<int>(params.at(1)->toInt(1)) : 1;
        const int column = params.at(2) ? static_cast<int>(params.at(2)->toInt(0)) : 0;
        if (modified != modified_) {
            modified_ = modified;
            emit modificationChanged(modified_);
        }
        if (row != cursorRow_ || column != cursorColumn_) {
            cursorRow_ = row - 1;
            cursorColumn_ = column;
            update();
        }
        emit cursorPositionChanged(row, column);
    }
}

void NeovimEditor::handleRedraw(const MsgPackValue& events) {
    if (!events.isArray()) return;
    for (const auto& event : events.array) {
        if (!event.isArray() || event.array.empty()) continue;
        const auto name = event.array[0].toString();
        const auto* args = event.at(1);
        if (!args || !args->isArray()) continue;

        if (name == "grid_line") {
            applyGridLine(*args);
        } else if (name == "put") {
            applyPut(*args);
        } else if (name == "cursor_goto") {
            const auto row = args->at(0) ? args->at(0)->toInt(0) : 0;
            const auto column = args->at(1) ? args->at(1)->toInt(0) : 0;
            cursorRow_ = static_cast<int>(row);
            cursorColumn_ = static_cast<int>(column);
        } else if (name == "clear") {
            clearGrid();
        } else if (name == "eol_clear") {
            for (int column = cursorColumn_; column < columns_; ++column)
                clearCell(cursorRow_, column);
        } else if (name == "resize") {
            const auto columns = args->at(0) ? args->at(0)->toInt(columns_) : columns_;
            const auto rows = args->at(1) ? args->at(1)->toInt(rows_) : rows_;
            resizeGrid(static_cast<int>(columns), static_cast<int>(rows));
        } else if (name == "scroll") {
            scrollRegion(static_cast<int>(args->at(0) ? args->at(0)->toInt(1) : 1));
        } else if (name == "set_scroll_region") {
            scrollTop_ = static_cast<int>(args->at(0) ? args->at(0)->toInt(0) : 0);
            scrollBottom_ = static_cast<int>(args->at(1) ? args->at(1)->toInt(rows_ - 1) : rows_ - 1);
            scrollLeft_ = static_cast<int>(args->at(2) ? args->at(2)->toInt(0) : 0);
            scrollRight_ = static_cast<int>(args->at(3) ? args->at(3)->toInt(columns_ - 1) : columns_ - 1);
        } else if (name == "default_colors_set") {
            defaultForeground_ = colorFrom(args->at(0), defaultForeground_);
            defaultBackground_ = colorFrom(args->at(1), defaultBackground_);
            clearGrid();
        } else if (name == "hl_attr_define") {
            const auto id = args->at(0) ? args->at(0)->toInt(-1) : -1;
            if (id >= 0) {
                if (static_cast<std::size_t>(id) >= highlights_.size())
                    highlights_.resize(static_cast<std::size_t>(id) + 1);
                highlights_[static_cast<std::size_t>(id)] = args->at(1) ? *args->at(1) : MsgPackValue::nil();
            }
        } else if (name == "set_hl") {
            const auto id = args->at(1) ? args->at(1)->toInt(-1) : -1;
            if (id >= 0) {
                if (static_cast<std::size_t>(id) >= highlights_.size())
                    highlights_.resize(static_cast<std::size_t>(id) + 1);
                highlights_[static_cast<std::size_t>(id)] = args->at(2) ? *args->at(2) : MsgPackValue::nil();
            }
        } else if (name == "highlight_set") {
            const auto attrs = args->at(0) ? *args->at(0) : MsgPackValue::nil();
            highlights_.push_back(attrs);
        } else if (name == "flush") {
            update();
        }
    }
}

void NeovimEditor::applyGridLine(const MsgPackValue& arguments) {
    const auto row = arguments.at(1) ? static_cast<int>(arguments.at(1)->toInt(-1)) : -1;
    const auto startColumn = arguments.at(2) ? static_cast<int>(arguments.at(2)->toInt(0)) : 0;
    const auto* cells = arguments.at(3);
    if (row < 0 || !cells || !cells->isArray()) return;
    resizeGrid(columns_, std::max(rows_, row + 1));
    int column = startColumn;
    for (const auto& cell : cells->array) {
        if (!cell.isArray() || cell.array.empty()) continue;
        const auto text = cell.array[0].toString();
        const auto highlight = cell.at(1) ? static_cast<int>(cell.at(1)->toInt(0)) : 0;
        const auto repeat = cell.at(2) ? static_cast<int>(cell.at(2)->toInt(1)) : 1;
        const auto attrs = highlight >= 0 && static_cast<std::size_t>(highlight) < highlights_.size()
                               ? highlights_[static_cast<std::size_t>(highlight)] : MsgPackValue::nil();
        const auto foreground = colorFrom(attrs.get("foreground"), defaultForeground_);
        const auto background = colorFrom(attrs.get("background"), defaultBackground_);
        const auto bold = attrs.get("bold") ? attrs.get("bold")->toBool(false) : false;
        const auto italic = attrs.get("italic") ? attrs.get("italic")->toBool(false) : false;
        const auto underline = attrs.get("underline") ? attrs.get("underline")->toBool(false) : false;
        for (int i = 0; i < std::max(1, repeat) && column < columns_; ++i, ++column) {
            grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                Cell{text, foreground, background, bold, italic, underline};
        }
    }
}

void NeovimEditor::applyPut(const MsgPackValue& arguments) {
    const auto row = arguments.at(0) ? static_cast<int>(arguments.at(0)->toInt(-1)) : -1;
    const auto startColumn = arguments.at(1) ? static_cast<int>(arguments.at(1)->toInt(0)) : 0;
    const auto* cells = arguments.at(2);
    if (row < 0 || !cells || !cells->isArray()) return;
    int column = startColumn;
    for (const auto& cell : cells->array) {
        if (!cell.isArray() || cell.array.empty()) continue;
        const auto text = cell.array[0].toString();
        const auto highlight = cell.at(1) ? static_cast<int>(cell.at(1)->toInt(0)) : 0;
        const auto repeat = cell.at(2) ? static_cast<int>(cell.at(2)->toInt(1)) : 1;
        const auto attrs = highlight >= 0 && static_cast<std::size_t>(highlight) < highlights_.size()
                               ? highlights_[static_cast<std::size_t>(highlight)] : MsgPackValue::nil();
        for (int i = 0; i < std::max(1, repeat) && column < columns_; ++i, ++column) {
            grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                Cell{text, colorFrom(attrs.get("foreground"), defaultForeground_),
                     colorFrom(attrs.get("background"), defaultBackground_), false, false, false};
        }
    }
}

void NeovimEditor::clearCell(int row, int column) {
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_) return;
    grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
        Cell{QString(), defaultForeground_, defaultBackground_, false, false, false};
}

void NeovimEditor::clearGrid() {
    for (int row = 0; row < rows_; ++row)
        for (int column = 0; column < columns_; ++column) clearCell(row, column);
}

void NeovimEditor::resizeGrid(int columns, int rows) {
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == columns_ && rows == rows_ && !grid_.empty()) return;
    if (columns == columns_ && rows == rows_ && grid_.size() == static_cast<std::size_t>(rows)) return;
    columns_ = columns;
    rows_ = rows;
    grid_.assign(static_cast<std::size_t>(rows_),
                 std::vector<Cell>(static_cast<std::size_t>(columns_)));
    for (int row = 0; row < rows_; ++row)
        for (int column = 0; column < columns_; ++column) clearCell(row, column);
}

void NeovimEditor::scrollRegion(int count) {
    const auto top = std::clamp(scrollTop_, 0, rows_ - 1);
    const auto bottom = std::clamp(scrollBottom_, top, rows_ - 1);
    const auto left = std::clamp(scrollLeft_, 0, columns_ - 1);
    const auto right = std::clamp(scrollRight_, left, columns_ - 1);
    if (top == bottom || count == 0) return;
    const auto span = bottom - top;
    if (std::abs(count) > span) {
        for (int row = top; row <= bottom; ++row)
            for (int column = left; column <= right; ++column) clearCell(row, column);
        return;
    }
    auto snapshot = grid_;
    for (int row = top; row <= bottom; ++row) {
        const int source = row - count;
        for (int column = left; column <= right; ++column) {
            if (source >= top && source <= bottom)
                grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                    snapshot[static_cast<std::size_t>(source)][static_cast<std::size_t>(column)];
            else
                clearCell(row, column);
        }
    }
}

void NeovimEditor::syncViewportSize() {
    if (!rpc_.isRunning() || cellWidth_ <= 0 || cellHeight_ <= 0) return;
    const auto columns = std::max(1, width() / cellWidth_);
    const auto rows = std::max(1, height() / cellHeight_);
    if (columns != columns_ || rows != rows_) {
        rpc_.call("nvim_ui_try_resize", MsgPackValue::fromArray({
            MsgPackValue::fromInt(columns), MsgPackValue::fromInt(rows),
        }));
    }
}

QString NeovimEditor::keySequence(QKeyEvent* event) const {
    const auto key = event->key();
    const auto modifiers = event->modifiers();
    QString base;
    switch (key) {
    case Qt::Key_Escape: base = "<Esc>"; break;
    case Qt::Key_Return:
    case Qt::Key_Enter: base = "<CR>"; break;
    case Qt::Key_Tab: base = "<Tab>"; break;
    case Qt::Key_Backspace: base = "<BS>"; break;
    case Qt::Key_Delete: base = "<Del>"; break;
    case Qt::Key_Left: base = "<Left>"; break;
    case Qt::Key_Right: base = "<Right>"; break;
    case Qt::Key_Up: base = "<Up>"; break;
    case Qt::Key_Down: base = "<Down>"; break;
    case Qt::Key_Home: base = "<Home>"; break;
    case Qt::Key_End: base = "<End>"; break;
    case Qt::Key_PageUp: base = "<PageUp>"; break;
    case Qt::Key_PageDown: base = "<PageDown>"; break;
    case Qt::Key_Insert: base = "<Insert>"; break;
    case Qt::Key_Space: base = " "; break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
            base = QString("<F%1>").arg(key - Qt::Key_F1 + 1);
        else if (modifiers & Qt::ControlModifier && key >= Qt::Key_A && key <= Qt::Key_Z)
            base = QString("<C-%1>").arg(QLatin1Char(key).toLower());
        else if (modifiers & Qt::AltModifier && key >= Qt::Key_A && key <= Qt::Key_Z)
            base = QString("<M-%1>").arg(QLatin1Char(key).toLower());
        else if (!event->text().isEmpty())
            base = event->text();
        else
            return {};
        break;
    }
    if ((modifiers & Qt::ShiftModifier) && base.size() == 1 && base[0].isLetter())
        return QString("<S-%1>").arg(base);
    return base;
}

void NeovimEditor::keyPressEvent(QKeyEvent* event) {
    if (completion_->isVisible()) {
        if (event->key() == Qt::Key_Down) {
            completion_->setCurrentRow(completion_->currentRow() + 1);
            return;
        }
        if (event->key() == Qt::Key_Up) {
            completion_->setCurrentRow(completion_->currentRow() - 1);
            return;
        }
        if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Tab) {
            auto* item = completion_->currentItem();
            if (item) insertCompletion(item->data(Qt::UserRole).toString());
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            completion_->hide();
            return;
        }
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space) {
        const auto position = cursorPosition();
        emit completionRequested(position.first, position.second);
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_BracketRight) {
        const auto position = cursorPosition();
        emit definitionRequested(position.first, position.second);
        return;
    }
    const auto sequence = keySequence(event);
    if (!sequence.isEmpty()) rpc_.input(sequence.toUtf8());
}

void NeovimEditor::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), defaultBackground_);
    painter.setFont(font_);
    const int ascent = QFontMetrics(font_).ascent();
    for (int row = 0; row < rows_; ++row) {
        for (int column = 0; column < columns_; ++column) {
            const auto& cell = grid_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
            const QRect target(column * cellWidth_, row * cellHeight_, cellWidth_, cellHeight_);
            if (!target.intersects(rect())) continue;
            if (cell.background.isValid()) painter.fillRect(target, cell.background);
            if (cell.text.isEmpty()) continue;
            auto cellFont = font_;
            cellFont.setBold(cell.bold);
            cellFont.setItalic(cell.italic);
            cellFont.setUnderline(cell.underline);
            painter.setFont(cellFont);
            painter.setPen(cell.foreground);
            painter.drawText(target.left(), target.top() + ascent, cell.text);
        }
    }
    if (cursorRow_ >= 0 && cursorRow_ < rows_ && cursorColumn_ >= 0 && cursorColumn_ < columns_) {
        const QRect target(cursorColumn_ * cellWidth_, cursorRow_ * cellHeight_,
                           cellWidth_, cellHeight_);
        painter.fillRect(target, QColor(90, 120, 180, 90));
        painter.setPen(QColor(140, 180, 255));
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }
}

void NeovimEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncViewportSize();
}

void NeovimEditor::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    setAttribute(Qt::WA_OpaquePaintEvent);
    update();
}

void NeovimEditor::mousePressEvent(QMouseEvent* event) {
    if (!rpc_.isRunning()) return;
    const int row = std::clamp(event->pos().y() / cellHeight_, 0, rows_ - 1);
    const int column = std::clamp(event->pos().x() / cellWidth_, 0, columns_ - 1);
    rpc_.call("nvim_win_set_cursor", MsgPackValue::fromArray({
        MsgPackValue::fromInt(0),
        MsgPackValue::fromArray({MsgPackValue::fromInt(row + 1), MsgPackValue::fromInt(column)}),
    }));
    setFocus();
}

} // namespace lxcode::gui
