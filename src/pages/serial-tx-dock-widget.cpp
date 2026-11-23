#include "serial-tx-dock-widget.h"

#include "config.h"
#include "dock-titlebar.h"
#include "menu.h"
#include "scanner.h"

#include <QContextMenuEvent>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

SerialTxDockWidget::SerialTxDockWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : DockWidget(title, parent, flags)
{
    edit_        = new QPlainTextEdit();
    payload_box_ = new QPlainTextEdit();

    layout_->addWidget(edit_);
    payload_box_->setMaximumHeight(100);
    payload_box_->setObjectName("payload-sending-box");
    layout_->addWidget(payload_box_);

    {
        const auto hbox = new QHBoxLayout();
        hbox->setContentsMargins(5, 5, 5, 5);

        hbox->addWidget(new QLabel("CH"));
        hbox->addSpacing(3);

        channel_box_ = new QSpinBox();
        auto font    = channel_box_->font();
        font.setCapitalization(QFont::AllUppercase);
        channel_box_->setFont(font);
        channel_box_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        channel_box_->setDisplayIntegerBase(16);
        channel_box_->setFixedWidth(60);
        channel_box_->setRange(0, 0xFF);

        hbox->addWidget(channel_box_);
        hbox->addStretch();

        const auto send_btn = new QPushButton();
        send_btn->setObjectName("serial-tx-btn");
        send_btn->setIcon(QIcon::fromTheme("send"));
        hbox->addWidget(send_btn);

        layout_->addLayout(hbox);

        connect(send_btn, &QPushButton::clicked, this, [this]() {
            if (hex_) {
                onsend(channel_box_->value(),
                       whisper::payload{
                           QByteArray::fromHex(payload_box_->toPlainText().toUtf8()).toStdString() });
            }
            else {
                onsend(channel_box_->value(),
                       whisper::payload{ payload_box_->toPlainText().toStdString() });
            }
        });
    }

    QTextCharFormat fmt{};
    fmt.setFont({ "Agave Nerd Font Mono", 12 });

    edit_->setCurrentCharFormat(fmt);
    edit_->setContextMenuPolicy(Qt::NoContextMenu);
    edit_->setReadOnly(true);
    edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit_->document()->setMaximumBlockCount(300);
    edit_->setUndoRedoEnabled(false);

    payload_box_->setCurrentCharFormat(fmt);
    payload_box_->setContextMenuPolicy(Qt::NoContextMenu);
    payload_box_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    payload_box_->document()->setMaximumBlockCount(300);

    initContextMenu();
    initToolbar();
    updateIcons();

    timer_ = new QTimer(this);
    timer_->start(50); // ms
    connect(timer_, &QTimer::timeout, this, &SerialTxDockWidget::consume);

    connect(App(), &Scanner::StyleChanged, this, &SerialTxDockWidget::updateIcons);
}

void SerialTxDockWidget::append(const char *buf, size_t size)
{
    if (paused_ || !alive() || !buf || size == 0) return;

    QByteArray arr(buf, size);
    if (hex_) arr = arr.toHex(' ').toUpper() + ' ';

    {
        std::scoped_lock lock(mtx_);
        buffer_.append(arr);
    }
}

void SerialTxDockWidget::consume()
{
    if (buffer_.isEmpty()) return;

    QByteArray bq{};

    { // critical
        std::scoped_lock lock(mtx_);
        std::swap(buffer_, bq);
    }

    edit_->appendPlainText(bq);
}

void SerialTxDockWidget::initContextMenu()
{
    hex_action_ = new QAction(tr("Hex"));
    hex_action_->setCheckable(true);
    hex_action_->setChecked(hex_);
    connect(hex_action_, &QAction::toggled, [this](auto&& state) {
        hex_ = state;

        if (hex_) {
            payload_box_->setPlainText(
                QByteArray::fromStdString(payload_box_->toPlainText().toStdString()).toHex(' ').toUpper());
        }
        else {
            payload_box_->setPlainText(QByteArray::fromHex(payload_box_->toPlainText().toUtf8()));
        }
    });

    // copy
    copy_action_ = new QAction(tr("Copy"));
    copy_action_->setEnabled(false);
    connect(edit_, &QPlainTextEdit::copyAvailable, copy_action_, &QAction::setEnabled);

    // pause
    pause_action_ = new QAction(tr("Pause"));
    pause_action_->setCheckable(true);
    pause_action_->setChecked(paused_);
    connect(pause_action_, &QAction::toggled, [this](auto&& paused) { paused_ = paused; });

    // wrap
    wrap_action_ = new QAction(tr("Soft Wrap"));
    wrap_action_->setCheckable(true);
    wrap_action_->setChecked(true);
    connect(wrap_action_, &QAction::toggled, [this](auto&& state) {
        edit_->setLineWrapMode(!state ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    });

    // scroll down
    down_action_ = new QAction(QIcon::fromTheme("scroll-down"), tr("Soft Wrap"));
    connect(down_action_, &QAction::triggered, [this] { edit_->moveCursor(QTextCursor::End); });

    // clear
    clear_action_ = new QAction(QIcon::fromTheme("delete"), tr("Clear All"));
    connect(clear_action_, &QAction::triggered, edit_, &QPlainTextEdit::clear);

    // context menu
    menu_ = new Menu(this);
    menu_->addAction(hex_action_);
    menu_->addSeparator();
    menu_->addAction(copy_action_);
    menu_->addSeparator();
    menu_->addAction(pause_action_);
    menu_->addAction(clear_action_);
}

void SerialTxDockWidget::initToolbar()
{
    const auto hex = new QToolButton();
    hex->setDefaultAction(hex_action_);

    const auto pause = new QToolButton();
    pause->setDefaultAction(pause_action_);

    const auto clear = new QToolButton();
    clear->setDefaultAction(clear_action_);

    const auto wrap = new QToolButton();
    wrap->setDefaultAction(wrap_action_);

    const auto down = new QToolButton();
    down->setDefaultAction(down_action_);

    titlebar_->addWidget(hex, 0, Qt::AlignRight);
    titlebar_->addWidget(pause, 0, Qt::AlignRight);
    titlebar_->addWidget(wrap, 0, Qt::AlignRight);
    titlebar_->addWidget(down, 0, Qt::AlignRight);
    titlebar_->addWidget(clear, 0, Qt::AlignRight);
}

void SerialTxDockWidget::updateIcons()
{
    const QString theme = config::theme == "dark" ? "light" : "dark";
    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/hex-" + theme), QIcon::Normal, QIcon::On);
        icon.addPixmap(QPixmap(":/icons/ascii-" + theme), QIcon::Normal, QIcon::Off);
        hex_action_->setIcon(icon);
    }
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

void SerialTxDockWidget::contextMenuEvent(QContextMenuEvent *event) { menu_->exec(event->globalPos()); }
