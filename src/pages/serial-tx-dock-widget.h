#ifndef SCANNER_SERIAL_TX_DOCK_WIDGET_H
#define SCANNER_SERIAL_TX_DOCK_WIDGET_H

#include "dock-widget.h"

#include <functional>
#include <mutex>
#include <whisper/payload.h>

class QPlainTextEdit;
class Menu;
class QTimer;
class QLineEdit;
class QSpinBox;

class SerialTxDockWidget final : public DockWidget
{
    Q_OBJECT
public:
    explicit SerialTxDockWidget(const QString& title, QWidget *parent = nullptr,
                                Qt::WindowFlags flags = Qt::WindowFlags());

    void append(const char *, size_t);

    std::function<void(uint8_t, const whisper::payload&)> onsend = [](auto, const auto&) {};

public slots:
    void consume();

protected:
    void initContextMenu();
    void initToolbar();

    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void updateIcons();

private:
    Menu             *menu_{};
    QPointer<QAction> hex_action_{};
    QPointer<QAction> copy_action_{};
    QPointer<QAction> pause_action_{};
    QPointer<QAction> clear_action_{};
    QPointer<QAction> wrap_action_{};
    QPointer<QAction> down_action_{};

    QPlainTextEdit *edit_{};
    QSpinBox       *channel_box_{};
    QPlainTextEdit *payload_box_{};

    bool              paused_{ false };
    std::atomic<bool> hex_{ true };

    std::mutex mtx_{};
    QByteArray buffer_{};
    QTimer    *timer_{};
};

#endif //! SCANNER_SERIAL_TX_DOCK_WIDGET_H