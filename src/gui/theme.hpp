#pragma once
#include "lxcode/core/expected.hpp"

#include <QApplication>
#include <QColor>
#include <QTextCharFormat>
#include <QString>

#include <map>
#include <string>
#include <vector>

namespace lxcode::gui {

enum class TokenKind { Plain, Keyword, Type, Function, String, Number, Comment, Preprocessor };

struct EditorColors {
    QColor background;
    QColor foreground;
    QColor lineNumber;
    QColor currentLine;
    QColor selection;
    QColor keyword;
    QColor type;
    QColor function;
    QColor string;
    QColor number;
    QColor comment;
    QColor preprocessor;
    QColor error;
    QColor warning;
};

class Theme {
public:
    static Theme load(const std::string& name, const std::string& resourcesDirectory);
    static std::vector<std::string> available(const std::string& resourcesDirectory);

    std::string name;
    bool dark = true;
    QColor window;
    QColor panel;
    QColor panelAlt;
    QColor border;
    QColor text;
    QColor muted;
    QColor accent;
    QColor selection;
    EditorColors editor;

    QTextCharFormat format(TokenKind kind, int fontSize = 12, bool bold = false) const;
    QString stylesheet() const;
    void apply(QApplication& application) const;
};

} // namespace lxcode::gui
