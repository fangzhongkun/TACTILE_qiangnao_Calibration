#ifndef SCANNER_MAINWINDOW_H
#define SCANNER_MAINWINDOW_H

#include "combobox.h"
#include "csv/csv.h"
#include "devices/device.h"
#include "force-calibration/sensor_.h"
#include "log-dock-widget.h"
#include "media/clock.h"
#include "media/queue.h"
#include "menu.h"
#include "serial-dock-widget.h"
#include "serial-tx-dock-widget.h"
#include "statistics-dock-widget.h"
#include "table-dock-widget.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <map>
#include <shared_mutex>

class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class TitleBar;
class QToolBar;
class QStatusBar;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());

    ~MainWindow() override;

    int open(const serial::options& options, int device_type);

    void stop();

signals:
    void arrived();
    void rangeChanged(int min, int max);
    void deviceChanged();
    void device_set(uint8_t dev);
    // device
    void connected();
    void disconnected();
    void new_frame_ready(uint32_t id, const touch::matrix<uint16_t>& frame);
public slots:
    void clearPeaks(); // 清除显示的峰值数据


protected:
    void contextMenuEvent(QContextMenuEvent *event) override;       // 处理右键菜单事件
    bool eventFilter(QObject *watched, QEvent *event) override;  // 处理事件过滤器，允许过滤特定的事件

private:
    void initTitlebar();      // 初始化标题栏
    void initToolbar();       // 初始化工具栏
    void initStatusBar();     // 初始化状态栏
    void initContextMenu();   // 初始化右键菜单
    void showProperties();    // 显示属性对话框
    void updateIcons();       // 更新界面图标
    void init_sensor_connection();
    void setDeviceStreamRunning(bool enabled);
    serial::options old_options_;
    int device_type_;
private:
    // titlebar
    QPointer<TitleBar>  titlebar_{};
    QPointer<QCheckBox> settings_{};
    QPointer<QCheckBox> layout_{};

    // toolbar
    QPointer<QToolBar> toolbar_{};
    QPointer<QAction>  open_action_{};
    QPointer<QAction>  disconnect_action_{};
    QPointer<QAction>  pause_action_{};
    QPointer<QAction>  pause_upload_action_{};
    QPointer<QAction>  resume_upload_action_{};
    QPointer<QAction>  showpeak_{};
    QPointer<QAction>  zeroing_{};
    QPointer<QAction>  rezeroing_{};
    QPointer<QAction>  address_{};
    QPointer<QAction>  upgrade_{};
    QPointer<QSpinBox> range_max_{};
    QPointer<QDoubleSpinBox> pressure_kpa_{};
    QPointer<QPushButton> write_ad_table_{};

    // status bar
    QPointer<QLabel> pos_label_{};
    QPointer<QLabel> value_label_{};
    QPointer<QLabel> statistics_label_{};
    QPointer<QLabel> fps_label_{};
    QPointer<QLabel> sampling_label_{};

    //  // 菜单指针
    QPointer<Menu> context_menu_{};
    QPointer<Menu> layout_menu_{};

    // dock weights
    // 停靠窗口指针，显示额外的信息
    QPointer<StatisticsDockWidget> statistics_window_{};
    QPointer<TableDockWidget>      table_window_{};
    std::map<uint32_t, QPointer<TableDockWidget>> area_table_windows_{};
    QPointer<LogDockWidget>        log_window_{};
    QPointer<SensorWidget>        sensor_window_{};
    QPointer<SerialDockWidget>     serial_rx_window_{};
    QPointer<SerialTxDockWidget>   serial_tx_window_{};
    // 设备指针，使用智能指针管理设备的生命周期
    std::unique_ptr<Device> device_{};

    // 多线程相关
    std::atomic<bool> running_{};   // 标识程序是否正在运行
    std::jthread thread_{};         // 使用 jthread 处理后台任务
    safe_queue<std::pair<uint32_t, touch::matrix<uint16_t>>> frames_{ 4 }; // 存储处理的帧数据，使用线程安全队列


    std::shared_mutex       frame_mtx_{};
    touch::matrix<uint16_t> frame_{}; // 当前状态栏显示/最后一帧
    std::map<uint32_t, touch::matrix<uint16_t>> area_peak_frames_{}; // 各区域独立峰值

    int hovered_x_{};
    int hovered_y_{};

    //
    std::atomic<bool> paused_{};
    bool stream_paused_by_user_{};

    // 用于帧率计算的定时器和相关变量
    QTimer                  *fps_timer_{};
    std::atomic<double>      fps_{};
    uint32_t                 frame_num_{};
    std::chrono::nanoseconds fps_ts_{ av::clock::nopts };

    bool isTestMode = false;
    bool isShowPeak = false;
    uint8_t id_;
};

#endif //! SCANNER_MAINWINDOW_H
