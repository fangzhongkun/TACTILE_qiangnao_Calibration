#include "mainwindow.h"

#include "address-dialog.h"
#include "colormap.h"
#include "config.h"
#include "device-properties-dialog.h"
#include "devices/dhs-default-device.h"
#include "devices/dhs-master-uint16-device.h"
#include "devices/brainco-glove-v2-device.h"
#include "devices/haocun-glove-device.h"
#include "force-calibration/sensor_.h"
#include "frameless/frameless-maker.h"
#include "frameless/titlebar.h"
#include "infrastructure/supported_device_catalog.h"
#include "logging.h"
#include "message.h"
#include "open-device-dialog.h"
#include "scanner.h"
#include "separator.h"
#include "settings-dialog.h"
#include "upgrade-dialog.h"
// #include "utils.h"
#include "utils/utils.h"

#include <fmt/chrono.h>
#include <iostream>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent, const Qt::WindowFlags flags) : QMainWindow(parent, flags)
{
    setWindowFlags(windowFlags() & ~Qt::WindowFullscreenButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);

    titlebar_ = new TitleBar(this);
    new FramelessMaker(titlebar_, this);
    setContentsMargins({});
    setWindowTitle(tr("具感绝对压感半自动标定"));

    statistics_window_ = new StatisticsDockWidget(tr("Statistics"));
    table_window_      = new TableDockWidget(tr("数值"));
    log_window_        = new LogDockWidget(tr("Log Panel"));
    sensor_window_     = new SensorWidget(tr("测试"));
    serial_rx_window_  = new SerialDockWidget(tr("Serial Receiving"));
    serial_tx_window_  = new SerialTxDockWidget(tr("Serial Sending"));

    addDockWidget(Qt::RightDockWidgetArea, statistics_window_);
    addDockWidget(Qt::LeftDockWidgetArea, table_window_);
    addDockWidget(Qt::BottomDockWidgetArea, log_window_);
    addDockWidget(Qt::RightDockWidgetArea, sensor_window_);
    addDockWidget(Qt::BottomDockWidgetArea, serial_rx_window_);
    addDockWidget(Qt::BottomDockWidgetArea, serial_tx_window_);

    setDockNestingEnabled(true);
    initToolbar();
    initStatusBar();
    initTitlebar();
    initContextMenu();
    updateIcons();

    connect(App(), &Scanner::StyleChanged, this, &MainWindow::updateIcons);
    init_sensor_connection();
    fps_timer_ = new QTimer(this);
    fps_timer_->start(500);

    connect(fps_timer_, &QTimer::timeout, this, [this]() {
        const auto ts = av::clock::ns();
        if ((ts - fps_ts_) > 2s || ((ts - fps_ts_) > 1s && frame_num_ > 200)) {
            fps_       = frame_num_ / (static_cast<double>((ts - fps_ts_).count()) / 1000000000.0);
            frame_num_ = 0;
            fps_ts_    = ts;
        }
        fps_label_->setText(QString::asprintf("%.1f", fps_.load()));
    });

    connect(this, &MainWindow::new_frame_ready, this,
            [this](uint32_t id, const touch::matrix<uint16_t>& frame) {
                // 丢弃切换部位前已经进入队列的残留帧，避免旧部位窗口再次出现。
                if (!device_ || id != device_->get_id()) return;
                touch::matrix<uint16_t> frames_;
                const bool brainco =
                    productModelFromId(static_cast<uint8_t>(device_type_)) == ProductModel::BRAINCO_GLOVE_V2;
                const bool converted = brainco ? Utils::convert_to_brainco_template_by_csv(frame, frames_, id)
                                               : Utils::convert_to_template_by_csv(frame, frames_, id);
                if (!converted) frames_ = frame;

                TableDockWidget *table = nullptr;
                if (brainco) {
                    static const QStringList part_names{ QString(), QStringLiteral("拇指"),
                                                         QStringLiteral("食指"), QStringLiteral("中指"),
                                                         QStringLiteral("无名指"), QStringLiteral("小指") };
                    const int part = static_cast<int>((id >> 4U) & 0x0FU);
                    table = table_window_;
                    table->setWindowTitle(QStringLiteral("数值 - %1")
                                              .arg(part >= 1 && part < part_names.size()
                                                       ? part_names.at(part)
                                                       : QStringLiteral("未知部位")));
                    table->product_model_changed(device_type_);
                    table->setDataRange(0, range_max_ ? range_max_->value() : 255);
                    table->setVisible(true);
                }
                else {
                    auto it = area_table_windows_.find(id);
                    if (it == area_table_windows_.end() || !it->second) {
                        const auto title =
                            QString("区域 0x%1").arg(id, 2, 16, QLatin1Char('0')).toUpper();
                        if (area_table_windows_.empty() && table_window_) {
                            // 复用原来的“数值”窗口作为第一个区域窗口，避免额外多出一个空 table。
                            table = table_window_;
                            table->setWindowTitle(title);
                        }
                        else {
                            table = new TableDockWidget(title, this);
                            addDockWidget(Qt::LeftDockWidgetArea, table);
                        }
                        table->product_model_changed(device_type_);
                        table->setDataRange(0, range_max_ ? range_max_->value() : 4095);
                        table->setVisible(true);
                        area_table_windows_[id] = table;
                    }
                    else {
                        table = it->second;
                    }
                }

                table->setData(id, frames_);

                // 标定窗口缓存所有区域矩阵；采集/测试时再按当前选择的部位 ID 取对应矩阵。
                if (sensor_window_) {
                    sensor_window_->setData(id, frames_);
                }

                emit arrived();
            });

    table_window_->setVisible(true);
    sensor_window_->setVisible(true);
    log_window_->setVisible(false);
    statistics_window_->setVisible(false);

    resize(1280, 960);
}

void MainWindow::setDeviceStreamRunning(bool enabled)
{
    // A manual pause owns the stream state until the user presses Resume Upload.
    // Internal query flows must neither resume it nor send duplicate pause commands.
    if (device_ && !stream_paused_by_user_) device_->set_run(enabled);
}

void MainWindow::init_sensor_connection()
{
    connect(sensor_window_, &SensorWidget::read_address_signals, this, [=] {
        setDeviceStreamRunning(false);
        uint8_t id_11 = device_->address();
        sensor_window_->set_address(id_11);
        setDeviceStreamRunning(true);
    });
    connect(sensor_window_, &SensorWidget::choose_id, this, [=](uint8_t id) {
        const bool brainco = productModelFromId(static_cast<uint8_t>(device_type_)) ==
                             ProductModel::BRAINCO_GLOVE_V2;
        if (!brainco) setDeviceStreamRunning(false);
        device_->set_id(id);
        for (auto& [_, table] : area_table_windows_) {
            if (table && table != table_window_) {
                removeDockWidget(table);
                table->deleteLater();
            }
        }
        area_table_windows_.clear();
        area_peak_frames_.clear();
        if (table_window_) {
            if (productModelFromId(static_cast<uint8_t>(device_type_)) == ProductModel::BRAINCO_GLOVE_V2) {
                static const QStringList part_names{ QString(), QStringLiteral("拇指"),
                                                     QStringLiteral("食指"), QStringLiteral("中指"),
                                                     QStringLiteral("无名指"), QStringLiteral("小指") };
                const int part = static_cast<int>((id >> 4U) & 0x0FU);
                table_window_->setWindowTitle(
                    QStringLiteral("数值 - %1")
                        .arg(part >= 1 && part < part_names.size() ? part_names.at(part)
                                                                  : QStringLiteral("未知部位")));
            }
            else {
                table_window_->setWindowTitle(
                    QString("区域 0x%1").arg(id, 2, 16, QLatin1Char('0')).toUpper());
            }
            table_window_->setVisible(true);
        }
        if (!brainco) setDeviceStreamRunning(true);
        // open(old_options_, device_type_);
    });

    connect(sensor_window_, &SensorWidget::modify_address_requested, this, [=](uint8_t id) {
        if (!device_) {
            logw("修改设备ID失败：设备未连接，new_id=0x{:02X}", id);
            return;
        }
        setDeviceStreamRunning(false);
        const int ret = device_->address(id);
        if (ret == 0) {
            // 地址写入后，本地通信目标同步切换到新 ID，后续回读才能继续访问设备。
            device_->set_id(id);
            logi("修改设备ID命令已发送：new_id=0x{:02X}", id);
        }
        else {
            logw("修改设备ID命令发送失败：new_id=0x{:02X}", id);
        }
        setDeviceStreamRunning(true);
    });

    connect(sensor_window_, &SensorWidget::read_sn_signals, this, [=] {
        setDeviceStreamRunning(false);
        QThread::sleep(1);
        std::string sn = device_->serial_number().c_str();
        sensor_window_->set_sn(sn);
        setDeviceStreamRunning(true);
    });

    connect(sensor_window_, &SensorWidget::read_firmsoft_signals, this, [=] {
        setDeviceStreamRunning(false);
        std::string v = device_->firmware_version();
        sensor_window_->set_firmware_version(v);
        setDeviceStreamRunning(true);
    });
    connect(sensor_window_, &SensorWidget::is_calibration_query, this, [=] {
        setDeviceStreamRunning(false);
        uint8_t tmp = device_->calibration_query();
        sensor_window_->set_calibration_state(tmp);
        setDeviceStreamRunning(true);
    });

    connect(sensor_window_, &SensorWidget::set_device_active_query, this, [=](bool enabled) {
        if (!device_) {
            logw("设置设备主动查询状态失败：设备未连接，enabled={}", enabled);
            return;
        }
        setDeviceStreamRunning(enabled);
        logi("设备主动查询{}", enabled ? "恢复" : "暂停");
    });

    connect(sensor_window_, &SensorWidget::re_calibration_requested, this,
            [=](uint8_t part_id) {
                if (!device_) {
                    logw("重新标定命令发送失败：设备未连接");
                    return;
                }
                if (productModelFromId(static_cast<uint8_t>(device_type_)) == ProductModel::BRAINCO_GLOVE_V2) {
                    // 强脑：逐根手指整指擦除（ERASE DataLength=0），5 根手指共 14 个区域全部清除。
                    // 只擦 MCU 中的 LUT，本地采集 CSV 不动，之后点"标定传感器"可重新下发。
                    for (uint8_t finger = 1; finger <= 5; ++finger) {
                        const auto erased = device_->erase_calibration_lut(finger, 0U);
                        if (!erased.success) {
                            Message::error(this, QStringLiteral("擦除手指 %1 失败：%2").arg(finger).arg(erased.error));
                            sensor_window_->set_region_mask_status(device_->query_calibration_all());
                            return;
                        }
                    }
                    logi("标定擦除：强脑全部 14 个区域已擦除");
                    sensor_window_->set_region_mask_status(device_->query_calibration_all());
                    return;
                }
                const auto result = device_->erase_calibration_lut(part_id, 0U);
                logi("标定擦除：part={}（整个部位）, success={}, status=0x{:02X}", part_id, result.success,
                     result.status);
                sensor_window_->set_part_calibration_status(part_id, 1U, result.success, result.status,
                                                            result.expected_points, result.mode, result.crc32,
                                                            result.version, result.error);
            });

    connect(sensor_window_, &SensorWidget::send_calibration_curve_float, this,
            [=](QVector<float> data, int package_id) {
                if (!device_) {
                    logw("标定曲线下发失败：设备未连接，package_id={}", package_id);
                    return;
                }
                if (data.isEmpty()) {
                    logw("标定曲线下发跳过：float 包为空，package_id={}", package_id);
                    return;
                }
                // 主动查询的暂停/恢复由 SensorWidget::simulate_calibration_to_mcu() 统一控制，
                // 这里不能每包单独 set_run(true)，否则 500 ms 包间隔内会恢复主动查询。
                device_->calibration_curve(data.data(), static_cast<size_t>(data.size()), package_id);
                logi("标定曲线下发：package_id={}, type=float, count={}", package_id, data.size());
            });

    connect(sensor_window_, &SensorWidget::download_calibration_lut, this,
            [this](uint8_t part_id, QVector<float> adc, QVector<float> force_cn, uint32_t version,
                   uint8_t region_id) {
                if (!device_) {
                    sensor_window_->set_lut_download_result(part_id, false, 0xffU, 0U, version,
                                                            QStringLiteral("设备未连接"));
                    return;
                }
                const auto result =
                    device_->download_calibration_lut(part_id, adc, force_cn, version, region_id);
                sensor_window_->set_lut_download_result(part_id, result.success, result.status,
                                                        result.crc32, result.version, result.error);
            });

    connect(sensor_window_, &SensorWidget::query_part_calibration_status, this, [this](uint8_t part_id) {
        if (!device_) {
            sensor_window_->set_part_calibration_status(part_id, 1U, false, 0xffU, 0U, 0U, 0U, 0U,
                                                        QStringLiteral("设备未连接"));
            return;
        }
        if (productModelFromId(static_cast<uint8_t>(device_type_)) == ProductModel::BRAINCO_GLOVE_V2) {
            // 强脑：一条 STATUS_ALL 拿到 14 个区域的状态。不能按 region_XX.csv 逐区域查，
            // 那是其它产品的分区配置，会把拇指、小指的区域数算成 1。
            sensor_window_->set_region_mask_status(device_->query_calibration_all());
            return;
        }
        const int region_count =
            std::max(1, Utils::get_region_count_by_csv((static_cast<uint32_t>(part_id) << 4U) | 1U));
        for (uint8_t region = 1U; region <= static_cast<uint8_t>(region_count); ++region) {
            const auto result = device_->query_calibration_status(part_id, region);
            sensor_window_->set_part_calibration_status(part_id, region, result.success, result.status,
                                                        result.expected_points, result.mode, result.crc32,
                                                        result.version, result.error);
        }
    });
}

MainWindow::~MainWindow() { stop(); }

int MainWindow::open(const serial::options& options, const int device_type)
{
    stop();
    // emit device_set(device_type);
    old_options_ = options;
    device_type_ = device_type;
    sensor_window_->product_model_changed(device_type);
    table_window_->product_model_changed(device_type);
    for (auto& [_, table] : area_table_windows_) {
        if (table && table != table_window_) {
            removeDockWidget(table);
            table->deleteLater();
        }
    }
    area_table_windows_.clear();
    area_peak_frames_.clear();
    if (table_window_) {
        table_window_->setWindowTitle(tr("数值"));
    }
    if (!infra::isSupportedDeviceType(device_type)) {
        Message::error(this, tr("Unsupported device type"));
        return -1;
    }

    switch (productModelFromId(static_cast<uint8_t>(device_type))) {
    case ProductModel::ZY_T2_XP:
        device_ = std::make_unique<DhsDefaultDevice>(options);
        logi("Opening device: ZY-T2 || XP, type={}, driver=DhsDefaultDevice", device_type);
        break;
    case ProductModel::OP3:
        device_ = std::make_unique<DhsMasterUint16Device>(options);
        logi("Opening device: OP3, type={}, driver=DhsMasterUint16Device", device_type);
        break;
    case ProductModel::BRAINCO_GLOVE_V2:
        device_ = std::make_unique<BrainCoGloveV2Device>(options);
        logi("Opening device: BrainCo Glove V2, type={}, driver=BrainCoGloveV2Device", device_type);
        break;
    case ProductModel::HAOCUN_GLOVE:
        device_ = std::make_unique<HaocunGloveDevice>(options);
        logi("Opening device: Haocun Glove, type={}, driver=HaocunGloveDevice", device_type);
        break;
    default: Message::error(this, tr("Unsupported device type")); return -1;
    }

    // TODO: move to thread
    if (device_->open(device_type) < 0) {
        Message::error(this, tr("Failed to open the device"));
        return -1;
    }
    stream_paused_by_user_ = false;
    emit connected();
    device_->ondisconnected = [this] { emit disconnected(); };
    // reset
    frame_num_              = 0;
    fps_ts_                 = av::clock::ns();
    fps_                    = 0.0;
    emit deviceChanged();

    auto [rmin, rmax] = device_->range();
    emit rangeChanged(rmin, rmax);

    device_->onarrived = [this](uint32_t id, const touch::matrix<uint16_t>& frame) {
        frame_num_++;
        frames_.push({ id, frame }, true);
    };

    device_->onbridgearrived = [this](uint16_t positive, uint16_t negative) {
        if (sensor_window_) {
            sensor_window_->setBridgeData(positive, negative);
        }
    };

    running_ = true;
    frames_.start();
    thread_ = std::jthread([=, this] {
        while (running_) {
            id_ = device_->get_id();
            device_->set_slave(id_);
            auto next = frames_.wait_and_pop();
            if (!next.has_value() || paused_) continue;

            // convert
            auto [id, matrix] = next.value();
            // showpeaks
            std::scoped_lock lock(frame_mtx_);
            if (!isTestMode) {
                if (showpeak_->isChecked()) {
                    isShowPeak = true;
                }
                else {
                    isShowPeak = false;
                }
            }

            auto& peak_frame = area_peak_frames_[id];
            peak_frame = (isShowPeak && peak_frame.rows == matrix.rows && peak_frame.cols == matrix.cols)
                             ? touch::max(peak_frame, matrix)
                             : matrix;
            frame_     = peak_frame;

            // if (device_type==4) {
            //     touch::matrix<uint16_t> frame_type{};
            //     Utils::ConverToTemplate(frame_,frame_type);
            //     emit new_frame_ready(id, frame_type);
            // }
            // else {
            //     emit new_frame_ready(id, frame_);
            // }
            emit new_frame_ready(id, peak_frame);
            // table_window_->setData(id, frame_);
            // sensor_window_->setData(id, frame_);
            // emit arrived();
        }
    });

    return 0;
}

void MainWindow::stop()
{
    running_ = false;
    frames_.stop();
    if (thread_.joinable()) thread_.join();
}

void MainWindow::clearPeaks()
{
    if (showpeak_->isChecked()) {
        std::scoped_lock lock(frame_mtx_);
        frame_.fill(0);
        for (auto& [_, peak_frame] : area_peak_frames_) {
            peak_frame.fill(0);
        }
    }
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event) { context_menu_->exec(event->globalPos()); }

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == statusBar() && event->type() == QEvent::Paint) {
        std::shared_lock lock(frame_mtx_);

        if (frame_.rows > 0 && frame_.cols > 0) {
            auto x = hovered_x_;
            auto y = hovered_y_;

            x = std::clamp(x, 0, frame_.cols - 1);
            y = std::clamp(y, 0, frame_.rows - 1);

            pos_label_->setText(QString("L %1, C %2").arg(y).arg(x));
            value_label_->setText(QString::number(frame_(y, x)));
            statistics_label_->setText(QString::asprintf("[%d, %d] μ %.2f Σ %.2f", frame_.min(),
                                                         frame_.max(), frame_.mean<float>(),
                                                         frame_.sum<float>()));
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initTitlebar()
{
    layout_ = new QCheckBox();
    layout_->setObjectName("layout-btn");
    layout_->setCheckable(false);

    // menu
    {
        layout_menu_ = new Menu(this);

        {
            const auto visible = layout_menu_->addAction(tr("Toolbar"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::toolbar);
            toolbar_->setVisible(config::layout::visibility::toolbar);

            connect(visible, &QAction::triggered, toolbar_, &QToolBar::setVisible);

            connect(toolbar_, &QToolBar::visibilityChanged, visible, &QAction::setChecked);
            connect(toolbar_, &QToolBar::visibilityChanged,
                    [](auto&& v) { config::layout::visibility::toolbar = v; });
        }

        {
            const auto visible = layout_menu_->addAction(tr("Status Bar"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::statusbar);
            statusBar()->setVisible(config::layout::visibility::statusbar);
            connect(visible, &QAction::triggered, [this](auto&& state) {
                statusBar()->setVisible(state);
                config::layout::visibility::statusbar = state;
            });
        }

        layout_menu_->addSeparator();

        {
            const auto visible = layout_menu_->addAction(tr("Statistics Window"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::statistics_window);
            statistics_window_->setVisible(config::layout::visibility::statistics_window);
            connect(visible, &QAction::triggered, statistics_window_, &DockWidget::setAlive);
            connect(statistics_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(statistics_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::statistics_window = v; });
        }

        {
            const auto visible = layout_menu_->addAction(tr("Values Window"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::values_window);
            table_window_->setVisible(config::layout::visibility::values_window);
            connect(visible, &QAction::triggered, table_window_, &DockWidget::setAlive);
            connect(table_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(table_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::values_window = v; });

            connect(table_window_, &TableDockWidget::grabbed,
                    [this](const auto& pixmap, std::wstring path) {
                        QString base = "scanner_table";
                        if (device_) {
                            if (auto sn = device_->serial_number(); sn != "(Unknown)") {
                                base = QString::fromStdString(sn);
                            }
                        }

                        //                auto filename = QFileDialog::getSaveFileName(
                        //                    nullptr, tr("Save Image"), config::record::image::path + "/" +
                        //                    base + "_" + QTIMESTAMP, "PNG Image (*.png);;JPG Image
                        //                    (*.jpg)");
                        QString filename =
                            QString::fromStdWString(path) + "/" + base + "_" + QTIMESTAMP + ".png";

                        if (filename.isEmpty()) return;

                        (pixmap.save(filename)) ? Message::success(nullptr, tr("Saved."))
                                                : Message::error(nullptr, tr("Failed to save."));
                    });
        }

        layout_menu_->addSeparator();

        {
            const auto visible = layout_menu_->addAction(tr("Log Panel"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::log_window);
            log_window_->setVisible(config::layout::visibility::log_window);
            connect(visible, &QAction::triggered, log_window_, &DockWidget::setAlive);
            connect(log_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(log_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::log_window = v; });
        }
        layout_menu_->addSeparator();
        {
            const auto visible = layout_menu_->addAction(tr("Serial Receiving"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::serial_rx_window);
            serial_rx_window_->setAlive(config::layout::visibility::serial_rx_window);
            serial::hook_before_rx(
                [this](auto&& buf, auto&& size) { serial_rx_window_->append(buf, size); });
            connect(visible, &QAction::triggered, serial_rx_window_, &DockWidget::setAlive);
            connect(serial_rx_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(serial_rx_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::serial_rx_window = v; });
        }
        {
            const auto visible = layout_menu_->addAction(tr("Serial Sending"));
            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::serial_tx_window);
            serial_tx_window_->setAlive(config::layout::visibility::serial_tx_window);
            serial::hook_before_tx(
                [this](auto&& buf, auto&& size) { serial_tx_window_->append(buf, size); });
            connect(visible, &QAction::triggered, serial_tx_window_, &DockWidget::setAlive);
            connect(serial_tx_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(serial_tx_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::serial_tx_window = v; });

            serial_tx_window_->onsend = [this](uint8_t ch, const whisper::payload& payload) {
                if (device_) {
                    device_->send(ch, payload);
                }
            };
        }
        layout_menu_->addSeparator();

        {
            const auto visible = layout_menu_->addAction(tr("Test Window"));

            visible->setCheckable(true);
            visible->setChecked(config::layout::visibility::log_window);
            sensor_window_->setVisible(config::layout::visibility::log_window);
            connect(visible, &QAction::triggered, sensor_window_, &DockWidget::setAlive);
            connect(sensor_window_, &DockWidget::aliveChanged, visible, &QAction::setChecked);
            connect(sensor_window_, &DockWidget::aliveChanged,
                    [](auto&& v) { config::layout::visibility::log_window = v; });
        }
    }

    // settings button
    settings_ = new QCheckBox();
    settings_->setObjectName("settings-btn");
    settings_->setToolTip(tr("Settings"));
    settings_->setCheckable(false);

    // title bar
    titlebar_->addWidget(layout_, 0, Qt::AlignRight | Qt::AlignVCenter);
    titlebar_->addWidget(settings_, 0, Qt::AlignRight | Qt::AlignVCenter);
    titlebar_->setToolTip(tr("Layout"));
    setMenuWidget(titlebar_);

    connect(settings_, &QCheckBox::clicked, [] {
        SettingsDialog dialog{};
        dialog.exec();
    });

    connect(layout_, &QCheckBox::clicked,
            [this] { layout_menu_->exec(mapToGlobal(layout_->geometry().bottomLeft()) + QPoint{ 0, 5 }); });
}

void MainWindow::initToolbar()
{
    toolbar_ = new QToolBar("Tool Bar", this);
    toolbar_->setMovable(false);
    toolbar_->setIconSize({ 22, 22 });
    addToolBar(toolbar_);

    // tools
    {
        open_action_ = addAction(QIcon::fromTheme("unlink"), tr("Open Device"), Qt::CTRL | Qt::Key_O);
        toolbar_->addAction(open_action_);

        disconnect_action_ = addAction(tr("Disconnect"));

        pause_action_ = addAction(tr("Pause"));
        pause_action_->setCheckable(true);
        pause_action_->setToolTip(QStringLiteral("暂停界面显示，设备仍会继续上报"));
        toolbar_->addAction(pause_action_);
        toolbar_->widgetForAction(pause_action_)->setObjectName("pause-btn");

        pause_upload_action_ = toolbar_->addAction(QStringLiteral("暂停上报"));
        resume_upload_action_ = toolbar_->addAction(QStringLiteral("恢复上报"));
        pause_upload_action_->setObjectName("pause-upload-action");
        resume_upload_action_->setObjectName("resume-upload-action");
        pause_upload_action_->setVisible(false);
        resume_upload_action_->setVisible(false);

        toolbar_->addSeparator();

        showpeak_ = toolbar_->addAction(QIcon::fromTheme("peak"), tr("Showpeaks"));
        showpeak_->setCheckable(true);

        toolbar_->addSeparator();

        zeroing_ = toolbar_->addAction(tr("Zeroing"));
        zeroing_->setCheckable(true);
        zeroing_->setChecked(true);
        rezeroing_ = toolbar_->addAction(QIcon::fromTheme("refresh"), tr("Re-Zeroing"));

        toolbar_->addSeparator();
        address_ = toolbar_->addAction(QIcon::fromTheme("address"), tr("Address"));
        upgrade_ = toolbar_->addAction(QIcon::fromTheme("upgrade"), tr("Upgrade"));

        toolbar_->addSeparator();
        range_max_ = new QSpinBox();
        range_max_->setRange(2, std::numeric_limits<int>::max());
        range_max_->setValue(4095);
        range_max_->setMinimumWidth(115);
        range_max_->setObjectName("range-max");
        range_max_->setToolTip(tr("The maximum value of the device value range"));
        toolbar_->addWidget(range_max_);

        auto* pressure_label = new QLabel(QStringLiteral("压强"));
        pressure_label->setObjectName("pressure-label");
        toolbar_->addWidget(pressure_label);

        pressure_kpa_ = new QDoubleSpinBox();
        pressure_kpa_->setRange(0.0, 100000.0);
        pressure_kpa_->setDecimals(2);
        pressure_kpa_->setSingleStep(1.0);
        pressure_kpa_->setSuffix(QStringLiteral(" KPa"));
        pressure_kpa_->setMinimumWidth(140);
        pressure_kpa_->setObjectName("pressure-kpa");
        pressure_kpa_->setToolTip(QStringLiteral("写入 AD 表使用的压强，单位 KPa"));
        toolbar_->addWidget(pressure_kpa_);

        write_ad_table_ = new QPushButton(QStringLiteral("写表"));
        write_ad_table_->setObjectName("write-ad-table-btn");
        write_ad_table_->setToolTip(QStringLiteral("把当前数值表中的 AD 值追加写入 AD表 目录下的 CSV"));
        toolbar_->addWidget(write_ad_table_);
    }

    // connects
    {
        connect(open_action_, &QAction::triggered, [this] {
            if (OpenDeviceDialog dialog{}; dialog.exec() == QDialog::Accepted) {
                if (open(dialog.options(), dialog.device()) < 0) {
                    Message::error(this, tr("Failed to open the device"));
                }
            }
        });

        connect(this, &MainWindow::connected, this, [this]() {
            open_action_->setIcon(QIcon(":/icons/link-green"));
            const bool brainco = productModelFromId(static_cast<uint8_t>(device_type_)) ==
                                 ProductModel::BRAINCO_GLOVE_V2;
            pause_upload_action_->setVisible(brainco);
            resume_upload_action_->setVisible(brainco);
            pause_upload_action_->setEnabled(brainco);
            resume_upload_action_->setEnabled(false);
        });
        connect(this, &MainWindow::disconnected, this, [this]() {
            if (!device_ || !device_->ready()) open_action_->setIcon(QIcon::fromTheme("unlink"));
            stream_paused_by_user_ = false;
            pause_upload_action_->setEnabled(false);
            resume_upload_action_->setEnabled(false);
        });

        connect(disconnect_action_, &QAction::triggered, [this] {
            device_.reset();
            emit disconnected();
        });

        connect(pause_action_, &QAction::triggered, [this](auto&& state) { paused_ = state; });

        connect(pause_upload_action_, &QAction::triggered, [this] {
            if (!device_ || device_->enable(Capacities::SAMPLE_STREAM, false) != 0) {
                Message::warning(this, QStringLiteral("设备未连接，暂停上报指令未发送"));
                return;
            }
            stream_paused_by_user_ = true;
            pause_upload_action_->setEnabled(false);
            resume_upload_action_->setEnabled(true);
            logi("已发送暂停传感器上报指令（Channel=06, Payload=01 00）");
        });
        connect(resume_upload_action_, &QAction::triggered, [this] {
            if (!device_ || device_->enable(Capacities::SAMPLE_STREAM, true) != 0) {
                Message::warning(this, QStringLiteral("设备未连接，恢复上报指令未发送"));
                return;
            }
            stream_paused_by_user_ = false;
            pause_action_->setChecked(false);
            paused_ = false;
            pause_upload_action_->setEnabled(true);
            resume_upload_action_->setEnabled(false);
            logi("已发送恢复传感器上报指令（Channel=06, Payload=01 01）");
        });

        connect(zeroing_, &QAction::toggled,
                [this](auto state) { device_ && device_->enable(Capacities::ZEROING, state); });
        connect(rezeroing_, &QAction::triggered, [this] { device_ && device_->rezeroing(); });

        connect(address_, &QAction::triggered, [this] {
            if (!device_) {
                Message::warning(this, tr("No opened device"));
                return;
            }

            AddressDialog dialog(device_.get(), this);
            dialog.exec();
        });

        connect(write_ad_table_, &QPushButton::clicked, [this] {
            TableDockWidget* table = table_window_;
            if (device_) {
                const auto it = area_table_windows_.find(device_->get_id());
                if (it != area_table_windows_.end() && it->second) table = it->second;
            }
            if (!table) {
                Message::warning(this, QStringLiteral("当前没有可写入的数值表"));
                return;
            }

            const QString dir =
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AD表"));
            if (!QDir().mkpath(dir)) {
                Message::error(this, QStringLiteral("无法创建目录：%1").arg(dir));
                return;
            }

            const QString file = QDir(dir).filePath(QStringLiteral("AD表.csv"));
            QString error;
            if (!table->appendDisplayedAdCsv(file, pressure_kpa_->value(), &error)) {
                Message::warning(this, error.isEmpty() ? QStringLiteral("写入 AD 表失败") : error);
                return;
            }
            Message::success(this, QStringLiteral("已写入 %1").arg(file));
        });

        connect(range_max_, &QSpinBox::valueChanged, [this](int max) {
            statistics_window_->setYAxisRange(0, max);
            table_window_->setDataRange(0, max);
            for (auto& [_, table] : area_table_windows_) {
                if (table) table->setDataRange(0, max);
            }
        });

        connect(this, &MainWindow::rangeChanged, this, [this](auto min, auto max) {
            if (range_max_->value() != max) {
                range_max_->setValue(max);
            }
            statistics_window_->setYAxisRange(min, max);
            table_window_->setDataRange(min, max);
            for (auto& [_, table] : area_table_windows_) {
                if (table) table->setDataRange(min, max);
            }
        });
    }
}

void MainWindow::initStatusBar()
{
    setStatusBar(new QStatusBar(this));
    statusBar()->setContentsMargins({ 12, 1, 12, 1 });
    statusBar()->layout()->setSpacing(10);
    statusBar()->setSizeGripEnabled(false);

    // labels
    {
        // position & values
        pos_label_ = new QLabel("L 0, C 0");
        pos_label_->setAlignment(Qt::AlignCenter);
        statusBar()->addWidget(pos_label_);

        statusBar()->addWidget(new QLabel("-"));

        value_label_ = new QLabel("0");
        statusBar()->addWidget(value_label_);

        statusBar()->addWidget(new QLabel("∈"));

        // min-max, mean
        statistics_label_ = new QLabel("[0, 0] μ 0.0 Σ 0.0");
        statusBar()->addWidget(statistics_label_);

        // FPS
        statusBar()->addWidget(new Separator());

        fps_label_ = new QLabel("0.0");
        statusBar()->addWidget(new QLabel("FPS"));
        statusBar()->addWidget(fps_label_, 1);

        // csv recording
        sampling_label_ = new QLabel("Sampling: --:--:--");
        sampling_label_->hide();
        sampling_label_->setAlignment(Qt::AlignCenter);
        sampling_label_->setContentsMargins({ 15, 0, 8, 0 });
        statusBar()->addPermanentWidget(sampling_label_, 0);
    }

    statusBar()->installEventFilter(this);

    // connects
    {
        connect(this, &MainWindow::arrived, this, [=, this] {
            statusBar()->update();

            std::shared_lock lock(frame_mtx_);
            statistics_window_->arrived(frame_);
        });
    }
}

void MainWindow::initContextMenu()
{
    context_menu_ = new Menu(this);

    context_menu_->addAction(open_action_);
    context_menu_->addAction(disconnect_action_);

    context_menu_->addSeparator();

    context_menu_->addAction(pause_action_);

    context_menu_->addSeparator();

    // zeroing calibration
    context_menu_->addAction(zeroing_);
    context_menu_->addAction(rezeroing_);

    context_menu_->addSeparator();

    // peaks
    context_menu_->addAction(showpeak_);

    const auto clear_action = addAction(tr("Clear Peaks"), Qt::Key_F5);
    context_menu_->addAction(clear_action);
    connect(clear_action, &QAction::triggered, [this] { clearPeaks(); });

    context_menu_->addSeparator();

    const auto properties = context_menu_->addAction(QIcon(":/icons/info-light"), tr("Device Properties"));
    connect(properties, &QAction::triggered, [this] { showProperties(); });
}

void MainWindow::updateIcons()
{
    const QString theme = (config::theme == "dark") ? "light" : "dark";

    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/pause-" + theme), QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(":/icons/play-" + theme), QIcon::Normal, QIcon::On);
        pause_action_->setIcon(icon);
    }
    {
        QIcon icon{};
        icon.addPixmap(QPixmap(":/icons/" + QString::fromStdString(config::theme) + "/png/zero.png"),
                       QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(":/icons/zero"), QIcon::Normal, QIcon::On);
        zeroing_->setIcon(icon);
    }
}

void MainWindow::showProperties()
{
    if (!device_) {
        Message::warning(this, tr("No opened device"));
        return;
    }

    DevicePropertiesDialog dialog(device_.get(), this);
    dialog.exec();
}
