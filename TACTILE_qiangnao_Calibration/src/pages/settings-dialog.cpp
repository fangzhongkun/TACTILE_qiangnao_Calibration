#include "settings-dialog.h"

#include "combobox.h"
#include "config.h"
#include "frameless/frameless-maker.h"
#include "frameless/titlebar.h"
#include "path-edit.h"
#include "scanner.h"
#include "scrollwidget.h"
#include "version.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setMinimumSize(640, 480);

    const auto titlebar = new TitleBar(this);
    new FramelessMaker(titlebar, this);

    const auto layout = new QVBoxLayout();
    layout->setSpacing(0);
    layout->setContentsMargins({});
    setLayout(layout);

    setWindowTitle(tr("Settings"));
    layout->addWidget(titlebar);

    const auto hbox = new QHBoxLayout();
    layout->addLayout(hbox);

    menu_  = new QListWidget();
    stack_ = new QStackedWidget();
    hbox->addWidget(menu_);
    hbox->addWidget(stack_);

    titlebar->titleLabel()->setFixedWidth(175);
    menu_->setFixedWidth(175);
    menu_->setFocusPolicy(Qt::NoFocus);

    menu_->addItem(tr("General"));
    menu_->addItem(tr("Appearance"));
    menu_->addItem(tr("Data Storage"));
    menu_->addItem(tr("About"));

    stack_->addWidget(GeneralPage());
    stack_->addWidget(AppearancePage());
    stack_->addWidget(RecordPage());
    stack_->addWidget(AboutPage());

    connect(menu_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);

    menu_->setCurrentRow(0);
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    config::save();
    QDialog::closeEvent(event);
}

static QLabel *LABEL(const QString& text, int width)
{
    const auto label = new QLabel(text);
    label->setMinimumWidth(width);
    return label;
}

QWidget *SettingsDialog::GeneralPage()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("General"));

        const auto theme = new ComboBox({
            { "dark", tr("Dark") },
            { "light", tr("Light") },
        });
        theme->select(config::theme);
        theme->onselected([](auto&& value) {
            config::theme = value.toString().toStdString();

            QEvent event{ QEvent::ThemeChange };
            QApplication::sendEvent(qApp, &event);
        });

        form->addRow(LABEL(tr("Theme"), 125), theme);

        const auto lang = new ComboBox({
            { "en_US", "English" },
            { "zh_CN", "中文简体" },
        });
        lang->select(config::language);
        lang->onselected([](auto&& value) { config::language = value.toString(); });
        form->addRow(LABEL(tr("Language"), 125), lang);

        const auto path = new QLineEdit(config::configpath);
        path->setReadOnly(true);
        path->setContextMenuPolicy(Qt::NoContextMenu);
        form->addRow(LABEL(tr("Configuration File"), 125), path);
    }

    page->addSpacer();

    return page;
}

QWidget *SettingsDialog::AppearancePage()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("Layout"));

        const auto toolbar = new QCheckBox();
        toolbar->setChecked(config::layout::visibility::toolbar);
        connect(toolbar, &QCheckBox::toggled,
                [](auto&& checked) { config::layout::visibility::toolbar = checked; });

        form->addRow(LABEL(tr("Toolbar"), 150), toolbar);

        const auto statistics = new QCheckBox();
        statistics->setChecked(config::layout::visibility::statistics_window);
        connect(statistics, &QCheckBox::toggled,
                [](auto&& checked) { config::layout::visibility::statistics_window = checked; });

        form->addRow(tr("Statistics Window"), statistics);

        const auto values = new QCheckBox();
        values->setChecked(config::layout::visibility::values_window);
        connect(values, &QCheckBox::toggled,
                [](auto&& checked) { config::layout::visibility::values_window = checked; });

        form->addRow(tr("Values Window"), values);

        const auto log = new QCheckBox();
        log->setChecked(config::layout::visibility::log_window);
        connect(log, &QCheckBox::toggled,
                [](auto&& checked) { config::layout::visibility::log_window = checked; });

        form->addRow(tr("Logging Window"), log);
    }

    page->addSpacer();

    return page;
}

QWidget *SettingsDialog::RecordPage()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("Image"));

        const auto path = new PathEdit(config::record::image::path);
        connect(path, &PathEdit::changed, [](auto&& dir) { config::record::image::path = dir; });
        form->addRow(LABEL(tr("Save Path"), 125), path);
    }

    {
        const auto form = page->addForm(tr("CSV"));

        const auto path = new PathEdit(config::record::csv::path);
        connect(path, &PathEdit::changed, [](auto&& dir) { config::record::csv::path = dir; });
        form->addRow(LABEL(tr("Save Path"), 125), path);
    }

    page->addSpacer();

    return page;
}

QWidget *SettingsDialog::AboutPage()
{
    const auto page = new QWidget();
    const auto vbox = new QVBoxLayout();

    {
        vbox->addStretch(1);

        // logo
        {
            const auto hbox = new QHBoxLayout();
            vbox->addLayout(hbox);

            hbox->addStretch();

            const auto icon = new QLabel();
            icon->setScaledContents(true);
            icon->setFixedSize(100, 100);
            icon->setPixmap(QPixmap(":/icons/scanner"));
            icon->setAlignment(Qt::AlignCenter);

            hbox->addWidget(icon);

            hbox->addStretch();
        }

        const auto name = new QLabel(tr("Scanner"));
        name->setAlignment(Qt::AlignCenter);
        name->setObjectName("about-name");
        vbox->addWidget(name);

        const auto version = new QLabel("Version : " + QString(SCANNER_VERSION));
        version->setAlignment(Qt::AlignCenter);
        version->setObjectName("about-version");
        vbox->addWidget(version);

        vbox->addStretch(2);
    }
    page->setLayout(vbox);

    return page;
}