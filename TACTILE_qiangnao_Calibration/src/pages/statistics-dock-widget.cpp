#include "statistics-dock-widget.h"

#include "config.h"
#include "csv/csv.h"
#include "dock-titlebar.h"
#include "logging.h"
#include "menu.h"
#include "message.h"
#include "scanner.h"
#include "statistics-parameters-dialog.h"
#include "utils.h"

#include <QDateTime>
#include <QGraphicsSimpleTextItem>

#define CH_MIN  0x00
#define CH_MAX  0x01
#define CH_MEAN 0x02

StatisticsDockWidget::StatisticsDockWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : DockWidget(title, parent, flags)
{
    setMinimumHeight(160);

    chart_view_ = new ChartView(this);
    chart_view_->setRenderHint(QPainter::Antialiasing);
    chart_view_->setContentsMargins({});
    layout_->addWidget(chart_view_);

    {
        chart_ = new QChart();
        chart_->setMargins({});
        chart_->setAnimationOptions(QChart::NoAnimation);
        chart_view_->setChart(chart_);

        for (int i = 0; i < CHANNELS; ++i) {
            series_.append(new QLineSeries());
            chart_->addSeries(series_[i]);
            buffers_.append(QList<QPointF>{});
        }

        series_[CH_MIN]->setName("Min");
        series_[CH_MAX]->setName("Max");
        series_[CH_MEAN]->setName("Mean");

        axis_x_ = new QDateTimeAxis();
        axis_y_ = new QValueAxis();

        chart_->addAxis(axis_x_, Qt::AlignBottom);
        chart_->addAxis(axis_y_, Qt::AlignLeft);

        axis_x_->setFormat("hh:mm:ss");
        axis_x_->setTickCount(10);
        axis_x_->setGridLineVisible(false);

        axis_y_->setRange(0, 32768);
        axis_y_->setTickCount(4);
        axis_y_->setLabelFormat("%d");

        for (int i = 0; i < CHANNELS; ++i) {
            series_[i]->attachAxis(axis_x_);
            series_[i]->attachAxis(axis_y_);
        }
    }

    initContextMenu();
    initToolbar();
    updateStyle();

    timer_ = new QTimer(this);
    timer_->start(50); // ms
    connect(timer_, &QTimer::timeout, this, &StatisticsDockWidget::refresh);

    connect(chart_view_, &ChartView::mouseMoved, [](const QPointF&) {});

    connect(App(), &Scanner::StyleChanged, this, &StatisticsDockWidget::updateStyle);
}

void StatisticsDockWidget::setYAxisRange(const qreal min, const qreal max) { axis_y_->setRange(min, max); }

void StatisticsDockWidget::arrived(const touch::matrix<uint16_t>& frame)
{
    if (paused_) return;

    const auto now = QDateTime::currentDateTime().toMSecsSinceEpoch();

    if (internal_ > 0 && (now - last_pts_ < internal_)) return;
    last_pts_ = QDateTime::currentDateTime().toMSecsSinceEpoch();

    { // critical
        std::scoped_lock lock(mtx_);

        for (auto& buffer : buffers_) {
            while (static_cast<size_t>(buffer.size()) > capacity_) {
                buffer.removeFirst();
            }
        }

        buffers_[CH_MIN].append(QPointF{ static_cast<qreal>(now), static_cast<qreal>(frame.min()) });
        buffers_[CH_MAX].append(QPointF{ static_cast<qreal>(now), static_cast<qreal>(frame.max()) });
        buffers_[CH_MEAN].append(QPointF{ static_cast<qreal>(now), (frame.mean<qreal>()) });
    }
}

void StatisticsDockWidget::refresh()
{
    if (!alive()) return;

    std::scoped_lock lock(mtx_);

    for (int i = 0; i < series_.size(); ++i) {
        series_[i]->replace(buffers_[i]);
    }

    // FIXME: stack
    if (!buffers_[0].empty()) {
        axis_x_->setRange(QDateTime::fromMSecsSinceEpoch(buffers_[0].first().x()),
                          QDateTime::fromMSecsSinceEpoch(buffers_[0].back().x()));
    }
}

void StatisticsDockWidget::clear()
{
    { // critical
        std::scoped_lock lock(mtx_);

        for (auto& buffer : buffers_) {
            buffer.clear();
        }
    }
}

void StatisticsDockWidget::save()
{
    const auto file = config::record::csv::path + "/scanner_statistics_" + QTIMESTAMP + ".csv";

    CSV csv_{};
    if (!csv_.open(file.toStdString())) {
        loge("failed to open file: {}", file.toStdString());
        Message::error(this, tr("Failed to open the file."));
        return;
    }

    for (const auto& buffer : buffers_) {
        for (const auto& p : buffer) {
            csv_.write(std::to_string(p.y()) + ",");
        }
        csv_.write("\n");
    }

    Message::success(this, tr("Saved."));
}

void StatisticsDockWidget::saveImage()
{
    const auto pixmap = chart_view_->grab();

    auto filename = QFileDialog::getSaveFileName(
        nullptr, tr("Save Image"), config::record::image::path + "/scanner_table_" + QTIMESTAMP,
        ".png(*.png);;.jpg(*.jpg)");

    if (filename.isEmpty()) return;

    (pixmap.save(filename)) ? Message::success(this, tr("Saved."))
                            : Message::error(this, tr("Failed to save."));
}

void StatisticsDockWidget::settings()
{
    StatisticsParametersDialog dialog{};
    dialog.setCapacity(static_cast<int>(capacity_));
    dialog.setInternal(static_cast<int>(internal_));
    if (dialog.exec() == QDialog::Accepted) {
        capacity_ = dialog.capacity();
        internal_ = dialog.internal();
    }
}

void StatisticsDockWidget::resizeEvent(QResizeEvent *event)
{
    axis_x_->setTickCount(event->size().width() / 100);
    axis_y_->setTickCount(event->size().height() / 50);

    QDockWidget::resizeEvent(event);
}

void StatisticsDockWidget::initContextMenu()
{
    // parameters
    params_action_ = new QAction(QIcon::fromTheme("parameter"), tr("Parameters"));
    connect(params_action_, &QAction::triggered, [this] { settings(); });

    // pause
    pause_action_ = new QAction(tr("Pause"));
    pause_action_->setCheckable(true);
    connect(pause_action_, &QAction::toggled, [this](auto&& paused) { paused_ = paused; });

    // download
    save_action_ = new QAction(QIcon::fromTheme("download"), tr("Save as CSV"));
    connect(save_action_, &QAction::triggered, [this] { save(); });

    // image
    save_img_action_ = new QAction(tr("Save as Image"));
    connect(save_img_action_, &QAction::triggered, [this] { saveImage(); });

    // clear
    clear_action_ = new QAction(QIcon::fromTheme("delete"), tr("Clear All"));
    connect(clear_action_, &QAction::triggered, [this] { clear(); });

    // menu
    menu_ = new Menu(this);
    menu_->addAction(save_action_);
    menu_->addAction(save_img_action_);
    menu_->addSeparator();
    menu_->addAction(pause_action_);
    menu_->addSeparator();
    menu_->addAction(clear_action_);
}

void StatisticsDockWidget::initToolbar()
{
    const auto settings = new QToolButton();
    settings->setDefaultAction(params_action_);

    const auto pause = new QToolButton();
    pause->setDefaultAction(pause_action_);

    const auto save = new QToolButton();
    save->setDefaultAction(save_action_);

    const auto clear = new QToolButton();
    clear->setDefaultAction(clear_action_);

    titlebar_->addWidget(settings, 0, Qt::AlignRight);
    titlebar_->addWidget(pause, 0, Qt::AlignRight);
    titlebar_->addWidget(save, 0, Qt::AlignRight);
    titlebar_->addWidget(clear, 0, Qt::AlignRight);
}

void StatisticsDockWidget::updateStyle()
{
    const QString theme = (config::theme == "dark") ? "light" : "dark";

    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/pause-" + theme), QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(":/icons/play-" + theme), QIcon::Normal, QIcon::On);
        pause_action_->setIcon(icon);
    }

    chart_->setTheme(config::theme == "dark" ? QChart::ChartThemeDark : QChart::ChartThemeLight);
    chart_->setBackgroundBrush(QBrush(config::theme == "dark" ? QColor(24, 24, 25) : QColor(255, 255, 255)));
}

void StatisticsDockWidget::contextMenuEvent(QContextMenuEvent *event) { menu_->exec(event->globalPos()); }
