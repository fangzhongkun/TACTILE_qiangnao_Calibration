#include "log-dock-widget.h"

#include "config.h"
#include "dock-titlebar.h"
#include "logging.h"
#include "menu.h"
#include "scanner.h"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <probe/thread.h>
#include <QContextMenuEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

LogDockWidget::LogDockWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : DockWidget(title, parent, flags)
{
    edit_ = new QPlainTextEdit();
    layout_->addWidget(edit_);

    QTextCharFormat fmt{};
    fmt.setFont({ "Agave Nerd Font Mono", 12 });
    edit_->setCurrentCharFormat(fmt);
    edit_->setContextMenuPolicy(Qt::NoContextMenu);
    edit_->setReadOnly(true);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->document()->setMaximumBlockCount(1000);
    edit_->setUndoRedoEnabled(false);

    initContextMenu();
    initToolbar();
    updateIcons();

    google::AddLogSink(this);

    timer_ = new QTimer(this);
    timer_->start(50); // ms
    connect(timer_, &QTimer::timeout, this, &LogDockWidget::consume);

    connect(App(), &Scanner::StyleChanged, this, &LogDockWidget::updateIcons);
}

LogDockWidget::~LogDockWidget() { google::RemoveLogSink(this); }

static const char *Level(int severity)
{
    switch (severity) {
    case 1:  return "WARNING";
    case 2:  return "ERROR";
    case 3:  return "FATAL";
    default: return "INFO";
    }
}

void LogDockWidget::send(google::LogSeverity           severity, const char *, const char *, int,
                         const google::LogMessageTime& ts, const char *message, size_t len)
{
    if (paused_) return;

    const auto repr =
        fmt::format("{:%F %T} {:>7} [{:>16}] ", ts.tm(), Level(severity), probe::thread::name());

    QByteArray bytes(repr.c_str(), repr.size());
    bytes.append(message, len);

    {
        std::scoped_lock lock(mtx_);
        buffer_.push_back(bytes + '\n');
    }
}

void LogDockWidget::consume()
{
    if (buffer_.isEmpty()) return;

    QByteArray buffer{};

    { // critical
        std::scoped_lock lock(mtx_);
        std::swap(buffer, buffer_);
    }

    buffer.chop(1); // remove the last '\n'
    edit_->appendPlainText(buffer);
}

void LogDockWidget::initContextMenu()
{
    // copy
    copy_action_ = new QAction(tr("Copy"));
    copy_action_->setEnabled(false);
    connect(edit_, &QPlainTextEdit::copyAvailable, copy_action_, &QAction::setEnabled);

    // pause
    pause_action_ = new QAction(tr("Pause"));
    pause_action_->setCheckable(true);
    connect(pause_action_, &QAction::toggled, [this](auto&& paused) { paused_ = paused; });

    // wrap
    wrap_action_ = new QAction(tr("Soft Wrap"));
    wrap_action_->setCheckable(true);
    connect(wrap_action_, &QAction::toggled, [this](auto&& state) {
        edit_->setLineWrapMode(!state ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    });

    // scroll down
    down_action_ = new QAction(QIcon::fromTheme("scroll-down"), tr("Soft Wrap"));
    connect(down_action_, &QAction::triggered,
            [this] { edit_->verticalScrollBar()->setValue(edit_->verticalScrollBar()->maximum()); });

    // clear
    clear_action_ = new QAction(QIcon::fromTheme("delete"), tr("Clear All"));
    connect(clear_action_, &QAction::triggered, edit_, &QPlainTextEdit::clear);

    // context menu
    menu_ = new Menu(this);
    menu_->addAction(copy_action_);
    menu_->addSeparator();
    menu_->addAction(pause_action_);
    menu_->addAction(clear_action_);
}

void LogDockWidget::initToolbar()
{
    const auto pause = new QToolButton();
    pause->setDefaultAction(pause_action_);

    const auto clear = new QToolButton();
    clear->setDefaultAction(clear_action_);

    const auto wrap = new QToolButton();
    wrap->setDefaultAction(wrap_action_);

    const auto down = new QToolButton();
    down->setDefaultAction(down_action_);

    titlebar_->addWidget(pause, 0, Qt::AlignRight);
    titlebar_->addWidget(wrap, 0, Qt::AlignRight);
    titlebar_->addWidget(down, 0, Qt::AlignRight);
    titlebar_->addWidget(clear, 0, Qt::AlignRight);
}

void LogDockWidget::updateIcons()
{
    const QString theme = config::theme == "dark" ? "light" : "dark";

    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/copy-" + theme), QIcon::Normal, QIcon::On);
        icon.addPixmap(QPixmap(":/icons/copy-" + QString::fromStdString(config::theme)), QIcon::Disabled,
                       QIcon::On);
        copy_action_->setIcon(icon);
    }
    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/pause-" + theme), QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(":/icons/play-" + theme), QIcon::Normal, QIcon::On);
        pause_action_->setIcon(icon);
    }
    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/soft-unwrap-" + theme), QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(":/icons/soft-wrap-" + theme), QIcon::Normal, QIcon::On);
        wrap_action_->setIcon(icon);
    }
}

void LogDockWidget::contextMenuEvent(QContextMenuEvent *event) { menu_->exec(event->globalPos()); }
