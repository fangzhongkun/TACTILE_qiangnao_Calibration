#ifndef SCANNER_SERIAL_DOCK_WIDGET_H
#define SCANNER_SERIAL_DOCK_WIDGET_H

#include "dock-widget.h"

#include <atomic>
#include <mutex>

class QPlainTextEdit;
class QActionGroup;
class QToolButton;
class Menu;
class QTimer;

class SerialDockWidget final : public DockWidget
{
    Q_OBJECT
public:
    explicit SerialDockWidget(const QString& title, QWidget *parent = nullptr,
                              Qt::WindowFlags flags = Qt::WindowFlags());

    void append(const char *, size_t);

public slots:
    void consume();

protected:
    void initContextMenu();
    void initToolbar();

    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void updateIcons();

private:
    void setChannelFilter(int channel, const QString& label);
    void extractFilteredFrames();

    Menu             *menu_{};
    Menu             *filter_menu_{};
    QActionGroup     *filter_group_{};
    QToolButton      *filter_button_{};
    QPointer<QAction> hex_action_{};
    QPointer<QAction> copy_action_{};
    QPointer<QAction> pause_action_{};
    QPointer<QAction> clear_action_{};
    QPointer<QAction> wrap_action_{};
    QPointer<QAction> down_action_{};

    QPlainTextEdit *edit_{};

    bool              paused_{ true };
    std::atomic<bool> hex_{ true };

    // -1 表示不过滤，保持原来的原始字节流显示；否则按帧解析，只显示通道低 4 位匹配的帧。
    std::atomic<int> channel_filter_{ -1 };

    std::mutex mtx_{};
    QByteArray buffer_{};
    QByteArray frame_buffer_{};
    QTimer    *timer_{};
};

#endif //! SCANNER_SERIAL_DOCK_WIDGET_H
