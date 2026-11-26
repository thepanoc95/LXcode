#include "gui/settingsdialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace Ui {

class SettingsDialog {
public:
    QComboBox* theme = nullptr;
    QSpinBox* fontSize = nullptr;
    QSpinBox* tabSize = nullptr;
    QCheckBox* insertSpaces = nullptr;
    QLineEdit* nvimPath = nullptr;
    QLineEdit* toolchainRoots = nullptr;
    QLineEdit* toolchainName = nullptr;
    QLineEdit* cLanguageServer = nullptr;
    QLineEdit* cppLanguageServer = nullptr;
    QLineEdit* swiftLanguageServer = nullptr;
    QLineEdit* debuggerCommand = nullptr;
};

} // namespace Ui

namespace lxcode::gui {

SettingsDialog::SettingsDialog(Settings& settings, const std::string& resourcesDirectory,
                               QWidget* parent)
    : QDialog(parent), settings_(settings), resourcesDirectory_(resourcesDirectory),
      ui_(std::make_unique<Ui::SettingsDialog>()) {
    setWindowTitle(tr("LXcode Settings"));
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    ui_->theme = new QComboBox(this);
    for (const auto& name : Theme::available(resourcesDirectory_))
        ui_->theme->addItem(QString::fromStdString(name));
    form->addRow(tr("Theme"), ui_->theme);

    ui_->fontSize = new QSpinBox(this);
    ui_->fontSize->setRange(8, 32);
    form->addRow(tr("Editor font size"), ui_->fontSize);

    ui_->tabSize = new QSpinBox(this);
    ui_->tabSize->setRange(1, 16);
    form->addRow(tr("Tab width"), ui_->tabSize);

    ui_->insertSpaces = new QCheckBox(tr("Insert spaces when pressing Tab"), this);
    form->addRow(QString(), ui_->insertSpaces);

    ui_->nvimPath = new QLineEdit(this);
    ui_->nvimPath->setPlaceholderText(tr("Leave blank to use nvim from PATH or NVIM"));
    form->addRow(tr("Neovim executable"), ui_->nvimPath);

    ui_->toolchainRoots = new QLineEdit(this);
    ui_->toolchainRoots->setPlaceholderText(tr("Colon-separated OSXCross roots"));
    form->addRow(tr("OSXCross roots"), ui_->toolchainRoots);

    ui_->toolchainName = new QLineEdit(this);
    ui_->toolchainName->setPlaceholderText(tr("Leave blank for automatic selection"));
    form->addRow(tr("Toolchain"), ui_->toolchainName);

    ui_->cLanguageServer = new QLineEdit(this);
    ui_->cLanguageServer->setPlaceholderText(tr("e.g. clangd --background-index"));
    form->addRow(tr("C language server"), ui_->cLanguageServer);

    ui_->cppLanguageServer = new QLineEdit(this);
    ui_->cppLanguageServer->setPlaceholderText(tr("e.g. clangd --background-index"));
    form->addRow(tr("C++ language server"), ui_->cppLanguageServer);

    ui_->swiftLanguageServer = new QLineEdit(this);
    form->addRow(tr("Swift language server"), ui_->swiftLanguageServer);

    ui_->debuggerCommand = new QLineEdit(this);
    ui_->debuggerCommand->setPlaceholderText(tr("e.g. lldb-dap"));
    form->addRow(tr("Debug adapter"), ui_->debuggerCommand);

    layout->addLayout(form);
    auto* note = new QLabel(
        tr("Settings are stored in $XDG_CONFIG_HOME/LXcode/settings.json. The editor requires Neovim; language servers and debug adapters are external tools."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* save = new QPushButton(tr("Save"), this);
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    load();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::load() {
    const auto theme = settings_.string("theme", "dark");
    const int index = ui_->theme->findText(QString::fromStdString(theme));
    ui_->theme->setCurrentIndex(std::max(0, index));
    ui_->fontSize->setValue(settings_.integer("editorFontSize", 12));
    ui_->tabSize->setValue(settings_.integer("editorTabSize", 4));
    ui_->insertSpaces->setChecked(settings_.boolean("editorInsertSpaces", true));
    ui_->nvimPath->setText(QString::fromStdString(settings_.string("editorNvimPath")));

    QStringList roots;
    for (const auto& root : settings_.stringList("toolchainRoots"))
        roots.push_back(QString::fromStdString(root));
    ui_->toolchainRoots->setText(roots.join(':'));
    ui_->toolchainName->setText(QString::fromStdString(settings_.string("toolchain")));
    ui_->cLanguageServer->setText(
        QString::fromStdString(settings_.languageServerCommand("c")));
    ui_->cppLanguageServer->setText(
        QString::fromStdString(settings_.languageServerCommand("cpp")));
    ui_->swiftLanguageServer->setText(
        QString::fromStdString(settings_.languageServerCommand("swift")));
    ui_->debuggerCommand->setText(QString::fromStdString(settings_.string("debugAdapter")));
}

std::string SettingsDialog::themeName() const {
    return ui_->theme->currentText().toStdString();
}

int SettingsDialog::fontSize() const { return ui_->fontSize->value(); }

int SettingsDialog::tabSize() const { return ui_->tabSize->value(); }

bool SettingsDialog::insertSpaces() const { return ui_->insertSpaces->isChecked(); }

std::string SettingsDialog::debuggerCommand() const {
    return ui_->debuggerCommand->text().trimmed().toStdString();
}

void SettingsDialog::accept() {
    settings_.set("theme", themeName());
    settings_.set("editorFontSize", fontSize());
    settings_.set("editorTabSize", tabSize());
    settings_.set("editorInsertSpaces", insertSpaces());
    settings_.set("editorNvimPath", ui_->nvimPath->text().trimmed().toStdString());

    std::vector<std::string> roots;
    for (const auto& item : ui_->toolchainRoots->text().split(':', Qt::SkipEmptyParts))
        roots.push_back(item.trimmed().toStdString());
    settings_.set("toolchainRoots", roots);
    settings_.set("toolchain", ui_->toolchainName->text().trimmed().toStdString());
    settings_.setLanguageServerCommand("c", ui_->cLanguageServer->text().trimmed().toStdString());
    settings_.setLanguageServerCommand("cpp", ui_->cppLanguageServer->text().trimmed().toStdString());
    settings_.setLanguageServerCommand("swift", ui_->swiftLanguageServer->text().trimmed().toStdString());
    settings_.set("debugAdapter", debuggerCommand());
    settings_.save();
    emit settingsChanged();
    QDialog::accept();
}

} // namespace lxcode::gui
