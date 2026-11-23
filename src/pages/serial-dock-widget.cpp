#include "serial-dock-widget.h"

#include "config.h"
#include "dock-titlebar.h"
#include "menu.h"
#include "scanner.h"

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <whisper/crc.h>

SerialDockWidget::SerialDockWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : DockWidget(title, parent, flags)
{
    edit_ = new QPlainTextEdit();
    layout_->addWidget(edit_);

    QTextCharFormat fmt{};
    fmt.setFont({ "Agave Nerd Font Mono", 12 });
    edit_->setCurrentCharFormat(fmt);
    edit_->setContextMenuPolicy(Qt::NoContextMenu);
    edit_->setReadOnly(true);
    // A 2 Mbps HEX stream produces very long lines; wrapping every line can stall the UI.
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->document()->setMaximumBlockCount(100);
    edit_->setUndoRedoEnabled(false);

    initContextMenu();
    initToolbar();
    updateIcons();

    timer_ = new QTimer(this);
    timer_->start(50); // ms
    connect(timer_, &QTimer::timeout, this, &SerialDockWidget::consume);

    connect(this, &DockWidget::aliveChanged, [this](auto&& alive) {
        if (!alive) pause_action_->setChecked(true);
    });

    connect(App(), &Scanner::StyleChanged, this, &SerialDockWidget::updateIcons);
}

void SerialDockWidget::append(const char *buf, size_t size)
{
    if (paused_ || !buf || size == 0) return;

    if (channel_filter_.load() >= 0) {
        std::scoped_lock lock(mtx_);
        frame_buffer_.append(buf, static_cast<qsizetype>(size));
        extractFilteredFrames();
        return;
    }

    QByteArray arr(buf, size);
    if (hex_) arr = arr.toHex(' ').toUpper() + ' ';

    {
        std::scoped_lock lock(mtx_);
        buffer_.append(arr);
    }
}

void SerialDockWidget::consume()
{
    if (buffer_.isEmpty()) return;

    QByteArray bq{};

    { // critical
        std::scoped_lock lock(mtx_);
        std::swap(buffer_, bq);
    }

    if (bq.endsWith('\n')) bq.chop(1); // 过滤模式每帧一行，去掉末尾换行避免多出空行
    edit_->appendPlainText(bq);
}

// 调用方需持有 mtx_。按 `3C 3C | ch | flags | len(2) | payload | crc(2) | 3E 3E` 切帧，
// 串口读到的数据块可能在帧中间断开，所以不完整的尾部留在 frame_buffer_ 里等下一块。
void SerialDockWidget::extractFilteredFrames()
{
    constexpr qsizetype kFrameOverhead = 10;   // 帧头2 + 通道1 + 标志1 + 长度2 + CRC2 + 帧尾2
    constexpr int       kMaxPayload    = 1024; // 设备发出的最长帧也远小于此，超过视为假帧头

    const int filter = channel_filter_.load();

    for (;;) {
        const qsizetype head = frame_buffer_.indexOf("<<");
        if (head < 0) {
            // 最后一个字节可能是下一帧帧头的前半个 0x3C，留着
            if (frame_buffer_.size() > 1) frame_buffer_.remove(0, frame_buffer_.size() - 1);
            return;
        }
        if (head > 0) frame_buffer_.remove(0, head);
        if (frame_buffer_.size() < 6) return;

        const auto *p           = reinterpret_cast<const uint8_t *>(frame_buffer_.constData());
        const int   payload_len = p[4] | (p[5] << 8);
        // 设备发出的帧 payload 至少 1 字节；长度 0 的"帧"CRC 恒为 0000，会被数据里的
        // 3C 3C xx xx 00 00 00 00 3E 3E 冒充，直接当假帧头处理。
        if (payload_len == 0 || payload_len > kMaxPayload) {
            frame_buffer_.remove(0, 1); // 假帧头，往后挪一个字节重新找
            continue;
        }

        const qsizetype total = payload_len + kFrameOverhead;
        if (frame_buffer_.size() < total) return;

        // 从一帧中间开始抓（比如刚取消暂停）时，payload 里可能恰好有结构完整的伪帧，
        // 必须和固件收包一样校验 CRC16 才能排除。
        const uint16_t crc = static_cast<uint16_t>(p[6 + payload_len] | (p[7 + payload_len] << 8));
        if (p[total - 2] != 0x3E || p[total - 1] != 0x3E ||
            crc != whisper::crc_16(p + 6, static_cast<size_t>(payload_len))) {
            frame_buffer_.remove(0, 1);
            continue;
        }

        if ((p[2] & 0x0F) == filter) { // 高 4 位是设备地址，按低 4 位的通道号匹配
            const QByteArray frame = frame_buffer_.left(total);
            buffer_.append(hex_ ? frame.toHex(' ').toUpper() : frame);
            buffer_.append('\n');
        }
        frame_buffer_.remove(0, total);
    }
}

void SerialDockWidget::setChannelFilter(int channel, const QString& label)
{
    {
        std::scoped_lock lock(mtx_);
        channel_filter_ = channel;
        frame_buffer_.clear();
    }
    if (filter_button_) filter_button_->setText(label);
}

void SerialDockWidget::initContextMenu()
{
    hex_action_ = new QAction(tr("Hex"));
    hex_action_->setCheckable(true);
    hex_action_->setChecked(hex_);
    connect(hex_action_, &QAction::toggled, [this](auto&& state) { hex_ = state; });

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

    // channel filter
    filter_menu_  = new Menu(QStringLiteral("通道过滤"), this);
    filter_group_ = new QActionGroup(this);
    filter_group_->setExclusive(true);

    const struct
    {
        int     channel;
        QString label;
    } filters[] = {
        { -1, QStringLiteral("全部") },
        { 0x07, QStringLiteral("0x07 标定") },
        { 0x06, QStringLiteral("0x06 控制") },
        { 0x02, QStringLiteral("0x02 数据") },
        { 0x01, QStringLiteral("0x01 属性") },
    };
    for (const auto& f : filters) {
        QAction *action = filter_menu_->addAction(f.label);
        action->setCheckable(true);
        action->setChecked(f.channel < 0);
        filter_group_->addAction(action);
        connect(action, &QAction::triggered,
                [this, channel = f.channel, label = f.label] { setChannelFilter(channel, label); });
    }

    // context menu
    menu_ = new Menu(this);
    menu_->addAction(hex_action_);
    menu_->addSeparator();
    menu_->addAction(copy_action_);
    menu_->addSeparator();
    menu_->addAction(pause_action_);
    menu_->addAction(clear_action_);
    menu_->addSeparator();
    menu_->addMenu(filter_menu_);
}

void SerialDockWidget::initToolbar()
{
    filter_button_ = new QToolButton();
    filter_button_->setText(QStringLiteral("全部"));
    filter_button_->setToolTip(QStringLiteral("通道过滤：只显示通道号匹配的完整帧，每帧一行"));
    filter_button_->setMenu(filter_menu_);
    filter_button_->setPopupMode(QToolButton::InstantPopup);
    titlebar_->addWidget(filter_button_, 0, Qt::AlignRight);

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

void SerialDockWidget::updateIcons()
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

void SerialDockWidget::contextMenuEvent(QContextMenuEvent *event) { menu_->exec(event->globalPos()); }
