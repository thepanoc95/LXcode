#include "gui/mainwindow.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("LXcode");
    QCoreApplication::setApplicationName("LXcode");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("LXcode — an independent, Linux-native Xcode-compatible IDE");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", "A .xcodeproj or .xcworkspace to open", "[project]");
    QCommandLineOption smokeOption("smoke-test",
                                   "Show the main window briefly and exit (used by tests)");
    parser.addOption(smokeOption);
    QCommandLineOption openFileOption("open-file", "Open a source file after startup", "file");
    parser.addOption(openFileOption);
    parser.process(application);

    const auto positional = parser.positionalArguments();
    const QString project = positional.isEmpty() ? QString() : positional.first();

#ifdef LXCODE_RESOURCES_DIR
    const std::string resources = LXCODE_RESOURCES_DIR;
#else
    const std::string resources = "resources";
#endif

    lxcode::gui::MainWindow window(project, resources);
    window.show();
    if (!parser.value(openFileOption).isEmpty())
        QMetaObject::invokeMethod(&window, "openFileFromArgument", Qt::QueuedConnection,
                                  Q_ARG(QString, parser.value(openFileOption)));
    if (parser.isSet(smokeOption))
        QTimer::singleShot(300, &application, &QCoreApplication::quit);
    return application.exec();
}
