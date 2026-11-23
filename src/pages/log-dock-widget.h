#ifndef SCANNER_LOG_DOCK_WIDGET_H
#define SCANNER_LOG_DOCK_WIDGET_H

#include "dock-widget.h"

#include <glog/logging.h>
#include <mutex>

class QPlainTextEdit;
class Menu;
class QTimer;

class LogDockWidget final : public DockWidget, public google::LogSink
{
    Q_OBJECT
public:
    explicit LogDockWidget(const QString& title, QWidget *parent = nullptr,
                           Qt::WindowFlags flags = Qt::WindowFlags());
    ~LogDockWidget() override;

    void send(google::LogSeverity severity, const char *path, const char *filename, int line,
              const google::LogMessageTime& ts, const char *message, size_t len) override;

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
    QPointer<QAction> copy_action_{};
    QPointer<QAction> pause_action_{};
    QPointer<QAction> clear_action_{};
    QPointer<QAction> wrap_action_{};
    QPointer<QAction> down_action_{};

    QPlainTextEdit *edit_{};

    bool paused_{ false };

    std::mutex mtx_{};
    QByteArray buffer_{};
    QTimer    *timer_{};
};

#endif //! SCANNER_LOG_DOCK_WIDGET_H