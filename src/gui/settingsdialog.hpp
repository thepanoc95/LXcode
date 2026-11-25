#pragma once
#include "gui/theme.hpp"
#include "lxcode/config/settings.hpp"

#include <QDialog>
#include <QString>

#include <memory>

namespace Ui {
class SettingsDialog;
}

namespace lxcode::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Settings& settings, const std::string& resourcesDirectory,
                            QWidget* parent = nullptr);
    ~SettingsDialog() override;

    std::string themeName() const;
    int fontSize() const;
    int tabSize() const;
    bool insertSpaces() const;
    std::string debuggerCommand() const;

signals:
    void settingsChanged();

private slots:
    void accept() override;

private:
    void load();

    Settings& settings_;
    std::string resourcesDirectory_;
    std::unique_ptr<Ui::SettingsDialog> ui_;
};

} // namespace lxcode::gui
