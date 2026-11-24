#ifndef SCANNER_STATISTICS_DOCK_WIDGET_H
#define SCANNER_STATISTICS_DOCK_WIDGET_H

#include "chart-view.h"
#include "dock-widget.h"
#include "libtouch/matrix.h"

class Menu;
class QTimer;

constexpr inline int CHANNELS = 3;

class StatisticsDockWidget final : public DockWidget
{
    Q_OBJECT
public:
    explicit StatisticsDockWidget(const QString& title, QWidget *parent = nullptr,
                                  Qt::WindowFlags flags = Qt::WindowFlags());

    void setYAxisRange(qreal, qreal);

    void arrived(const touch::matrix<uint16_t>& frame);

public slots:
    void refresh();
    void clear();
    void save();
    void saveImage();
    void settings();

protected:
    void initContextMenu();
    void initToolbar();

    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void updateStyle();

private:
    ChartView        *chart_view_{};
    QChart           *chart_{};
    Menu             *menu_{};
    QPointer<QAction> pause_action_{};
    QPointer<QAction> save_action_{};
    QPointer<QAction> save_img_action_{};
    QPointer<QAction> clear_action_{};
    QPointer<QAction> params_action_{};

    QDateTimeAxis *axis_x_{};
    QValueAxis    *axis_y_{};

    QVector<QLineSeries *> series_{};

    std::mutex              mtx_{};
    QVector<QList<QPointF>> buffers_{};

    bool    paused_{ false };
    size_t  capacity_{ 2048 };
    int64_t internal_{}; // ms
    int64_t last_pts_{}; // ms
    QTimer *timer_{};
};

#endif //! SCANNER_STATISTICS_DOCK_WIDGET_H