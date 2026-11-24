#include "scanner.h"

#include "config.h"
#include "logging.h"

#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

Scanner::Scanner(int& argc, char **argv) : QApplication(argc, argv)
{
    setWindowIcon(QIcon{ ":/icons/scanner" });

    // fonts

    setStyle(QStyleFactory::create("Fusion"));

    LoadTheme(config::theme);
}

bool Scanner::event(QEvent *event)
{
    if (event->type() == QEvent::ThemeChange) {
        LoadTheme(config::theme);
        return true;
    }

    if (event->type() == QEvent::Quit) {
        config::save();
    }

    return QApplication::event(event);
}

void Scanner::LoadTheme(const std::string& theme)
{
    const std::vector<QString> files{
        ":/stylesheets/fluent",
        ":/stylesheets/fluent-" + QString::fromStdString(theme),
        ":/stylesheets/scanner",
        ":/stylesheets/scanner-" + QString::fromStdString(theme),
    };

    QString style{};
    for (auto& qss : files) {
        QFile file(qss);
        file.open(QFile::ReadOnly);

        if (file.isOpen()) {
            style += file.readAll();
            file.close();
        }
    }
    qApp->setStyleSheet(style);

    QIcon::setThemeName(QString::fromStdString(theme));

    App()->StyleChanged();
}