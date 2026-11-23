#include "sensor_.h"

#include "application/calibration_capture_service.h"
#include "application/calibration_test_service.h"
#include "logging.h"
#include "utils/utils.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

namespace
{
    static void waitForMs(int ms);

    // 强脑：砝码/杆重按 g 输入，采集 CSV、下发 LUT 与传感器输出统一使用 mN。
    constexpr double kGramToMilliNewton = 9.80665;

    static int normalizeCollectPartId(int value)
    {
        // 新部位 ID 规则：11、21、31、41、51、61。
        // 兼容旧界面/旧配置传入的 1~6，自动映射为 11~61。
        if (value >= 1 && value <= 6) {
            return (value << 4) | 0x01;
        }

        switch (value) {
        case 0x11:
        case 0x21:
        case 0x31:
        case 0x41:
        case 0x51:
        case 0x61: return value;
        default:   break;
        }

        const int index = qBound(1, value / 0x10, 6);
        return (index << 4) | 0x01;
    }

    static int collectPartIndexFromId(int value)
    {
        return qBound(1, normalizeCollectPartId(value) >> 4, 6);
    }

    static int nextCollectPartId(int value)
    {
        const int index      = collectPartIndexFromId(value);
        const int next_index = (index >= 6) ? 1 : (index + 1);
        return (next_index << 4) | 0x01;
    }

    static touch::matrix<uint16_t> braincoRegionMatrix(const touch::matrix<uint16_t>& source,
                                                       int part_id, int region_id)
    {
        if (source.empty()) return source;
        int start = 0;
        int rows = 0;
        const int part = (part_id >> 4) & 0x0F;
        if (part == 1) {
            if (region_id == 1) { start = 0; rows = 8; }
            else if (region_id == 2) { start = 8; rows = 3; }
        }
        else if (part >= 2 && part <= 4) {
            if (region_id == 1) { start = 0; rows = 7; }
            else if (region_id == 2) { start = 7; rows = 2; }
            else if (region_id == 3) { start = 9; rows = 2; }
        }
        else if (part == 5) {
            if (region_id == 1) { start = 0; rows = 6; }
            else if (region_id == 2) { start = 6; rows = 1; }
            else if (region_id == 3) { start = 7; rows = 2; }
        }
        if (rows <= 0 || start + rows > source.rows) return {};
        touch::matrix<uint16_t> selected(source.rows, source.cols, uint16_t{});
        for (int row = start; row < start + rows; ++row)
            for (int col = 0; col < source.cols; ++col)
                selected(row, col) = source(row, col);
        return selected;
    }
} // namespace

SensorWidget::SensorWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : DockWidget(title, parent, flags)
{
    current_save_path_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (current_save_path_.isEmpty()) {
        current_save_path_ = QDir::homePath();
    }

    page_ = new QWidget(this);
    setWidget(page_);

    init_style();
    init_ui();
    init_connections();
    set_collect_part_id(collect_part_id_, false);
    set_calibration_status_ui(app::CalibrationStatus::Unknown);

    logi("SensorWidget initialized: compact engineering UI, save_path={}",
         current_save_path_.toStdString());
}

void SensorWidget::init_style()
{
    setStyleSheet(R"(
        QWidget {
            background-color: #1f1f23;
            color: #dcdcdc;
            font-size: 15px;
        }
        QGroupBox {
            background-color: #25262b;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            margin-top: 14px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 8px;
            color: #7dd3fc;
            font-weight: bold;
        }
        QLabel { background: transparent; }
        QLabel#valueLabel {
            color: #4fc1ff;
            font-weight: bold;
        }
        QLabel#statusUnknown {
            color: #facc15;
            font-weight: bold;
        }
        QLabel#statusOk {
            color: #4ade80;
            font-weight: bold;
        }
        QLabel#statusBad {
            color: #f87171;
            font-weight: bold;
        }
        QPushButton {
            font-weight: 600;
            background-color: #2d2f36;
            border: 1px solid #40414f;
            padding: 6px 14px;
            border-radius: 6px;
        }
        QPushButton:hover { background-color: #3a3c45; }
        QPushButton:pressed {
            background-color: #0ea5e9;
            color: white;
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            color: #777;
        }
        QSpinBox, QDoubleSpinBox {
            background-color: #2a2b31;
            border: 1px solid #40414f;
            padding: 2px 3px;
            border-radius: 2px;
        }
    )");
}

void SensorWidget::init_ui()
{
    auto *main_layout = new QVBoxLayout(page_);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(8);

    main_layout->addWidget(create_system_settings_group());
    main_layout->addWidget(create_device_info_group());

    current_centroid_label_ = new QLabel("当前质心：cx=0.500000, cy=0.500000    差分=0.000000", page_);
    current_centroid_label_->setObjectName("valueLabel");
    current_centroid_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    current_centroid_label_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                     Qt::TextSelectableByKeyboard);
    main_layout->addWidget(current_centroid_label_);
    main_layout->addStretch();
}

QWidget *SensorWidget::create_system_settings_group()
{
    auto *group  = new QGroupBox("系统设置", page_);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *row1        = new QWidget(group);
    auto *row1_layout = new QHBoxLayout(row1);
    row1_layout->setContentsMargins(0, 0, 0, 0);
    row1_layout->setSpacing(10);

    auto *host_version_label = new QLabel(QString::fromLatin1(SCANNER_VERSION), row1);
    host_version_label->setObjectName("valueLabel");

    product_model_label_ = new QLabel(productModelToString(product_type_), row1);
    product_model_label_->setObjectName("valueLabel");

    row1_layout->addWidget(new QLabel("上位机版本：", row1));
    row1_layout->addWidget(host_version_label);
    row1_layout->addSpacing(24);
    row1_layout->addWidget(new QLabel("产品型号：", row1));
    row1_layout->addWidget(product_model_label_);
    row1_layout->addStretch();

    auto *row2        = new QWidget(group);
    auto *row2_layout = new QHBoxLayout(row2);
    row2_layout->setContentsMargins(0, 0, 0, 0);
    row2_layout->setSpacing(8);

    path_set_button_       = new QPushButton("路径设置", row2);
    save_path_value_label_ = new QLabel(current_save_path_, row2);
    save_path_value_label_->setObjectName("valueLabel");
    save_path_value_label_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                    Qt::TextSelectableByKeyboard);

    row2_layout->addWidget(path_set_button_);
    row2_layout->addWidget(new QLabel("保存路径：", row2));
    row2_layout->addWidget(save_path_value_label_, 1);

    layout->addWidget(row1);
    layout->addWidget(row2);
    return group;
}

QWidget *SensorWidget::create_device_info_group()
{
    auto *group  = new QGroupBox("设备信息", page_);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *grid_host = new QWidget(group);
    auto *grid      = new QGridLayout(grid_host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    firmware_version_label_ = new QLabel("NULL", grid_host);
    firmware_version_label_->setObjectName("valueLabel");

    serial_number_label_ = new QLabel("NULL", grid_host);
    serial_number_label_->setObjectName("valueLabel");
    serial_number_label_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    serial_number_read_button_ = new QPushButton("读取序列号", grid_host);

    sensor_id_label_ = new QLabel("null", grid_host);
    sensor_id_label_->setObjectName("valueLabel");
    sensor_id_label_->setMinimumWidth(120);
    sensor_id_label_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    sensor_id_read_button_ = new QPushButton("读取", grid_host);

    sensor_modify_id_spinbox_ = new QSpinBox(grid_host);
    sensor_modify_id_spinbox_->setRange(0x00, 0x61);
    sensor_modify_id_spinbox_->setPrefix("0x");
    sensor_modify_id_spinbox_->setDisplayIntegerBase(16);
    sensor_modify_id_spinbox_->setMinimumWidth(120);
    sensor_modify_id_button_ = new QPushButton("修改", grid_host);

    calibration_status_label_ = new QLabel("未查询", grid_host);
    calibration_status_label_->setObjectName("statusUnknown");
    calibration_status_label_->setMinimumWidth(440);
    calibration_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                       Qt::TextSelectableByKeyboard);
    calibration_query_button_ = new QPushButton("查询", grid_host);

    // 强脑 14 个区域的标定状态：5 根手指 × 指尖/指中/指根，拇指没有指中。
    // 格子下标与 STATUS_ALL 掩码的位序一致（bit0 拇指指尖 … bit13 小指指根）。
    region_status_title_ = new QLabel("区域状态：", grid_host);
    region_status_host_  = new QWidget(grid_host);
    auto *region_grid    = new QGridLayout(region_status_host_);
    region_grid->setContentsMargins(0, 0, 0, 0);
    region_grid->setHorizontalSpacing(12);
    region_grid->setVerticalSpacing(4);
    const QStringList finger_names{ "拇指", "食指", "中指", "无名指", "小指" };
    const QStringList region_names{ "指尖", "指中", "指根" };
    for (int col = 0; col < region_names.size(); ++col) {
        region_grid->addWidget(new QLabel(region_names.at(col), region_status_host_), 0, col + 1,
                               Qt::AlignCenter);
    }
    size_t bit = 0U;
    for (int finger = 0; finger < finger_names.size(); ++finger) {
        region_grid->addWidget(new QLabel(finger_names.at(finger), region_status_host_), finger + 1, 0);
        // 拇指只有协议区域 1（指尖）和 2（指根），指根放在第 3 列与其它手指对齐。
        const QVector<int> columns = finger == 0 ? QVector<int>{ 0, 2 } : QVector<int>{ 0, 1, 2 };
        for (int col : columns) {
            auto *cell = new QLabel(region_status_host_);
            cell->setAlignment(Qt::AlignCenter);
            cell->setMinimumWidth(64);
            region_grid->addWidget(cell, finger + 1, col + 1);
            region_status_cells_[bit++] = cell;
        }
    }
    auto *thumb_middle = new QLabel("—", region_status_host_);
    thumb_middle->setAlignment(Qt::AlignCenter);
    thumb_middle->setStyleSheet("color:#6b7280;");
    region_grid->addWidget(thumb_middle, 1, 2);
    region_grid->setColumnStretch(4, 1);
    reset_region_status_cells();
    region_status_title_->setVisible(false); // 只在强脑型号下显示，见 product_model_changed()
    region_status_host_->setVisible(false);

    grid->addWidget(new QLabel("固件版本：", grid_host), 0, 0);
    grid->addWidget(firmware_version_label_, 0, 1);

    grid->addWidget(new QLabel("序列号：", grid_host), 1, 0);
    grid->addWidget(serial_number_label_, 1, 1);
    grid->addWidget(serial_number_read_button_, 1, 2);

    grid->addWidget(new QLabel("设备ID：", grid_host), 2, 0);
    grid->addWidget(sensor_id_label_, 2, 1);
    grid->addWidget(sensor_id_read_button_, 2, 2);
    grid->addWidget(new QLabel("修改ID：", grid_host), 2, 3);
    grid->addWidget(sensor_modify_id_spinbox_, 2, 4);
    grid->addWidget(sensor_modify_id_button_, 2, 5);

    grid->addWidget(new QLabel("是否标定：", grid_host), 3, 0);
    grid->addWidget(calibration_status_label_, 3, 1, 1, 4);
    grid->addWidget(calibration_query_button_, 3, 5);

    grid->addWidget(region_status_title_, 4, 0, Qt::AlignTop);
    grid->addWidget(region_status_host_, 4, 1, 1, 5);
    grid->setColumnStretch(6, 1);

    layout->addWidget(grid_host);
    layout->addWidget(create_function_test_group(group));
    layout->addWidget(create_calibration_test_group(group));
    return group;
}

QWidget *SensorWidget::create_function_test_group(QWidget *parent)
{
    auto *group  = new QGroupBox("功能测试", parent);
    auto *layout = new QGridLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    self_check_button_ = new QPushButton("开始自检", group);

    // 按需求删除 UI 上的“选择对应ID”和“传感器切换”。
    // 标定/测试流程仍通过内部 collect_part_protocol_id_ 与 sensor_data_by_id_
    // 自动按 0x11/0x21/0x31/0x41/0x51/0x61 取对应 ID 的矩阵。
    sensor_choose_id_spinbox_ = nullptr;
    sensor_switch_button_     = nullptr;

    layout->addWidget(new QLabel("设备自检：", group), 0, 0);
    layout->addWidget(self_check_button_, 0, 1);
    layout->setColumnStretch(2, 1);
    return group;
}

QWidget *SensorWidget::create_calibration_test_group(QWidget *parent)
{
    auto *group  = new QGroupBox("标定测试", parent);
    auto *layout = new QGridLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    pressure_input_ = new QDoubleSpinBox(group);
    pressure_input_->setRange(-100000.0, 100000.0);
    pressure_input_->setDecimals(3);
    pressure_input_->setValue(52.0);
    pressure_input_->setSuffix(" g");
    pressure_input_->setMinimumWidth(140);

    // 这里沿用 pressure_step_input_ 成员变量，界面含义改为“杆克重”。
    // 后续荷载增量从 resources/config/load_steps.csv 读取。
    pressure_step_input_ = new QDoubleSpinBox(group);
    pressure_step_input_->setRange(0.0, 100000.0);
    pressure_step_input_->setDecimals(3);
    pressure_step_input_->setValue(52.0);
    pressure_step_input_->setSuffix(" g");
    pressure_step_input_->setMinimumWidth(140);
    // 默认从杆克重开始；保存 CSV 时会自动补写 force=0、diff_ad=0 的零挡位。
    pressure_input_->setValue(52.0);

    collect_button_       = new QPushButton("采集数据", group);
    collect_part_spinbox_ = new QSpinBox(group);
    collect_part_spinbox_->setRange(0x11, 0x61);
    collect_part_spinbox_->setSingleStep(0x10);
    collect_part_spinbox_->setDisplayIntegerBase(16);
    collect_part_spinbox_->setPrefix("0x");
    collect_part_spinbox_->setValue(0x11);
    collect_part_spinbox_->setMinimumWidth(100);
    collect_region_spinbox_ = new QSpinBox(group);
    collect_region_spinbox_->setRange(1, 1);
    collect_region_spinbox_->setValue(1);
    collect_region_spinbox_->setMinimumWidth(100);
    collect_region_spinbox_->setEnabled(false);
    switch_collect_part_button_ = new QPushButton("切换部位", group);
    collect_area_spinbox_       = new QSpinBox(group);
    collect_area_spinbox_->setRange(1, 15);
    collect_area_spinbox_->setValue(1);
    collect_area_spinbox_->setMinimumWidth(100);
    reset_collect_point_button_ = new QPushButton("重置该点位", group);
    switch_collect_area_button_ = new QPushButton("切换区域", group);
    collected_points_label_     = new QLabel(group);
    collected_points_label_->setTextFormat(Qt::RichText);
    collected_points_label_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                     Qt::TextSelectableByKeyboard);
    send_button_                 = new QPushButton("标定传感器", group);
    re_calibration_button_       = new QPushButton("重新标定", group);
    test_button_                 = new QPushButton("测试", group);
    save_test_result_button_     = new QPushButton("切换测试区域", group);

    error_percent_input_ = new QDoubleSpinBox(group);
    error_percent_input_->setRange(0.0, 100.0);
    error_percent_input_->setDecimals(2);
    error_percent_input_->setValue(15.0);
    error_percent_input_->setSuffix(" %");
    error_percent_input_->setMinimumWidth(110);

    fs_error_percent_input_ = new QDoubleSpinBox(group);
    fs_error_percent_input_->setRange(0.0, 100.0);
    fs_error_percent_input_->setDecimals(2);
    fs_error_percent_input_->setValue(5.0);
    fs_error_percent_input_->setSuffix(" %FS");
    fs_error_percent_input_->setMinimumWidth(110);

    // 测试/标定日志显示区域已按需求删除：不再在界面固定区域打印运行日志，
    // 测试结果只写入 CSV，必要提示使用 toast 或弹窗。
    test_result_label_ = nullptr;

    layout->addWidget(new QLabel("输入压力：", group), 0, 0);
    layout->addWidget(pressure_input_, 0, 1);
    layout->addWidget(new QLabel("杆克重：", group), 0, 2);
    layout->addWidget(pressure_step_input_, 0, 3);
    collect_point_label_ = new QLabel("采集点位：", group);
    layout->addWidget(collect_point_label_, 1, 0);
    layout->addWidget(collect_area_spinbox_, 1, 1);
    layout->addWidget(reset_collect_point_button_, 1, 2);
    layout->addWidget(new QLabel("采集部位ID：", group), 1, 3);
    layout->addWidget(collect_part_spinbox_, 1, 4);
    layout->addWidget(new QLabel("上传区域：", group), 1, 5);
    layout->addWidget(collect_region_spinbox_, 1, 6);

    layout->addWidget(collect_button_, 2, 1);
    layout->addWidget(switch_collect_area_button_, 2, 2);
    layout->addWidget(switch_collect_part_button_, 2, 4);

    layout->addWidget(send_button_, 3, 1);
    layout->addWidget(re_calibration_button_, 3, 2);

    collected_points_title_ = new QLabel("已采集点位：", group);
    layout->addWidget(collected_points_title_, 4, 0);
    layout->addWidget(collected_points_label_, 4, 1, 1, 6);

    layout->addWidget(new QLabel("误差设置：", group), 5, 0);
    layout->addWidget(error_percent_input_, 5, 1);
    layout->addWidget(new QLabel("满量程误差设置：", group), 5, 2);
    layout->addWidget(fs_error_percent_input_, 5, 3);

    layout->addWidget(test_button_, 6, 1);
    layout->addWidget(save_test_result_button_, 6, 2);

    update_collected_points_ui();

    layout->setColumnStretch(7, 1);
    return group;
}

void SensorWidget::init_connections()
{
    connect(path_set_button_, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择保存路径", current_save_path_.isEmpty() ? QDir::homePath() : current_save_path_,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (dir.isEmpty()) return;
        current_save_path_ = dir;
        if (save_path_value_label_) save_path_value_label_->setText(current_save_path_);
        update_collected_points_ui();
        logi("Save path changed: {}", current_save_path_.toStdString());
    });

    connect(serial_number_read_button_, &QPushButton::clicked, this, [this]() {
        logi("Read serial number requested");
        if (!read_serial_number_once(500)) {
            QMessageBox::warning(this, "提示", "未读取到序列号");
        }
    });

    connect(sensor_id_read_button_, &QPushButton::clicked, this, [this]() {
        logi("Read device ID requested");
        if (!read_device_address_once(500)) {
            QMessageBox::warning(this, "错误", "读取设备ID失败");
            return;
        }
        query_calibration_state_from_device(500);
    });

    connect(calibration_query_button_, &QPushButton::clicked, this, [this]() {
        bind_device_to_collect_part_id();
        const int region_count  = std::max(1, Utils::get_region_count_by_csv(collect_part_protocol_id_));
        region_status_text_     = QStringList(region_count, QString());
        region_calibrated_      = QVector<bool>(region_count, false);
        region_query_received_  = QVector<bool>(region_count, false);
        region_query_succeeded_ = QVector<bool>(region_count, false);
        calibration_query_received_ = false;
        calibration_state_value_    = 0U;
        const uint8_t wire_part     = static_cast<uint8_t>((collect_part_protocol_id_ >> 4) & 0x0f);
        // 界面只显示“已标定 / 未标定 / 未查询”三种结果；查询过程保持“未查询”。
        set_calibration_status_ui(app::CalibrationStatus::Unknown);
        logi("Part calibration status query requested: protocol_id=0x{:02X}, wire_part={}",
             collect_part_protocol_id_, wire_part);
        emit query_part_calibration_status(wire_part);
    });

    connect(sensor_modify_id_button_, &QPushButton::clicked, this, [this]() {
        const int id      = sensor_modify_id_spinbox_ ? sensor_modify_id_spinbox_->value() : 0;
        address_received_ = false;
        logi("Modify device ID requested: new_id=0x{:02X}", id);
        emit modify_address_requested(static_cast<uint8_t>(id));
        // 修改后立即按读取设备ID的流程回读一次，用于刷新界面并确认设备已切到新地址。
        if (!read_device_address_once(500)) {
            QMessageBox::warning(this, "错误", "修改设备ID后回读失败，请确认设备当前ID或重新连接");
            return;
        }
        query_calibration_state_from_device(500);
    });

    connect(self_check_button_, &QPushButton::clicked, this, [this]() { run_self_check(); });

    // “传感器切换”按钮已从 UI 删除，不再提供手动切换入口。

    connect(collect_button_, &QPushButton::clicked, this, [this]() { collect_current_sample(); });

    connect(reset_collect_point_button_, &QPushButton::clicked, this,
            [this]() { reset_current_collect_point(); });

    connect(switch_collect_area_button_, &QPushButton::clicked, this,
            [this]() { switch_collect_area_to_next(); });

    connect(switch_collect_part_button_, &QPushButton::clicked, this,
            [this]() { switch_collect_part_to_next(); });

    connect(collect_part_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        // 关键修复：部位 ID 必须以本次触发的 value 为准，不能在循环/异步回调中复用最后一个值。
        // set_collect_part_id() 会同步 UI 部位 ID 与真实协议 ID：0x11/0x21/.../0x61。
        set_collect_part_id(value, true);
    });

    connect(collect_region_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        // MCU continuously reports all five fingers; region selection only changes the host-side view.
        set_collect_region_id(value, false);
    });

    connect(collect_area_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        collect_area_index_ = value;
        pending_collect_samples_.clear();
        reset_collect_zero_baseline();
        reset_pressure_to_default();
        update_collected_points_ui();
        check_current_point_duplicate_hint();
    });

    connect(pressure_step_input_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                // 杆克重修改后，采集/测试默认均回到杆重档。
                // 后续档位按 杆重、杆重+resources/config/load_steps.csv 增量自动生成。
                reset_pressure_to_default();
                reset_test_pressure_sequence();
            });

    connect(test_button_, &QPushButton::clicked, this, [this]() { run_calibration_test_once(); });

    connect(save_test_result_button_, &QPushButton::clicked, this,
            [this]() { switch_test_area_to_next(); });

    connect(send_button_, &QPushButton::clicked, this, [this]() { send_all_parts_lut_to_device(); });

    connect(re_calibration_button_, &QPushButton::clicked, this, [this]() {
        if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) {
            // 强脑："重新标定"擦除 MCU 中全部 14 个区域的 LUT。本地采集 CSV 不删除，
            // 之后点"标定传感器"即可按已有采集数据逐区域重新下发。
            const auto answer = QMessageBox::question(
                this, QStringLiteral("重新标定"),
                QStringLiteral("将擦除设备中全部 14 个区域的标定 LUT，擦除后设备上传原始 AD。\n"
                               "本地采集 CSV 会保留，之后点“标定传感器”可重新下发。\n\n确定继续？"));
            if (answer != QMessageBox::Yes) return;
            emit set_device_active_query(false);
            emit re_calibration_requested(0U);
            emit set_device_active_query(true);
            showToast(QStringLiteral("已请求擦除全部 14 个区域的标定 LUT（本地采集 CSV 已保留），结果见区域状态"));
            return;
        }
        // 其它产品："重新标定"只擦除当前选择部位的 LUT，不影响其它部位。
        bind_device_to_collect_part_id();
        const uint8_t wire_part = static_cast<uint8_t>((collect_part_protocol_id_ >> 4) & 0x0f);
        emit          set_device_active_query(false);
        emit          re_calibration_requested(wire_part);
        emit          set_device_active_query(true);
        showToast(QStringLiteral("已请求擦除部位 0x%1 的标定 LUT")
                      .arg(collect_part_protocol_id_, 2, 16, QLatin1Char('0'))
                      .toUpper());
    });
}

void SensorWidget::setData(uint32_t id, const touch::matrix<uint16_t>& m)
{
    {
        std::unique_lock lock(data_mutex_);
        sensor_data_by_id_[id] = m;

        // sensor_data_ 始终保持“当前选择部位 ID 对应的矩阵”。
        // 这样采集、测试、零点基准、实时质心显示都不会再误用最后一帧或固定 0x61 矩阵。
        const uint32_t current_protocol_id = product_type_ == ProductModel::BRAINCO_GLOVE_V2
                                                 ? static_cast<uint32_t>((collect_part_protocol_id_ & 0xF0) | 1)
                                                 : static_cast<uint32_t>(collect_part_protocol_id_);
        if (id == current_protocol_id || sensor_data_.empty()) {
            sensor_data_ = m;
        }
    }
    update_current_centroid_display(matrix_for_current_part(m));
}

void SensorWidget::setBridgeData(uint16_t positive, uint16_t negative)
{
    {
        std::unique_lock lock(data_mutex_);
        bridge_positive_   = positive;
        bridge_negative_   = negative;
        bridge_data_valid_ = true;
    }
}

void SensorWidget::set_firmware_version(std::string ver)
{
    firmware_version_  = std::move(ver);
    firmware_received_ = !firmware_version_.empty();
    if (firmware_version_label_) {
        firmware_version_label_->setText(firmware_received_ ? QString::fromStdString(firmware_version_)
                                                            : "NULL");
    }
    logi("Firmware version updated: {}", firmware_version_);
}

void SensorWidget::set_sn(std::string sn)
{
    current_sn_      = QString::fromStdString(sn).trimmed();
    serial_received_ = !current_sn_.isEmpty() && current_sn_ != QStringLiteral("(Unknown)");
    if (current_sn_.isEmpty()) current_sn_ = QStringLiteral("NO_SN");
    if (serial_number_label_) serial_number_label_->setText(current_sn_);
    update_collected_points_ui();
    logi("Serial number updated: {}", current_sn_.toStdString());
}

void SensorWidget::set_address(int id)
{
    // 0 是合法设备地址；0xFF 保留为未读取/读取失败的无效值。
    address_received_ = (id >= 0 && id < 0xff);
    address_id_       = address_received_ ? id : -1;

    if (sensor_id_label_) {
        sensor_id_label_->setText(address_received_ ? QString::number(id) : QStringLiteral("null"));
    }
    if (sensor_choose_id_spinbox_ && address_received_) {
        sensor_choose_id_spinbox_->blockSignals(true);
        sensor_choose_id_spinbox_->setValue(id);
        sensor_choose_id_spinbox_->blockSignals(false);
    }
    if (sensor_modify_id_spinbox_ && address_received_) {
        sensor_modify_id_spinbox_->blockSignals(true);
        sensor_modify_id_spinbox_->setValue(id);
        sensor_modify_id_spinbox_->blockSignals(false);
    }
    if (address_received_) {
        logi("Device address updated: 0x{:02X}, valid={}", id, true);
    }
    else {
        logw("Device address read failed or invalid: raw=0x{:02X}", id);
    }
}

void SensorWidget::set_calibration_state(uint8_t calibrated)
{
    calibration_state_value_    = calibrated;
    calibration_query_received_ = true;
    set_calibration_status_ui(app::calibrationStatusFromDeviceReply(calibrated));
    logi("Calibration state updated from device reply: raw={}, text={}", static_cast<int>(calibrated),
         app::calibrationStatusLogText(app::calibrationStatusFromDeviceReply(calibrated)));
}

void SensorWidget::set_lut_download_result(uint8_t part_id, bool success, uint8_t status, uint32_t crc32,
                                           uint32_t version, const QString& error)
{
    last_lut_download_success_ = success;
    last_lut_download_error_   = error;
    if (success) {
        logi("LUT 事务提交成功：part={}, status=0x{:02X}, crc32=0x{:08X}, version={}", part_id, status,
             crc32, version);
    }
    else {
        loge("LUT 事务提交失败：part={}, status=0x{:02X}, error={}", part_id, status, error.toStdString());
    }
}

void SensorWidget::set_part_calibration_status(uint8_t part_id, uint8_t region_id, bool success,
                                               uint8_t status, uint8_t points, uint8_t mode, uint32_t crc32,
                                               uint32_t version, const QString& error)
{
    const int protocol_id  = static_cast<int>(part_id) << 4 | 0x01;
    const int region_count = std::max(1, Utils::get_region_count_by_csv(protocol_id));
    if (region_calibrated_.size() != region_count) region_calibrated_ = QVector<bool>(region_count, false);
    if (region_query_received_.size() != region_count)
        region_query_received_ = QVector<bool>(region_count, false);
    if (region_query_succeeded_.size() != region_count)
        region_query_succeeded_ = QVector<bool>(region_count, false);

    if (region_id >= 1U && region_id <= static_cast<uint8_t>(region_count)) {
        const int  index                   = static_cast<int>(region_id) - 1;
        // 兼容旧固件重启后的状态响应：手掌会恢复已持久化的 CRC/版本，
        // 但查询帧中的 staging point count 会是 0。点数或持久化元数据任一有效即表示该区域有 LUT。
        const bool has_persisted_lut        = points >= 2U || crc32 != 0U || version != 0U;
        const bool calibrated               = success && status == 0U && has_persisted_lut;
        region_query_received_[index]      = true;
        region_query_succeeded_[index]     = success;
        region_calibrated_[region_id - 1U] = calibrated;
    }

    const bool all_received  = std::all_of(region_query_received_.cbegin(), region_query_received_.cend(),
                                           [](bool v) { return v; });
    const bool all_succeeded = std::all_of(region_query_succeeded_.cbegin(), region_query_succeeded_.cend(),
                                           [](bool v) { return v; });
    const bool all_calibrated =
        std::all_of(region_calibrated_.cbegin(), region_calibrated_.cend(), [](bool v) { return v; });

    if (protocol_id == collect_part_protocol_id_) {
        calibration_query_received_ = all_received && all_succeeded;
        calibration_state_value_    = (calibration_query_received_ && all_calibrated) ? 1U : 0U;
        if (!all_received || !all_succeeded) {
            set_calibration_status_ui(app::CalibrationStatus::Unknown);
        }
        else {
            set_calibration_status_ui(all_calibrated ? app::CalibrationStatus::Calibrated
                                                     : app::CalibrationStatus::NotCalibrated);
        }
    }

    if (!success) {
        logw("Part calibration status query failed: part={}, region={}, status=0x{:02X}, error={}", part_id,
             region_id, status, error.toStdString());
        return;
    }
    logi(
        "Part calibration status: part={}, region={}, calibrated={}, status=0x{:02X}, points={}, mode={}, crc32=0x{:08X}, version={}",
        part_id, region_id, success && status == 0U && (points >= 2U || crc32 != 0U || version != 0U),
        status, points, mode,
        crc32, version);
}

void SensorWidget::product_model_changed(uint8_t idx)
{
    product_type_      = productModelFromId(idx);
    bridge_data_valid_ = false;
    bridge_positive_   = 0;
    bridge_negative_   = 0;
    if (product_model_label_) product_model_label_->setText(productModelToString(product_type_));
    const bool brainco = product_type_ == ProductModel::BRAINCO_GLOVE_V2;
    if (collect_point_label_) collect_point_label_->setVisible(!brainco);
    if (collect_area_spinbox_) collect_area_spinbox_->setVisible(!brainco);
    if (reset_collect_point_button_) reset_collect_point_button_->setVisible(!brainco);
    if (collected_points_title_)
        collected_points_title_->setText(brainco ? QStringLiteral("采集状态：") : QStringLiteral("已采集点位："));
    if (brainco && collect_area_spinbox_ && collect_area_spinbox_->value() != 1)
        collect_area_spinbox_->setValue(1);
    if (collect_part_spinbox_) collect_part_spinbox_->setMaximum(brainco ? 0x51 : 0x61);
    if (collect_region_spinbox_) collect_region_spinbox_->setEnabled(brainco);
    // 强脑固件已支持 STATUS_ALL 查询 14 个区域和按区域 ERASE，两个按钮对所有型号都可用。
    if (calibration_query_button_) calibration_query_button_->setEnabled(true);
    if (re_calibration_button_) re_calibration_button_->setEnabled(true);
    if (region_status_title_) region_status_title_->setVisible(brainco);
    if (region_status_host_) region_status_host_->setVisible(brainco);
    if (brainco && collect_part_id_ > 0x51) set_collect_part_id(0x51, false);
    set_collect_region_id(brainco ? collect_region_id_ : 1, false);
    logi("Product model changed: {} ({})", static_cast<int>(idx),
         productModelToString(product_type_).toStdString());
}

void SensorWidget::reset_region_status_cells()
{
    for (QLabel *cell : region_status_cells_) {
        if (!cell) continue;
        cell->setText(QStringLiteral("未查询"));
        cell->setStyleSheet("color:#facc15; font-weight:bold;");
        cell->setToolTip(QString());
    }
}

void SensorWidget::set_region_mask_status(const CalibrationRegionMaskResult& result)
{
    calibration_query_received_ = result.success;
    calibration_state_value_    = result.all_calibrated() ? 1U : 0U;

    if (!result.success) {
        reset_region_status_cells();
        set_calibration_status_ui(app::CalibrationStatus::Unknown);
        if (calibration_status_label_) calibration_status_label_->setToolTip(result.error);
        logw("Region calibration status query failed: {}", result.error.toStdString());
        return;
    }

    int calibrated_count = 0;
    for (size_t bit = 0U; bit < region_status_cells_.size(); ++bit) {
        const bool calibrated = ((result.calibrated_mask >> bit) & 1U) != 0U;
        const bool force      = ((result.force_mask >> bit) & 1U) != 0U;
        if (calibrated) ++calibrated_count;

        QLabel *cell = region_status_cells_[bit];
        if (!cell) continue;
        cell->setText(calibrated ? QStringLiteral("已标定") : QStringLiteral("未标定"));
        cell->setStyleSheet(calibrated ? "color:#4ade80; font-weight:bold;" : "color:#f87171; font-weight:bold;");
        cell->setToolTip(calibrated ? (force ? QStringLiteral("力值模式") : QStringLiteral("原始 AD 模式"))
                                    : QString());
    }

    // "是否标定"仍保持三态：14 个区域全部完成才算已标定，明细看区域格子。
    set_calibration_status_ui(result.all_calibrated() ? app::CalibrationStatus::Calibrated
                                                      : app::CalibrationStatus::NotCalibrated);
    if (calibration_status_label_) {
        calibration_status_label_->setToolTip(QStringLiteral("14 个区域中已标定 %1 个").arg(calibrated_count));
    }
    logi("Region calibration status: calibrated_mask=0x{:04X}, force_mask=0x{:04X}, calibrated={}/14",
         result.calibrated_mask, result.force_mask, calibrated_count);
}

void SensorWidget::set_calibration_status_ui(app::CalibrationStatus state)
{
    if (!calibration_status_label_) return;

    // 产品界面只允许三态。Querying 仅作为内部状态，显示时归并为“未查询”。
    calibration_status_label_->setText(state == app::CalibrationStatus::Querying
                                           ? QStringLiteral("未查询")
                                           : app::calibrationStatusText(state));

    switch (state) {
    case app::CalibrationStatus::Unknown:
        calibration_status_label_->setObjectName("statusUnknown");
        calibration_status_label_->setStyleSheet("color:#facc15; font-weight:bold;");
        break;
    case app::CalibrationStatus::Querying:
        calibration_status_label_->setObjectName("statusUnknown");
        calibration_status_label_->setStyleSheet("color:#7dd3fc; font-weight:bold;");
        break;
    case app::CalibrationStatus::Calibrated:
        calibration_status_label_->setObjectName("statusOk");
        calibration_status_label_->setStyleSheet("color:#4ade80; font-weight:bold;");
        break;
    case app::CalibrationStatus::NotCalibrated:
        calibration_status_label_->setObjectName("statusBad");
        calibration_status_label_->setStyleSheet("color:#f87171; font-weight:bold;");
        break;
    }
}

bool SensorWidget::query_calibration_state_from_device(int timeout_ms)
{
    calibration_query_received_ = false;
    calibration_state_value_    = 0U;
    const int region_count      = std::max(1, Utils::get_region_count_by_csv(collect_part_protocol_id_));
    region_calibrated_          = QVector<bool>(region_count, false);
    region_query_received_      = QVector<bool>(region_count, false);
    region_query_succeeded_     = QVector<bool>(region_count, false);
    set_calibration_status_ui(app::CalibrationStatus::Unknown);
    const uint8_t wire_part = static_cast<uint8_t>((collect_part_protocol_id_ >> 4) & 0x0f);
    logi("Sending part calibration query: protocol_id=0x{:02X}, regions={}", collect_part_protocol_id_,
         region_count);

    emit query_part_calibration_status(wire_part);

    if (!calibration_query_received_) {
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [this, &loop]() {
            if (calibration_query_received_) loop.quit();
        });
        poll.start(10);
        timer.start(timeout_ms);
        loop.exec();
        poll.stop();
    }

    if (!calibration_query_received_) {
        set_calibration_status_ui(app::CalibrationStatus::Unknown);
        logw("Calibration query timeout: no device reply within {} ms", timeout_ms);
        return false;
    }

    logi("Part calibration query finished: protocol_id=0x{:02X}, calibrated={}", collect_part_protocol_id_,
         calibration_state_value_ != 0U);
    return true;
}

bool SensorWidget::read_device_address_once(int timeout_ms)
{
    address_received_ = false;
    logi("Sending read_address command to device");
    emit read_address_signals();

    if (!address_received_) {
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [this, &loop]() {
            if (address_received_) loop.quit();
        });
        poll.start(10);
        timer.start(timeout_ms);
        loop.exec();
        poll.stop();
    }

    if (address_received_) {
        logi("Read address result: success={}, address=0x{:02X}", true, address_id_);
    }
    else {
        logw("Read address result: success=false, address=null");
    }
    return address_received_;
}

bool SensorWidget::read_serial_number_once(int timeout_ms)
{
    serial_received_ = false;
    logi("Sending read_serial_number command to device");
    emit read_sn_signals();

    if (!serial_received_) {
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [this, &loop]() {
            if (serial_received_) loop.quit();
        });
        poll.start(10);
        timer.start(timeout_ms);
        loop.exec();
        poll.stop();
    }

    logi("Read serial number result: success={}, sn={}", serial_received_, current_sn_.toStdString());
    return serial_received_;
}

bool SensorWidget::read_firmware_once(int timeout_ms)
{
    firmware_received_ = false;
    logi("Sending read_firmware command to device");
    emit read_firmsoft_signals();

    if (!firmware_received_) {
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QTimer poll;
        connect(&poll, &QTimer::timeout, &loop, [this, &loop]() {
            if (firmware_received_) loop.quit();
        });
        poll.start(10);
        timer.start(timeout_ms);
        loop.exec();
        poll.stop();
    }

    logi("Read firmware result: success={}, version={}", firmware_received_, firmware_version_);
    return firmware_received_;
}

void SensorWidget::run_self_check()
{
    if (!self_check_button_) return;

    self_check_button_->setEnabled(false);
    self_check_button_->setText("自检中");

    // 自检始终以 UI 当前选择的部位为通信目标，不能沿用上一次查询留下的 slave id。
    bind_device_to_collect_part_id();
    firmware_version_.clear();
    current_sn_.clear();
    address_id_        = -1;
    address_received_  = false;
    serial_received_   = false;
    firmware_received_ = false;
    if (firmware_version_label_) firmware_version_label_->setText("NULL");
    if (serial_number_label_) serial_number_label_->setText("NULL");
    if (sensor_id_label_) sensor_id_label_->setText("null");

    logi("Self check started: current_part=0x{:02X}", collect_part_protocol_id_);

    const bool firmware_ok    = read_firmware_once(500);
    const bool sn_ok          = read_serial_number_once(1500);
    const bool address_ok     = read_device_address_once(500);
    const bool calibration_ok = query_calibration_state_from_device(1500);

    self_check_button_->setText("开始自检");
    self_check_button_->setEnabled(true);

    logi(
        "Self check finished: current_part=0x{:02X}, firmware_ok={}, firmware={}, sn_ok={}, sn={}, address_ok={}, address=0x{:02X}, calibration_query_ok={}, calibrated={}",
        collect_part_protocol_id_, firmware_ok, firmware_version_, sn_ok, current_sn_.toStdString(),
        address_ok, address_ok ? address_id_ : 0xff, calibration_ok, calibration_state_value_ != 0U);

    if (address_ok && firmware_ok && sn_ok && calibration_ok) {
        QMessageBox::information(this, "完成", "自检完成");
    }
    else {
        QMessageBox::warning(this, "提示", "自检完成，但存在未成功读取的项目，请查看日志");
    }
}

void SensorWidget::reset_device_info_ui()
{
    firmware_version_.clear();
    current_sn_.clear();
    address_id_                 = -1;
    address_received_           = false;
    serial_received_            = false;
    firmware_received_          = false;
    calibration_query_received_ = false;
    calibration_state_value_    = 0;
    region_status_text_.clear();
    region_calibrated_.clear();
    region_query_received_.clear();
    region_query_succeeded_.clear();

    if (firmware_version_label_) firmware_version_label_->setText("NULL");
    if (serial_number_label_) serial_number_label_->setText("NULL");
    if (sensor_id_label_) sensor_id_label_->setText("null");
    if (sensor_modify_id_spinbox_) sensor_modify_id_spinbox_->setValue(0);
    set_calibration_status_ui(app::CalibrationStatus::Unknown);
    logi("Device information UI reset");
}

touch::matrix<uint16_t> SensorWidget::matrix_for_current_part(const touch::matrix<uint16_t>& fallback) const
{
    touch::matrix<uint16_t> selected;
    {
        std::shared_lock lock(data_mutex_);
        const uint32_t protocol_id = product_type_ == ProductModel::BRAINCO_GLOVE_V2
                                         ? static_cast<uint32_t>((collect_part_protocol_id_ & 0xF0) | 0x01)
                                         : static_cast<uint32_t>(collect_part_protocol_id_);
        auto it = sensor_data_by_id_.find(protocol_id);
        if (it != sensor_data_by_id_.end() && !it->second.empty()) selected = it->second;
        else if (!sensor_data_.empty()) selected = sensor_data_;
    }
    if (selected.empty()) selected = fallback;
    return product_type_ == ProductModel::BRAINCO_GLOVE_V2
               ? braincoRegionMatrix(selected, collect_part_id_, collect_region_id_)
               : selected;
}

touch::matrix<uint16_t> SensorWidget::matrix_for_part_id(int                            part_id,
                                                         const touch::matrix<uint16_t>& fallback) const
{
    std::shared_lock lock(data_mutex_);

    uint32_t protocol_id = static_cast<uint32_t>(collect_part_protocol_id_for_part(part_id));
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) protocol_id = (protocol_id & 0xF0U) | 0x01U;
    auto           it          = sensor_data_by_id_.find(protocol_id);
    if (it != sensor_data_by_id_.end() && !it->second.empty()) {
        return it->second;
    }

    return fallback;
}

app::CaptureSample SensorWidget::make_capture_sample(const touch::matrix<uint16_t>& m, double force,
                                                     bool enable_log) const
{
    // 标定/测试必须使用当前选择的部位 ID 对应矩阵，而不是固定 0x61 或最后一帧。
    const touch::matrix<uint16_t> selected_matrix = matrix_for_current_part(m);

    // 新规则：抛弃原来的 diff_ad 计算方式，不再使用 matrix[0]-matrix[1] 或 OP3 电桥差分。
    // 为兼容后续建表/CSV 字段名，仍写入 sample.diff_ad，但其语义改为“当前 ID 矩阵总值”。
    double matrix_sum = 0.0;
    if (!selected_matrix.empty()) {
        const auto *data = selected_matrix.ptr();
        for (int i = 0; i < selected_matrix.size(); ++i) {
            matrix_sum += static_cast<double>(data[i]);
        }
    }

    return app::CalibrationCaptureService::calculateSample(selected_matrix, force, enable_log, true,
                                                           matrix_sum, false);
}

app::CaptureSample SensorWidget::make_capture_sample_for_part(int part_id, const touch::matrix<uint16_t>& m,
                                                              double force, bool enable_log) const
{
    const touch::matrix<uint16_t> selected_matrix = matrix_for_part_id(part_id, m);

    double matrix_sum = 0.0;
    if (!selected_matrix.empty()) {
        const auto *data = selected_matrix.ptr();
        for (int i = 0; i < selected_matrix.size(); ++i) {
            matrix_sum += static_cast<double>(data[i]);
        }
    }

    return app::CalibrationCaptureService::calculateSample(selected_matrix, force, enable_log, true,
                                                           matrix_sum, false);
}

void SensorWidget::update_current_centroid_display(const touch::matrix<uint16_t>& m)
{
    if (!current_centroid_label_) return;
    const auto sample = make_capture_sample(m, pressure_input_ ? pressure_input_->value() : 0.0, false);
    if (!sample.valid) {
        current_centroid_label_->setText(
            QStringLiteral("当前质心：cx=0.000000, cy=0.000000    矩阵总值=%1    未触发")
                .arg(sample.diff_ad, 0, 'f', 6));
        return;
    }
    current_centroid_label_->setText(QStringLiteral("当前质心：cx=%1, cy=%2    矩阵总值=%3")
                                         .arg(sample.cx, 0, 'f', 6)
                                         .arg(sample.cy, 0, 'f', 6)
                                         .arg(sample.diff_ad, 0, 'f', 6));
}

QString SensorWidget::current_calibration_csv_path() const
{
    // 强脑每个区域固定使用一个采集文件 lut_<SN>_id_XY_area_01.csv，不再区分采集点位。
    const bool brainco = product_type_ == ProductModel::BRAINCO_GLOVE_V2;
    return calibration_csv_path_for_point(brainco ? 1 : collect_area_index_);
}

bool SensorWidget::force_in_millinewton() const
{
    return product_type_ == ProductModel::BRAINCO_GLOVE_V2;
}

QString SensorWidget::force_unit() const
{
    return force_in_millinewton() ? QStringLiteral("mN") : QStringLiteral("g");
}

int SensorWidget::collect_part_protocol_id_for_part(int part_id) const
{
    // UI 部位 ID 与真实设备/协议 ID 固定映射：
    // 11 -> 0x11, 21 -> 0x21, 31 -> 0x31, 41 -> 0x41, 51 -> 0x51, 61 -> 0x61。
    // 同时兼容旧输入 1~6：1 -> 0x11, 2 -> 0x21, ..., 6 -> 0x61。
    const int base = normalizeCollectPartId(part_id);
    return product_type_ == ProductModel::BRAINCO_GLOVE_V2 ? ((base & 0xF0) | collect_region_id_) : base;
}

int SensorWidget::collect_part_protocol_id() const
{
    // 不再每次临时由 spinbox 或循环变量推导，统一使用 set_collect_part_id() 写入的稳定状态。
    return collect_part_protocol_id_;
}

QString SensorWidget::collect_part_tag_for_protocol_id(int protocol_id) const
{
    return QStringLiteral("id_%1").arg(protocol_id, 2, 16, QLatin1Char('0')).toUpper();
}

QString SensorWidget::current_collect_part_tag() const
{
    return collect_part_tag_for_protocol_id(collect_part_protocol_id());
}

void SensorWidget::set_collect_part_id(int part_id, bool emit_switch_device)
{
    const int normalized_part_id   = normalizeCollectPartId(part_id);
    collect_region_id_ = std::clamp(collect_region_id_, 1, max_collect_region_for_part(normalized_part_id));
    const int protocol_id          = collect_part_protocol_id_for_part(normalized_part_id);
    const int previous_protocol_id = collect_part_protocol_id_;

    const bool changed =
        (collect_part_id_ != normalized_part_id) || (collect_part_protocol_id_ != protocol_id);

    collect_part_id_          = normalized_part_id;
    collect_part_protocol_id_ = protocol_id;

    if (calibration_status_label_) {
        set_calibration_status_ui(app::CalibrationStatus::Unknown);
    }

    if (collect_part_spinbox_ && collect_part_spinbox_->value() != normalized_part_id) {
        collect_part_spinbox_->blockSignals(true);
        collect_part_spinbox_->setValue(normalized_part_id);
        collect_part_spinbox_->blockSignals(false);
    }
    if (collect_region_spinbox_) {
        collect_region_spinbox_->blockSignals(true);
        collect_region_spinbox_->setRange(1, max_collect_region_for_part(normalized_part_id));
        collect_region_spinbox_->setValue(collect_region_id_);
        collect_region_spinbox_->blockSignals(false);
    }

    if (sensor_choose_id_spinbox_) {
        sensor_choose_id_spinbox_->blockSignals(true);
        sensor_choose_id_spinbox_->setValue(protocol_id);
        sensor_choose_id_spinbox_->blockSignals(false);
    }

    if (changed) {
        test_records_by_part_[previous_protocol_id] = test_records_;
        pending_collect_samples_.clear();
        test_records_        = test_records_by_part_[protocol_id];
        test_area_index_     = 1;
        max_test_area_index_ = std::max(1, Utils::get_region_count_by_csv(protocol_id));
        {
            // 在新部位第一帧到达前不得沿用上一部位的兼容矩阵。
            // 各部位缓存仍保留，matrix_for_current_part() 会优先取新部位自己的缓存。
            std::unique_lock lock(data_mutex_);
            sensor_data_ = touch::matrix<uint16_t>{};
        }
        reset_collect_zero_baseline();
        reset_pressure_to_default();
        reset_test_pressure_sequence();
        reset_device_info_ui();
    }

    if (emit_switch_device) {
        emit choose_id(static_cast<uint8_t>(protocol_id));
    }

    update_collected_points_ui();
    check_current_point_duplicate_hint();
    update_current_centroid_display(matrix_for_current_part());

    logi("Collect part selected: part_id={}, protocol_id=0x{:02X}", collect_part_id_, protocol_id);
    showToast(QStringLiteral("已选择采集部位 ID 0x%1，后续标定/测试绑定设备ID 0x%2")
                  .arg(collect_part_id_, 2, 16, QLatin1Char('0'))
                  .arg(protocol_id, 2, 16, QLatin1Char('0'))
                  .toUpper());
}

int SensorWidget::max_collect_region_for_part(int part_id) const
{
    if (product_type_ != ProductModel::BRAINCO_GLOVE_V2) return 1;
    return ((normalizeCollectPartId(part_id) >> 4) == 1) ? 2 : 3;
}

void SensorWidget::set_collect_region_id(int region_id, bool emit_switch_device)
{
    const int bounded = std::clamp(region_id, 1, max_collect_region_for_part(collect_part_id_));
    const int next_protocol_id = (collect_part_id_ & 0xF0) | bounded;
    const bool changed = collect_region_id_ != bounded || collect_part_protocol_id_ != next_protocol_id;
    const int previous_protocol_id = collect_part_protocol_id_;
    collect_region_id_ = bounded;
    collect_part_protocol_id_ = product_type_ == ProductModel::BRAINCO_GLOVE_V2
                                    ? next_protocol_id
                                    : normalizeCollectPartId(collect_part_id_);
    if (collect_region_spinbox_ && collect_region_spinbox_->value() != bounded) {
        collect_region_spinbox_->blockSignals(true);
        collect_region_spinbox_->setValue(bounded);
        collect_region_spinbox_->blockSignals(false);
    }
    if (changed) {
        test_records_by_part_[previous_protocol_id] = test_records_;
        test_records_ = test_records_by_part_[collect_part_protocol_id_];
        pending_collect_samples_.clear();
        reset_collect_zero_baseline();
        reset_pressure_to_default();
        {
            std::unique_lock lock(data_mutex_);
            sensor_data_ = touch::matrix<uint16_t>{};
        }
    }
    if (emit_switch_device) emit choose_id(static_cast<uint8_t>(collect_part_protocol_id_));
    update_collected_points_ui();
    update_current_centroid_display(matrix_for_current_part());
    logi("Collect upload region selected: region={}, protocol_id=0x{:02X}", collect_region_id_,
         collect_part_protocol_id_);
}

void SensorWidget::bind_device_to_collect_part_id()
{
    // 只绑定当前稳定状态，避免所有部位被错误地绑定成最后一次循环值 ID6。
    const int protocol_id = collect_part_protocol_id();
    if (sensor_choose_id_spinbox_) {
        sensor_choose_id_spinbox_->blockSignals(true);
        sensor_choose_id_spinbox_->setValue(protocol_id);
        sensor_choose_id_spinbox_->blockSignals(false);
    }
    emit choose_id(static_cast<uint8_t>(protocol_id));
    logi("Collect part bound to protocol id: part_id={}, protocol_id=0x{:02X}", collect_part_id_,
         protocol_id);
}

QString SensorWidget::calibration_csv_path_for_point(int point) const
{
    QString sn = current_sn_.trimmed();
    if (sn.isEmpty() || sn == QStringLiteral("null")) {
        sn = QStringLiteral("NO_SN");
    }

    // 新规则：采集数据按“采集部位映射后的协议ID(0x11/0x21/0x31/0x41/0x51/0x61) + 点位(1~15)”隔离。
    return QDir(current_save_path_)
        .filePath(QStringLiteral("lut_%1_%2_area_%3.csv")
                      .arg(sn, current_collect_part_tag())
                      .arg(point, 2, 10, QLatin1Char('0')));
}

bool SensorWidget::is_collect_point_file_exists(int point) const
{
    return calibration_csv_has_data_rows(calibration_csv_path_for_point(point));
}

bool SensorWidget::ensure_calibration_csv_file(const QString& file_path, QString *error_message) const
{
    QFileInfo info(file_path);
    QDir      dir = info.absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) {
        const QString err = QStringLiteral("无法创建保存路径：%1").arg(dir.absolutePath());
        if (error_message) *error_message = err;
        loge("Ensure calibration csv failed: cannot create dir={}", dir.absolutePath().toStdString());
        return false;
    }

    if (info.exists() && info.size() > 0) {
        return true;
    }

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString err = QStringLiteral("无法创建采集文件：%1").arg(file_path);
        if (error_message) *error_message = err;
        loge("Ensure calibration csv failed: cannot create file={}", file_path.toStdString());
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#endif
    out << "diff_ad,force,cx,cy,rows,cols,peak,active_count,area_eff,var_x,var_y\n";
    logi("Calibration csv ensured: file={}", file_path.toStdString());
    return true;
}

bool SensorWidget::calibration_csv_has_data_rows(const QString& file_path) const
{
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool        header_skipped = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (!header_skipped) {
            header_skipped = true;
            if (line.contains(QStringLiteral("diff_ad")) || line.contains(QStringLiteral("force"))) {
                continue;
            }
        }
        const QStringList cols = line.split(',');
        if (cols.size() >= 4) {
            return true;
        }
    }
    return false;
}

bool SensorWidget::read_centroid_from_csv(const QString& file_path, double *cx, double *cy) const
{
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool        header_skipped = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (!header_skipped) {
            header_skipped = true;
            if (line.contains(QStringLiteral("diff_ad")) || line.contains(QStringLiteral("force"))) {
                continue;
            }
        }
        const QStringList cols = line.split(',');
        if (cols.size() < 4) continue;
        bool         okf = false;
        bool         okx = false;
        bool         oky = false;
        const double f   = cols.at(1).trimmed().toDouble(&okf);
        const double x   = cols.at(2).trimmed().toDouble(&okx);
        const double y   = cols.at(3).trimmed().toDouble(&oky);
        // 自动补写的 0g 行通常为 cx=0,cy=0，不能用于重复点位判断。
        if (okf && f <= 1e-9) continue;
        if (okx && oky) {
            if (cx) *cx = x;
            if (cy) *cy = y;
            return true;
        }
    }
    return false;
}

int SensorWidget::find_duplicate_collected_point(double cx, double cy, int ignore_point) const
{
    // 质心来自触觉矩阵，存在噪声和量化误差，不能用完全相等判断。
    // 归一化坐标下欧氏距离小于 0.03 时，认为仍落在同一采集点位。
    constexpr double kCentroidDuplicateDistance = 0.03;
    // 强脑每个区域只有一个采集文件，不存在“同一点位重复采集”的判断。
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) return -1;
    for (int point = 1; point <= 15; ++point) {
        if (point == ignore_point) continue;
        const QString path = calibration_csv_path_for_point(point);
        if (!QFileInfo::exists(path)) continue;
        double old_cx = 0.0;
        double old_cy = 0.0;
        if (!read_centroid_from_csv(path, &old_cx, &old_cy)) continue;
        const double dx = cx - old_cx;
        const double dy = cy - old_cy;
        if (std::sqrt(dx * dx + dy * dy) < kCentroidDuplicateDistance) {
            return point;
        }
    }
    return -1;
}

void SensorWidget::update_collected_points_ui()
{
    if (!collected_points_label_) return;
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) {
        const bool collected = is_collect_point_file_exists(1);
        collected_points_label_->setText(QStringLiteral("区域 0x%1：%2（每区域一个采集文件，力值 mN）")
            .arg(collect_part_protocol_id(), 2, 16, QLatin1Char('0'))
            .arg(collected ? QStringLiteral("已采集") : QStringLiteral("未采集"))
            .toUpper());
        return;
    }

    QString html =
        QStringLiteral("部位ID 0x%1： ").arg(collect_part_protocol_id(), 2, 16, QLatin1Char('0')).toUpper();
    for (int point = 1; point <= 15; ++point) {
        const bool    collected = is_collect_point_file_exists(point);
        const bool    current   = (point == collect_area_index_);
        const QString color     = collected ? QStringLiteral("#4ade80") : QStringLiteral("#777777");
        const QString weight    = current ? QStringLiteral("bold") : QStringLiteral("normal");
        const QString border    = current ? QStringLiteral("border-bottom:1px solid #7dd3fc;") : QString();
        html += QStringLiteral("<span style='color:%1;font-weight:%2;%3'>%4</span> ")
                    .arg(color, weight, border)
                    .arg(point);
    }
    collected_points_label_->setText(html.trimmed());
}

void SensorWidget::check_current_point_duplicate_hint()
{
    touch::matrix<uint16_t> m;
    {
        std::shared_lock lock(data_mutex_);
        m = sensor_data_;
    }

    const auto sample = make_capture_sample(m, pressure_input_ ? pressure_input_->value() : 0.0, false);
    if (!sample.valid) {
        return;
    }

    const int duplicate_point = find_duplicate_collected_point(sample.cx, sample.cy, collect_area_index_);
    if (duplicate_point > 0) {
        QMessageBox::warning(this, "提示", "当前点位已采集完成，切换区域后再采集");
        logw(
            "Current collect point duplicate hint: current_point={}, duplicate_point={}, cx={:.6f}, cy={:.6f}",
            collect_area_index_, duplicate_point, sample.cx, sample.cy);
    }
}

QStringList SensorWidget::calibration_csv_paths_for_part_id(int part_id) const
{
    QString sn = current_sn_.trimmed();
    if (sn.isEmpty() || sn == QStringLiteral("null")) {
        sn = QStringLiteral("NO_SN");
    }

    QDir        dir(current_save_path_);
    QStringList paths;

    const int     protocol_id = collect_part_protocol_id_for_part(part_id);
    const QString part_tag    = collect_part_tag_for_protocol_id(protocol_id);
    const QString prefix      = QStringLiteral("lut_%1_%2_area_").arg(sn, part_tag);
    const QString suffix      = QStringLiteral(".csv");
    QStringList   files       = dir.entryList(
        QStringList() << QStringLiteral("lut_%1_%2_area_*.csv").arg(sn, part_tag), QDir::Files, QDir::Name);

    const QRegularExpression area_re(QStringLiteral("_area_(\\d+)\\.csv$"));
    std::sort(files.begin(), files.end(), [&area_re](const QString& a, const QString& b) {
        const auto ma = area_re.match(a);
        const auto mb = area_re.match(b);
        const int  ia = ma.hasMatch() ? ma.captured(1).toInt() : 0;
        const int  ib = mb.hasMatch() ? mb.captured(1).toInt() : 0;
        if (ia != ib) return ia < ib;
        return a < b;
    });

    for (const QString& file : files) {
        if (file.startsWith(prefix) && file.endsWith(suffix)) {
            paths.push_back(dir.filePath(file));
        }
    }

    paths.removeDuplicates();
    return paths;
}

QStringList SensorWidget::calibration_csv_paths_for_current_sn() const
{
    return calibration_csv_paths_for_part_id(collect_part_id_);
}

QStringList SensorWidget::brainco_calibration_csv_paths_for_part(int part_id) const
{
    QString sn = current_sn_.trimmed();
    if (sn.isEmpty() || sn == QStringLiteral("null")) sn = QStringLiteral("NO_SN");

    const int part = normalizeCollectPartId(part_id) >> 4;
    QDir dir(current_save_path_);
    QStringList paths;
    const int region_count = max_collect_region_for_part(part_id);
    for (int region = 1; region <= region_count; ++region) {
        const int id = (part << 4) | region;
        const QString tag = collect_part_tag_for_protocol_id(id);
        const QStringList files = dir.entryList(
            QStringList() << QStringLiteral("lut_%1_%2_area_*.csv").arg(sn, tag), QDir::Files, QDir::Name);
        for (const QString& file : files) paths << dir.filePath(file);
    }
    paths.removeDuplicates();
    return paths;
}

#if 0
QStringList SensorWidget::calibration_csv_paths_for_current_sn_legacy_removed() const
{
    QString sn = current_sn_.trimmed();
    if (sn.isEmpty() || sn == QStringLiteral("null")) {
        sn = QStringLiteral("NO_SN");
    }

    QDir dir(current_save_path_);
    QStringList paths;

    // 新命名规则：lut_<SN>_id_11_area_01.csv、lut_<SN>_id_11_area_02.csv ...
    const QString part_tag = current_collect_part_tag();
    const QString prefix = QStringLiteral("lut_%1_%2_area_").arg(sn, part_tag);
    const QString suffix = QStringLiteral(".csv");
    QStringList files = dir.entryList(QStringList() << QStringLiteral("lut_%1_%2_area_*.csv").arg(sn, part_tag),
                                      QDir::Files,
                                      QDir::Name);

    // QDir::Name 是字典序，旧文件名若出现 area_1、area_10、area_2 会排序错误。
    // 这里按 area 后面的数字做自然排序，确保 1~15 点位读取顺序稳定。
    const QRegularExpression area_re(QStringLiteral("_area_(\\d+)\\.csv$"));
    std::sort(files.begin(), files.end(), [&area_re](const QString& a, const QString& b) {
        const auto ma = area_re.match(a);
        const auto mb = area_re.match(b);
        const int ia = ma.hasMatch() ? ma.captured(1).toInt() : 0;
        const int ib = mb.hasMatch() ? mb.captured(1).toInt() : 0;
        if (ia != ib) return ia < ib;
        return a < b;
    });

    for (const QString& file : files) {
        if (file.startsWith(prefix) && file.endsWith(suffix)) {
            paths.push_back(dir.filePath(file));
        }
    }

    // 兼容旧版本遗留文件：lut_<SN>.csv。
    // 只有在没有找到 area_XX 文件时才读取旧文件，避免同一批数据被重复建表。
    if (paths.isEmpty()) {
        const QString legacy = dir.filePath(QStringLiteral("lut_%1.csv").arg(sn));
        if (QFileInfo::exists(legacy)) {
            paths.push_back(legacy);
        }
    }

    paths.removeDuplicates();
    return paths;
}
#endif

QString SensorWidget::current_test_csv_path() const
{
    QString sn = current_sn_.trimmed();
    if (sn.isEmpty() || sn == QStringLiteral("null")) {
        sn = QStringLiteral("NO_SN");
    }

    // 测试结果按当前采集部位映射后的协议 ID 隔离。
    return QDir(current_save_path_)
        .filePath(QStringLiteral("test_%1_%2.csv").arg(sn, current_collect_part_tag()));
}

double SensorWidget::current_sensor_matrix_sum() const
{
    const touch::matrix<uint16_t> matrix = matrix_for_current_part();
    double                        sum    = 0.0;
    if (matrix.empty()) {
        return sum;
    }
    const auto *matrix_ptr = matrix.ptr();
    for (int i = 0; i < matrix.size(); ++i) {
        sum += static_cast<double>(matrix_ptr[i]);
    }
    return sum;
}

QString SensorWidget::load_steps_config_path() const
{
    const QString file_name = QStringLiteral("load_steps.csv");
    const QString app_dir   = QCoreApplication::applicationDirPath();
    const QString cur_dir   = QDir::currentPath();

    // 优先加载“与 src 同一级”的 resources/config/load_steps.csv。
    // 常见发布目录：project_root/src 为源码目录，project_root/resources/config 为配置目录。
    // 因此先从 app_dir/current 及其上级目录寻找 resources/config，找不到再回退到旧路径。
    const QStringList candidates = {
        QDir(app_dir).filePath(QStringLiteral("resources/config/") + file_name),
        QDir(app_dir).filePath(QStringLiteral("../resources/config/") + file_name),
        QDir(app_dir).filePath(QStringLiteral("../../resources/config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("resources/config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("../resources/config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("../../resources/config/") + file_name),

        // 兼容旧版本：之前曾放在 src/resources/config 下。
        QDir(app_dir).filePath(QStringLiteral("src/resources/config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("src/resources/config/") + file_name),
        QDir(app_dir).filePath(QStringLiteral("../src/resources/config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("../src/resources/config/") + file_name),

        // 其他兜底路径。
        QDir(app_dir).filePath(QStringLiteral("config/") + file_name),
        QDir(cur_dir).filePath(QStringLiteral("config/") + file_name),
        QStringLiteral(":/config/") + file_name
    };

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            logi("Load steps config selected: {}", path.toStdString());
            return path;
        }
    }

    return QDir(app_dir).filePath(QStringLiteral("../resources/config/") + file_name);
}

QVector<double> SensorWidget::load_configured_force_offsets() const
{
    const QString   path = load_steps_config_path();
    QFile           file(path);
    QVector<double> offsets;

    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logw("Load steps config not found/readable: {}, fallback to default offsets", path.toStdString());
        return QVector<double>{ 20.0, 50.0, 100.0, 200.0, 400.0, 600.0 };
    }

    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#endif
    int line_no = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        ++line_no;
        if (line_no == 1) {
            line.remove(QChar(0xFEFF));
        }
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QStringList cells = line.split(',', Qt::SkipEmptyParts);
        for (QString cell : cells) {
            cell = cell.trimmed();
            if (cell.isEmpty()) continue;
            bool         ok = false;
            const double v  = cell.toDouble(&ok);
            if (!ok) {
                // 允许第一行写 header，例如 offset_g。
                continue;
            }
            if (v > 0.0) {
                offsets.push_back(v);
            }
        }
    }

    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end(),
                              [](double a, double b) { return std::fabs(a - b) <= 1e-6; }),
                  offsets.end());

    if (offsets.isEmpty()) {
        logw("Load steps config has no valid positive offsets: {}, fallback to default offsets",
             path.toStdString());
        return QVector<double>{ 20.0, 50.0, 100.0, 200.0, 400.0, 600.0 };
    }

    return offsets;
}

QVector<double> SensorWidget::current_load_gradient() const
{
    const double rod_weight = pressure_step_input_ ? pressure_step_input_->value() : 52.0;

    // CSV 中配置的是“相对杆重的增量”，不是每次累加值。
    // 实际操作序列从杆重开始：杆重、杆重+offset[0]、杆重+offset[1]...
    // 保存采集 CSV 时自动补写零挡位：diff_ad=0, force=0，cx/cy 使用首次有效触发质心。
    QVector<double> gradient;
    gradient.push_back(rod_weight);

    const QVector<double> offsets = load_configured_force_offsets();
    for (double offset : offsets) {
        gradient.push_back(rod_weight + offset);
    }

    std::sort(gradient.begin(), gradient.end());
    gradient.erase(std::unique(gradient.begin(), gradient.end(),
                               [](double a, double b) { return std::fabs(a - b) <= 1e-6; }),
                   gradient.end());
    return gradient;
}

void SensorWidget::reset_collect_zero_baseline()
{
    collect_zero_matrix_sum_valid_ = false;
    collect_zero_matrix_sum_       = 0.0;
}

void SensorWidget::reset_pressure_to_default()
{
    collect_force_index_ = 0;
    if (pressure_input_) {
        const QVector<double> gradient = current_load_gradient();
        pressure_input_->setValue(gradient.isEmpty() ? 0.0 : gradient.front());
    }
}

void SensorWidget::reset_test_pressure_sequence()
{
    test_force_index_ = 0;
    if (pressure_input_) {
        const QVector<double> gradient = current_load_gradient();
        pressure_input_->setValue(gradient.isEmpty() ? 0.0 : gradient.front());
    }
}

double SensorWidget::current_test_force_from_config()
{
    const QVector<double> gradient = current_load_gradient();
    if (gradient.isEmpty()) {
        return pressure_input_ ? pressure_input_->value() : 0.0;
    }

    if (test_force_index_ >= gradient.size()) {
        if (pressure_input_) {
            pressure_input_->setValue(gradient.back());
        }
        QMessageBox::information(this, "提示", "已达到当前区域设置上限，需要切换区域");
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double force = gradient.at(test_force_index_);
    if (pressure_input_) {
        pressure_input_->setValue(force);
    }

    // 本次测试使用当前档位；下一次点击自动进入下一个配置档位。
    ++test_force_index_;

    return force;
}

void SensorWidget::advance_pressure_to_next_gradient()
{
    const QVector<double> gradient = current_load_gradient();
    if (gradient.isEmpty()) {
        return;
    }

    ++collect_force_index_;
    if (pressure_input_) {
        if (collect_force_index_ < gradient.size()) {
            pressure_input_->setValue(gradient.at(collect_force_index_));
        }
        else {
            pressure_input_->setValue(gradient.back());
        }
    }
}

bool SensorWidget::collect_pressure_limit_reached() const
{
    const QVector<double> gradient = current_load_gradient();
    return !gradient.isEmpty() && collect_force_index_ >= gradient.size();
}

void SensorWidget::sync_collect_area_from_ui()
{
    collect_area_index_ = collect_area_spinbox_ ? collect_area_spinbox_->value() : collect_area_index_;
    pending_collect_samples_.clear();
    reset_collect_zero_baseline();
    reset_pressure_to_default();
    update_collected_points_ui();
    check_current_point_duplicate_hint();

    const QString file_path = current_calibration_csv_path();
    logi("Collect point selected: part_id={}, point={}, csv={}", collect_part_id_, collect_area_index_,
         file_path.toStdString());
    showToast(QStringLiteral("已选择部位ID 0x%1 点位%2，输入压力已恢复为杆重档")
                  .arg(collect_part_protocol_id(), 2, 16, QLatin1Char('0'))
                  .arg(collect_area_index_)
                  .toUpper());
}

void SensorWidget::sync_collect_part_from_ui()
{
    set_collect_part_id(collect_part_spinbox_ ? collect_part_spinbox_->value() : collect_part_id_, true);
}

void SensorWidget::switch_collect_part_to_next()
{
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2 && collect_part_id_ >= 0x51)
        set_collect_part_id(0x11, true);
    else
        set_collect_part_id(nextCollectPartId(collect_part_id_), true);
}

void SensorWidget::switch_collect_area_to_next()
{
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) {
        const int next = collect_region_id_ % max_collect_region_for_part(collect_part_id_) + 1;
        set_collect_region_id(next, false);
        return;
    }
    const int current = collect_area_spinbox_ ? collect_area_spinbox_->value() : collect_area_index_;
    if (current >= 15) {
        collect_area_index_ = 15;
        if (collect_area_spinbox_) {
            collect_area_spinbox_->blockSignals(true);
            collect_area_spinbox_->setValue(15);
            collect_area_spinbox_->blockSignals(false);
        }
        pending_collect_samples_.clear();
        reset_collect_zero_baseline();
        reset_pressure_to_default();
        update_collected_points_ui();
        QMessageBox::information(this, "提示", "已经到最后一个点位，输入压力已恢复为杆重档");
        return;
    }

    collect_area_index_ = current + 1;
    if (collect_area_spinbox_) {
        collect_area_spinbox_->setValue(collect_area_index_);
    }
    else {
        sync_collect_area_from_ui();
    }
}

void SensorWidget::reset_current_collect_point()
{
    // 强脑已去掉“重置该点位”：每个区域固定一个采集文件，本地 CSV 不在这里删除。
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) return;
    sync_collect_area_from_ui();
    const QString file_path = current_calibration_csv_path();
    pending_collect_samples_.clear();
    reset_pressure_to_default();

    if (QFileInfo::exists(file_path)) {
        if (!QFile::remove(file_path)) {
            QMessageBox::warning(this, "错误", QStringLiteral("无法删除当前点位文件：%1").arg(file_path));
            return;
        }
        logi("Collect point reset: point={}, file={}", collect_area_index_, file_path.toStdString());
        showToast(QStringLiteral("点位 %1 已重置，可重新采集").arg(collect_area_index_));
    }
    else {
        showToast(QStringLiteral("点位 %1 当前没有采集文件，可直接采集").arg(collect_area_index_));
    }
    update_collected_points_ui();
}

void SensorWidget::collect_current_sample()
{
    bind_device_to_collect_part_id();
    const touch::matrix<uint16_t> m = matrix_for_current_part();

    const QVector<double> gradient = current_load_gradient();
    if (!gradient.isEmpty() && collect_force_index_ >= gradient.size()) {
        if (pressure_input_) {
            pressure_input_->setValue(gradient.back());
        }
        QMessageBox::information(this, "提示", "已达到当前区域设置上限，需要切换区域");
        logw(
            "Collect data blocked: current point reached configured pressure limit. point={}, gradient_count={}",
            collect_area_index_, gradient.size());
        return;
    }

    const double force =
        (!gradient.isEmpty() && collect_force_index_ >= 0 && collect_force_index_ < gradient.size())
            ? gradient.at(collect_force_index_)
            : (pressure_input_ ? pressure_input_->value() : 0.0);
    if (pressure_input_) {
        pressure_input_->setValue(force);
    }
    // 强脑：输入压力按 g（砝码/杆重），写入 CSV 的 force 换算为 mN，下发的 LUT 力值轴即为 mN。
    const double sample_force = force_in_millinewton() ? force * kGramToMilliNewton : force;
    auto         sample         = make_capture_sample(m, sample_force, true);
    const double raw_matrix_sum = sample.diff_ad;

    QDir dir(current_save_path_);
    if (!dir.exists() && !dir.mkpath(".")) {
        loge("Collect data failed: cannot create save directory {}", current_save_path_.toStdString());
        QMessageBox::warning(this, "错误", QStringLiteral("无法创建保存路径：%1").arg(current_save_path_));
        return;
    }

    const QString file_path = current_calibration_csv_path();
    QString       error;
    if (!ensure_calibration_csv_file(file_path, &error)) {
        QMessageBox::warning(this, "错误", error.isEmpty() ? QStringLiteral("创建采集文件失败") : error);
        return;
    }

    // 当前版本不再要求先点击 0g 空载基准档。采集压力默认从杆重开始，
    // CSV/LUT 的 0g 档由下面的零点行自动补写：diff_ad=0, force=0。
    // 因此实际采集样本直接使用当前矩阵总值/特征值作为传感器总值。
    sample.diff_ad = raw_matrix_sum;

    // 工程化规则：采集时若当前没有触发，不再直接丢弃。
    // 该样本先暂存为“待定质心样本”；等后面第一次触发出有效 cx/cy 后，
    // 使用后面触发得到的 cx/cy 作为这些待定样本的默认质心区域，再统一写入当前 area CSV。
    if (!sample.valid) {
        pending_collect_samples_.push_back(sample);
        advance_pressure_to_next_gradient();
        logw(
            "Collect data pending: sensor not triggered. force={:.6f}, diff_ad={:.6f}, pending_count={}, next_force={:.6f}",
            sample.force, sample.diff_ad, pending_collect_samples_.size(),
            pressure_input_ ? pressure_input_->value() : 0.0);
        showToast(QStringLiteral("当前未触发：已暂存 force=%1，等待后续触发质心区域")
                      .arg(sample.force, 0, 'f', 3));
        return;
    }

    const int duplicate_point = find_duplicate_collected_point(sample.cx, sample.cy, collect_area_index_);
    if (duplicate_point > 0) {
        QMessageBox::warning(this, "提示", "当前点位已采集完成，切换区域后再采集");
        logw(
            "Collect data blocked by duplicate centroid: current_point={}, duplicate_point={}, cx={:.6f}, cy={:.6f}",
            collect_area_index_, duplicate_point, sample.cx, sample.cy);
        return;
    }

    // 每个点位 CSV 默认自动补一档零点：diff_ad=0, force=0。
    // 注意：零挡位不再由第一次点击采集按钮得到，也不使用当前矩阵；
    // 第一次点击采集按钮记录的是当前输入压力和当前矩阵总值。
    const bool need_zero_row = !calibration_csv_has_data_rows(file_path);
    if (need_zero_row) {
        app::CaptureSample zero_sample{};
        zero_sample.diff_ad      = 0.0;
        zero_sample.force        = 0.0;
        zero_sample.cx           = 0.0;
        zero_sample.cy           = 0.0;
        zero_sample.touch_weight = 0.0;
        zero_sample.rows         = sample.rows;
        zero_sample.cols         = sample.cols;
        zero_sample.peak_value   = 0.0;
        zero_sample.active_count = 0.0;
        zero_sample.area_eff     = 0.0;
        zero_sample.var_x        = 0.0;
        zero_sample.var_y        = 0.0;
        zero_sample.valid        = true;
        zero_sample.triggered    = false;
        if (!app::CalibrationCaptureService::appendSampleToCsv(file_path, zero_sample, &error)) {
            QMessageBox::warning(this, "错误",
                                 error.isEmpty() ? QStringLiteral("保存默认零点数据失败") : error);
            return;
        }
    }

    // 若之前存在未触发样本，则使用本次有效触发的 cx/cy 统一补齐后写入。
    // 这样“未触发时的默认质心区域”就等于后面第一次触发出来的质心区域。
    int flushed_pending_count = 0;
    if (!pending_collect_samples_.empty()) {
        for (auto pending : pending_collect_samples_) {
            pending.cx           = sample.cx;
            pending.cy           = sample.cy;
            pending.touch_weight = sample.touch_weight;
            pending.triggered    = true;
            pending.valid        = true;
            if (!app::CalibrationCaptureService::appendSampleToCsv(file_path, pending, &error)) {
                QMessageBox::warning(this, "错误",
                                     error.isEmpty() ? QStringLiteral("保存待定采集数据失败") : error);
                return;
            }
            ++flushed_pending_count;
        }
        pending_collect_samples_.clear();
    }

    if (!app::CalibrationCaptureService::appendSampleToCsv(file_path, sample, &error)) {
        QMessageBox::warning(this, "错误", error.isEmpty() ? QStringLiteral("保存采集数据失败") : error);
        return;
    }

    advance_pressure_to_next_gradient();
    update_collected_points_ui();

    logi(
        "Collect data success: file={}, diff_ad={:.6f}, force={:.6f}, cx={:.6f}, cy={:.6f}, flushed_pending_count={}, next_force={:.6f}",
        file_path.toStdString(), sample.diff_ad, sample.force, sample.cx, sample.cy, flushed_pending_count,
        pressure_input_ ? pressure_input_->value() : 0.0);
    showToast(QStringLiteral("已采集：diff_ad=%1, force=%2, cx=%3, cy=%4；补写待定=%5")
                  .arg(sample.diff_ad, 0, 'f', 3)
                  .arg(QStringLiteral("%1 %2").arg(sample.force, 0, 'f', 3).arg(force_unit()))
                  .arg(sample.cx, 0, 'f', 3)
                  .arg(sample.cy, 0, 'f', 3)
                  .arg(flushed_pending_count));
}

namespace
{

    static QVector<float> toQVectorFloat(const std::vector<float>& input)
    {
        QVector<float> out;
        out.reserve(static_cast<int>(input.size()));
        for (float v : input) out.push_back(v);
        return out;
    }

    static void waitForMs(int ms)
    {
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(ms);
        loop.exec();
    }

    static QVector<float> buildLutMetaPacket(const app::CalibrationProgramResult& result, int fallback_rows,
                                             int fallback_cols)
    {
        const int rows = result.sensor_rows > 0 ? result.sensor_rows : fallback_rows;
        const int cols = result.sensor_cols > 0 ? result.sensor_cols : fallback_cols;
        const int area_num =
            result.area_num > 0 ? result.area_num : static_cast<int>(result.cx_grid.size());

        QVector<float> meta;
        meta.reserve(6);
        meta << static_cast<float>(result.part_protocol_id) << static_cast<float>(rows)
             << static_cast<float>(cols) << static_cast<float>(area_num)
             << static_cast<float>(result.force_num) << (result.has_shape_feature ? 1.0f : 0.0f);
        return meta;
    }

    static bool checkCalibrationPacketCount(const QVector<float>& packet, int package_id, QString *error)
    {
        if (packet.isEmpty()) {
            if (error) *error = QStringLiteral("package_id=%1 为空").arg(package_id);
            return false;
        }
        if (packet.size() > 255) {
            if (error) {
                *error =
                    QStringLiteral("package_id=%1 的 float 数量 %2 超过当前协议 uint8_t count 上限 255")
                        .arg(package_id)
                        .arg(packet.size());
            }
            return false;
        }
        return true;
    }

} // namespace

void SensorWidget::simulate_calibration_to_mcu()
{
    // 兼容旧按钮/旧调用：只下发当前选择部位。新的“标定传感器”按钮已改为调用
    // send_all_parts_lut_to_device()，一次性按 0x11~0x61 下发所有已采集部位。
    bind_device_to_collect_part_id();
    const bool brainco = product_type_ == ProductModel::BRAINCO_GLOVE_V2;
    const QStringList csv_paths = brainco ? brainco_calibration_csv_paths_for_part(collect_part_id_)
                                          : calibration_csv_paths_for_current_sn();
    if (csv_paths.isEmpty()) {
        const QString expected = current_calibration_csv_path();
        const QString msg = QStringLiteral("未找到该序列号对应的采集 CSV。期望文件示例：%1").arg(expected);
        QMessageBox::warning(this, "标定下发失败", msg);
        return;
    }

    const int lut_part_id = brainco ? (collect_part_id_ & 0xF0) | 0x01 : collect_part_protocol_id_;
    const auto result = app::CalibrationTestService::simulateCalibrationToMcuForPart(lut_part_id, csv_paths);
    if (!result.success) {
        QMessageBox::warning(this, "标定下发失败",
                             result.error_message.isEmpty() ? QStringLiteral("建表失败，请查看日志")
                                                            : result.error_message);
        return;
    }

    const touch::matrix<uint16_t> fallback_matrix = matrix_for_part_id(collect_part_protocol_id_);
    const QVector<float> meta_f  = buildLutMetaPacket(result, fallback_matrix.rows, fallback_matrix.cols);
    const QVector<float> cx_f    = toQVectorFloat(result.cx_grid);
    const QVector<float> cy_f    = toQVectorFloat(result.cy_grid);
    const QVector<float> force_f = toQVectorFloat(result.force_grid);
    const QVector<float> table_f = toQVectorFloat(result.diff_ad_table);

    QString error;
    if (!checkCalibrationPacketCount(meta_f, 0, &error) || !checkCalibrationPacketCount(cx_f, 1, &error) ||
        !checkCalibrationPacketCount(cy_f, 2, &error) || !checkCalibrationPacketCount(force_f, 3, &error) ||
        !checkCalibrationPacketCount(table_f, 4, &error)) {
        QMessageBox::warning(this, "标定下发失败", error);
        return;
    }

    constexpr int kCalibrationPackageIntervalMs = 500;
    emit          set_device_active_query(false);

    emit send_calibration_curve_float(meta_f, 0);
    waitForMs(kCalibrationPackageIntervalMs);
    emit send_calibration_curve_float(cx_f, 1);
    waitForMs(kCalibrationPackageIntervalMs);
    emit send_calibration_curve_float(cy_f, 2);
    waitForMs(kCalibrationPackageIntervalMs);
    emit send_calibration_curve_float(force_f, 3);
    waitForMs(kCalibrationPackageIntervalMs);
    emit send_calibration_curve_float(table_f, 4);
    waitForMs(kCalibrationPackageIntervalMs);

    emit set_device_active_query(true);

    logi(
        "当前部位 LUT 下发完成：part=0x{:02X}, meta={}, area_cx={}, area_cy={}, force_grid={}, sensor_table={}",
        result.part_protocol_id, meta_f.size(), cx_f.size(), cy_f.size(), force_f.size(), table_f.size());

    showToast(QStringLiteral("标定下发完成：部位 0x%1，area=%2，force=%3")
                  .arg(result.part_protocol_id, 2, 16, QLatin1Char('0'))
                  .arg(result.area_num)
                  .arg(result.force_num)
                  .toUpper());
}

void SensorWidget::send_all_parts_lut_to_device()
{
    if (product_type_ == ProductModel::BRAINCO_GLOVE_V2) {
        send_brainco_all_parts_lut_to_device();
        return;
    }
    // 固件 V1：仅下发 UI 当前选择部位，避免标定一个部位时误覆盖其它部位。
    // PC 发送 part=1..6，F103/H743 负责经 SPI 和五路 UART 路由。
    const int old_part_id     = collect_part_id_;
    const int old_protocol_id = collect_part_protocol_id_;

    const QVector<int> part_ids{ old_part_id };
    int                success_count   = 0;
    int                total_csv_count = 0;
    QStringList        errors;

    app::CalibrationTestService::clearLoadedLut();
    emit set_device_active_query(false);

    for (int part_id : part_ids) {
        const int         protocol_id = collect_part_protocol_id_for_part(part_id);
        const QStringList csv_paths   = calibration_csv_paths_for_part_id(part_id);
        if (csv_paths.isEmpty()) {
            const QString msg = QStringLiteral("部位 0x%1：未找到采集 CSV，跳过")
                                    .arg(protocol_id, 2, 16, QLatin1Char('0'))
                                    .toUpper();
            errors << msg;
            logw("LUT 串口下发：{}", msg.toStdString());
            continue;
        }

        const uint8_t wire_part   = static_cast<uint8_t>((protocol_id >> 4) & 0x0f);
        const bool haocun = product_type_ == ProductModel::HAOCUN_GLOVE;
        if (haocun) {
            const auto result = app::CalibrationTestService::simulateCalibrationToMcuForPart(protocol_id, csv_paths);
            if (!result.success) {
                errors << QStringLiteral("部位0x%1建表失败：%2").arg(protocol_id, 2, 16, QLatin1Char('0')).arg(result.error_message).toUpper();
                continue;
            }
            const QVector<float> adc = toQVectorFloat(result.wire_adc);
            const QVector<float> force_cn = toQVectorFloat(result.wire_force_cn);
            last_lut_download_success_ = false;
            last_lut_download_error_.clear();
            uint32_t version = static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch());
            if (version == 0U) version = 1U;
            emit download_calibration_lut(wire_part, adc, force_cn, version, 1U);
            if (!last_lut_download_success_) {
                errors << QStringLiteral("部位0x%1下发失败：%2").arg(protocol_id, 2, 16, QLatin1Char('0')).arg(last_lut_download_error_).toUpper();
                continue;
            }
            total_csv_count += csv_paths.size();
            ++success_count;
            continue;
        }
        const int     region_rows = Utils::get_region_rows_by_csv(protocol_id);
        const int     region_cols = Utils::get_region_cols_by_csv(protocol_id);
        if (region_rows <= 0 || region_cols <= 0) {
            errors << QStringLiteral("部位0x%1 区域配置无效")
                          .arg(protocol_id, 2, 16, QLatin1Char('0'))
                          .toUpper();
            continue;
        }
        // 必须与 F103 重建后的显示矩阵及 mapping/region CSV 坐标一致。
        const int            region_count = std::max(1, Utils::get_region_count_by_csv(protocol_id));
        QVector<QStringList> region_csv(region_count + 1);
        for (const QString& csv : csv_paths) {
            double cx = 0.0, cy = 0.0;
            if (!read_centroid_from_csv(csv, &cx, &cy)) continue;
            const int row =
                std::clamp(static_cast<int>(std::lround(cy * (region_rows - 1))), 0, region_rows - 1);
            const int col =
                std::clamp(static_cast<int>(std::lround(cx * (region_cols - 1))), 0, region_cols - 1);
            const int region = Utils::get_region_id_by_csv(row, col, protocol_id);
            if (region >= 1 && region <= region_count) region_csv[region] << csv;
        }

        bool part_ok = true;
        for (int region = 1; region <= region_count; ++region) {
            if (region_csv[region].isEmpty()) {
                errors << QStringLiteral("部位0x%1 区域%2没有采集点")
                              .arg(protocol_id, 2, 16, QLatin1Char('0'))
                              .arg(region)
                              .toUpper();
                part_ok = false;
                continue;
            }
            const auto result = app::CalibrationTestService::simulateCalibrationToMcuForPart(
                protocol_id, region_csv[region]);
            if (!result.success) {
                errors << QStringLiteral("部位0x%1 区域%2建表失败：%3")
                              .arg(protocol_id, 2, 16, QLatin1Char('0'))
                              .arg(region)
                              .arg(result.error_message)
                              .toUpper();
                part_ok = false;
                continue;
            }
            const QVector<float> adc      = toQVectorFloat(result.wire_adc);
            const QVector<float> force_cn = toQVectorFloat(result.wire_force_cn);
            for (qsizetype point = 0; point < adc.size(); ++point) {
                logi("待下发区域 LUT：part=0x{:02X}, region={}, point={}, adc_sum={:.3f}, force={:.3f}",
                     protocol_id, region, point, adc[point], force_cn[point]);
            }
            last_lut_download_success_ = false;
            last_lut_download_error_.clear();
            const uint32_t version =
                static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch()) + static_cast<uint32_t>(region);
            emit download_calibration_lut(wire_part, adc, force_cn, version, static_cast<uint8_t>(region));
            if (!last_lut_download_success_) {
                errors << QStringLiteral("部位0x%1 区域%2下发失败：%3")
                              .arg(protocol_id, 2, 16, QLatin1Char('0'))
                              .arg(region)
                              .arg(last_lut_download_error_)
                              .toUpper();
                part_ok = false;
                continue;
            }
            total_csv_count += region_csv[region].size();
            logi("分区 LUT 下发完成：part=0x{:02X}, region={}, csv_count={}, points={}", protocol_id,
                 region, region_csv[region].size(), adc.size());
        }
        if (part_ok) ++success_count;
    }

    emit set_device_active_query(true);

    collect_part_id_          = old_part_id;
    collect_part_protocol_id_ = old_protocol_id;
    if (collect_part_spinbox_) {
        collect_part_spinbox_->blockSignals(true);
        collect_part_spinbox_->setValue(old_part_id);
        collect_part_spinbox_->blockSignals(false);
    }
    if (sensor_choose_id_spinbox_) {
        sensor_choose_id_spinbox_->blockSignals(true);
        sensor_choose_id_spinbox_->setValue(old_protocol_id);
        sensor_choose_id_spinbox_->blockSignals(false);
    }
    update_collected_points_ui();
    check_current_point_duplicate_hint();
    update_current_centroid_display(matrix_for_current_part());

    if (success_count <= 0) {
        QMessageBox::warning(this, "标定下发失败",
                             errors.isEmpty() ? QStringLiteral("未找到任何部位的采集 CSV")
                                              : errors.join(QStringLiteral("\n")));
        return;
    }

    const QString summary = QStringLiteral("标定下发完成：当前部位 0x%1，共 %2 个 CSV")
                                .arg(old_protocol_id, 2, 16, QLatin1Char('0'))
                                .arg(total_csv_count);
    if (!errors.isEmpty()) {
        QMessageBox::information(this, "标定下发完成（部分部位跳过）",
                                 summary + QStringLiteral("\n\n") + errors.join(QStringLiteral("\n")));
    }
    showToast(summary);
}

void SensorWidget::send_brainco_all_parts_lut_to_device()
{
    struct RegionLut
    {
        int            protocol_id = 0;
        uint8_t        endpoint    = 0;
        uint8_t        region      = 0;
        QVector<float> adc;
        QVector<float> force;
        int            csv_count   = 0;
    };

    QVector<RegionLut> prepared;
    QStringList skipped;
    app::CalibrationTestService::clearLoadedLut();

    // 强脑使用 Endpoint + Region 作为 LUT 键：拇指最多 2 个区域，其余四指最多 3 个区域。
    // 每个区域是独立事务；没有采集数据的区域跳过，不要求凑齐 14 张后才能发送。
    for (int part = 1; part <= 5; ++part) {
        const int base_id = (part << 4) | 0x01;
        const QStringList part_paths = brainco_calibration_csv_paths_for_part(base_id);
        if (part_paths.isEmpty()) {
            skipped << QStringLiteral("部位 0x%1 没有采集数据").arg(base_id, 2, 16, QLatin1Char('0')).toUpper();
            continue;
        }

        const int region_count = max_collect_region_for_part(base_id);
        for (int region = 1; region <= region_count; ++region) {
            const QString marker = QStringLiteral("_ID_%1_AREA_")
                                       .arg((part << 4) | region, 2, 16, QLatin1Char('0'))
                                       .toUpper();
            QStringList region_paths;
            for (const QString& path : part_paths)
                if (path.toUpper().contains(marker) && path.endsWith(QStringLiteral("_area_01.csv"), Qt::CaseInsensitive) && calibration_csv_has_data_rows(path))
                    region_paths << path;

            if (region_paths.isEmpty()) {
                skipped << QStringLiteral("部位 0x%1 区域 %2 没有有效采集 CSV")
                              .arg(base_id, 2, 16, QLatin1Char('0'))
                              .arg(region)
                              .toUpper();
                continue;
            }

            const auto result =
                app::CalibrationTestService::simulateCalibrationToMcuForPart(base_id, region_paths);
            if (!result.success) {
                skipped << QStringLiteral("部位 0x%1 区域 %2 建表失败：%3")
                              .arg(base_id, 2, 16, QLatin1Char('0'))
                              .arg(region)
                              .arg(result.error_message)
                              .toUpper();
                continue;
            }

            RegionLut item;
            item.protocol_id = (part << 4) | region;
            item.endpoint    = static_cast<uint8_t>(part);
            item.region      = static_cast<uint8_t>(region);
            item.adc         = toQVectorFloat(result.wire_adc);
            item.force       = toQVectorFloat(result.wire_force_cn);
            item.csv_count   = region_paths.size();
            prepared.push_back(std::move(item));
        }
    }

    if (prepared.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("标定下发失败"),
                             QStringLiteral("没有可发送的区域 LUT。\n%1")
                                 .arg(skipped.join(QStringLiteral("\n"))));
        return;
    }

    // CalibrationVersion 是本次点击的批次标识。使用 Unix 秒生成；同一次实际发送的区域共用，
    // 不要求设备中所有区域版本相同，也不用于比较标定质量或协议版本。
    uint32_t version = static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch());
    if (version == 0U) version = 1U;
    int completed = 0;
    QStringList send_errors;
    emit set_device_active_query(false);
    for (const RegionLut& item : prepared) {
        last_lut_download_success_ = false;
        last_lut_download_error_.clear();
        logi("强脑区域 LUT 开始下发：id=0x{:02X}, endpoint={}, region={}, csv_count={}, points={}, version={}",
             item.protocol_id, item.endpoint, item.region, item.csv_count, item.adc.size(), version);
        emit download_calibration_lut(item.endpoint, item.adc, item.force, version, item.region);
        if (!last_lut_download_success_) {
            send_errors << QStringLiteral("ID 0x%1 下发失败：%2")
                          .arg(item.protocol_id, 2, 16, QLatin1Char('0'))
                          .arg(last_lut_download_error_)
                          .toUpper();
            break;
        }
        ++completed;
        logi("强脑区域 LUT 下发并核验完成：id=0x{:02X}, points={}", item.protocol_id,
             item.adc.size());
    }
    emit set_device_active_query(true);

    if (!send_errors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("标定下发失败"),
                             QStringLiteral("已完成 %1/%2 张可用区域 LUT。\n%3")
                                 .arg(completed)
                                 .arg(prepared.size())
                                 .arg(send_errors.join(QStringLiteral("\n"))));
        return;
    }
    const QString summary = QStringLiteral("强脑标定下发完成：%1 张区域 LUT，版本 %2")
                                .arg(completed)
                                .arg(version);
    if (!skipped.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("标定下发完成（部分区域跳过）"),
                                 summary + QStringLiteral("\n\n跳过：\n") +
                                     skipped.join(QStringLiteral("\n")));
    }
    showToast(summary, 3000);
}

void SensorWidget::simulate_mcu_load_current_part_capture_data()
{
    // 新需求：点击“模拟MCU”时，将所有部位 0x11/0x21/0x31/0x41/0x51/0x61
    // 的采集 CSV 分别生成 LUT，并全部写入 force_lut_calculator.c/.h 的多部位运行时表。
    const int old_part_id     = collect_part_id_;
    const int old_protocol_id = collect_part_protocol_id_;

    const QVector<int> part_ids{ 0x11, 0x21, 0x31, 0x41, 0x51, 0x61 };
    int                success_count   = 0;
    int                total_csv_count = 0;
    QStringList        errors;

    app::CalibrationTestService::clearLoadedLut();

    for (int part_id : part_ids) {
        collect_part_id_          = part_id;
        collect_part_protocol_id_ = collect_part_protocol_id_for_part(part_id);

        const QStringList csv_paths = calibration_csv_paths_for_part_id(part_id);
        if (csv_paths.isEmpty()) {
            errors << QStringLiteral("部位ID 0x%1：未找到采集CSV")
                          .arg(collect_part_protocol_id_, 2, 16, QLatin1Char('0'))
                          .toUpper();
            logw("模拟MCU：部位 0x{:02X} 未找到采集 CSV，跳过", collect_part_protocol_id_);
            continue;
        }

        const auto result = app::CalibrationTestService::simulateCalibrationToMcuForPart(
            collect_part_protocol_id_, csv_paths);
        if (!result.success) {
            errors << QStringLiteral("部位ID 0x%1：%2")
                          .arg(collect_part_protocol_id_, 2, 16, QLatin1Char('0'))
                          .arg(result.error_message.isEmpty() ? QStringLiteral("发送到 C/H 失败")
                                                              : result.error_message)
                          .toUpper();
            loge("模拟MCU：部位 0x{:02X} 发送到 C/H 失败：{}", collect_part_protocol_id_,
                 result.error_message.toStdString());
            continue;
        }

        ++success_count;
        total_csv_count += csv_paths.size();
        logi(
            "模拟MCU：部位 0x{:02X} 已发送到 C/H，csv_count={}, samples={}, point_groups={}, dims=CY:{} CX:{} FORCE:{}",
            collect_part_protocol_id_, csv_paths.size(), result.sample_count, result.point_group_count,
            result.cy_num, result.cx_num, result.force_num);
    }

    collect_part_id_          = old_part_id;
    collect_part_protocol_id_ = old_protocol_id;

    if (success_count == 0) {
        QMessageBox::warning(this, "模拟MCU失败",
                             errors.isEmpty() ? QStringLiteral("未找到任何部位的采集CSV")
                                              : errors.join(QStringLiteral("\n")));
        return;
    }

    const QString summary = QStringLiteral("模拟MCU完成：已发送 %1/6 个部位、%2 个CSV到 C/H")
                                .arg(success_count)
                                .arg(total_csv_count);
    if (!errors.isEmpty()) {
        QMessageBox::information(this, "模拟MCU完成（部分部位跳过）",
                                 summary + QStringLiteral("\n\n") + errors.join(QStringLiteral("\n")));
    }
    showToast(summary);
}

void SensorWidget::switch_test_area_to_next()
{
    bind_device_to_collect_part_id();
    // 测试区域数量与当前部位 region_XX.csv 一致。
    const int region_count = std::max(1, Utils::get_region_count_by_csv(collect_part_protocol_id_));
    max_test_area_index_   = region_count;
    if (test_area_index_ >= region_count) {
        test_area_index_ = region_count;
        reset_test_pressure_sequence();
        QMessageBox::information(this, "提示", "已经到最后一个测试区域，测试压力已恢复为杆重档");
        logi("Test area switch requested at last area: test_area={}, pressure reset to rod weight",
             test_area_index_);
        return;
    }

    ++test_area_index_;
    reset_test_pressure_sequence();
    logi(
        "Test area switched: test_area={}, pressure reset to rod weight; test CSV remains part-id-specific",
        test_area_index_);
    showToast(QStringLiteral("已切换测试区域 %1，测试压力已恢复为杆重档").arg(test_area_index_));
}

void SensorWidget::run_calibration_test_once()
{
    bind_device_to_collect_part_id();

    logi("[测试流程] ===== 开始一次测试 ===== part_id=0x{:02X}, test_area={}, record_count_before={}",
         collect_part_protocol_id(), test_area_index_, test_records_.size());

    // 测试压力恢复为和采集流程一致：杆重、杆重+配置增量... 每点击一次自动递增。
    const double input_force_g = current_test_force_from_config();
    if (std::isnan(input_force_g)) {
        logw("[测试流程] 输入压力无效，测试终止：part_id=0x{:02X}", collect_part_protocol_id());
        return;
    }
    // 强脑传感器标定后按 mN 输出，输入的砝码克重先换算为 mN 再参与误差判断。
    const double      input_force = force_in_millinewton() ? input_force_g * kGramToMilliNewton : input_force_g;
    const QString     unit        = force_unit();
    const std::string unit_str    = unit.toStdString();
    logi("[测试流程] Step A 输入压力：input={:.6f} g => input_force={:.6f} {}（按杆重起始并随点击递增）",
         input_force_g, input_force, unit_str);

    const touch::matrix<uint16_t> m                  = matrix_for_current_part();
    double                        matrix_sum_for_log = 0.0;
    for (int r = 0; r < m.rows; ++r) {
        for (int c = 0; c < m.cols; ++c) {
            matrix_sum_for_log += static_cast<double>(m(r, c));
        }
    }
    logi("[测试流程] Step B 读取当前部位矩阵：part_id=0x{:02X}, rows={}, cols={}, matrix_sum={:.6f}",
         collect_part_protocol_id(), m.rows, m.cols, matrix_sum_for_log);

    const auto realtime_sample = make_capture_sample(m, input_force, true);
    if (!realtime_sample.valid) {
        QMessageBox::warning(this, "测试失败", "当前实时样本无效，无法生成测试记录");
        logw("Calibration test failed: invalid realtime sample, part_id=0x{:02X}, input_force={:.6f}",
             collect_part_protocol_id(), input_force);
        return;
    }

    // 测试只使用传感器实时上传数据：标定固件已将总力按原始 AD 加权分配回各点，
    // 所以矩阵总和就是传感器反馈的实际总力，不再执行上位机模拟 MCU 预测。
    const double  sensor_value     = realtime_sample.diff_ad;
    const double  feature_diff_ad  = realtime_sample.diff_ad;
    const double  feature_cx       = realtime_sample.cx;
    const double  feature_cy       = realtime_sample.cy;
    const int     feature_rows     = realtime_sample.rows;
    const int     feature_cols     = realtime_sample.cols;
    const QString test_source_text = QStringLiteral("传感器实际上传总值");

    logi(
        "[测试流程] Step C 使用传感器实际上传总值：rows={}, cols={}, sensor_value(matrix_sum)={:.6f}, cx={:.6f}, cy={:.6f}, input_force={:.6f}",
        feature_rows, feature_cols, sensor_value, feature_cx, feature_cy, input_force);

    const double diff          = sensor_value - input_force;
    const double ratio_percent = (std::abs(input_force) > 1e-9) ? (diff / input_force * 100.0) : 0.0;

    logi(
        "[测试流程] Step D 测试值来源={}：sensor_value={:.6f} {}, diff=sensor-input={:.6f} {}, ratio_percent={:.6f}%",
        test_source_text.toStdString(), sensor_value, unit_str, diff, unit_str, ratio_percent);

    scannerpt::domain::TestRecord record;
    record.input_pressure = input_force;
    record.sensor_value   = sensor_value;
    record.diff           = diff;
    record.ratio_percent  = ratio_percent;
    record.area_index     = test_area_index_;
    record.cx             = feature_cx;
    record.cy             = feature_cy;
    test_records_.push_back(record);

    // current_test_csv_path() 已按当前采集部位 ID 生成：test_<SN>_id_11.csv 等。
    // save_test_records_to_csv() 内部继续使用“误差设置”和“满量程误差设置”做通过/失败判断。
    save_test_records_to_csv();

    double fs_reference = 0.0;
    for (const auto& item : test_records_) {
        fs_reference = std::max(fs_reference, std::abs(item.input_pressure));
    }
    const double fs_percent     = (fs_reference > 1e-9) ? (diff / fs_reference * 100.0) : 0.0;
    const double error_limit    = error_percent_input_ ? error_percent_input_->value() : 15.0;
    const double fs_error_limit = fs_error_percent_input_ ? fs_error_percent_input_->value() : 5.0;
    const bool   ratio_pass     = std::abs(ratio_percent) <= error_limit;
    const bool   fs_pass        = std::abs(fs_percent) <= fs_error_limit;

    logi(
        "[测试流程] Step E 误差判断：error_limit={:.6f}%, fs_error_limit={:.6f}%FS, fs_reference={:.6f} {}, ratio_percent={:.6f}% => {}, fs_percent={:.6f}%FS => {}",
        error_limit, fs_error_limit, fs_reference, unit_str, ratio_percent, ratio_pass ? "PASS" : "FAIL", fs_percent,
        fs_pass ? "PASS" : "FAIL");
    logi("[测试流程] Step F 写入测试 CSV：file={}, part_id=0x{:02X}, source={}, record_count_after={}",
         current_test_csv_path().toStdString(), collect_part_protocol_id(), test_source_text.toStdString(),
         test_records_.size());

    logi(
        "Calibration test CSV updated: file={}, part_id=0x{:02X}, source={}, input_force={:.6f}, sensor_value={:.6f}, diff={:.6f}, ratio_percent={:.6f}, ratio_pass={}, fs_percent={:.6f}, fs_pass={}, diff_ad={:.6f}, cx={:.6f}, cy={:.6f}, record_count={}",
        current_test_csv_path().toStdString(), collect_part_protocol_id(), test_source_text.toStdString(),
        input_force, sensor_value, diff, ratio_percent, ratio_pass, fs_percent, fs_pass, feature_diff_ad,
        feature_cx, feature_cy, test_records_.size());

    showToast(QStringLiteral("测试已写入CSV：输入=%1，传感器=%2，误差=%3，来源=%4，判断=%5/%6")
                  .arg(QStringLiteral("%1 %2").arg(input_force, 0, 'f', 3).arg(unit))
                  .arg(QStringLiteral("%1 %2").arg(sensor_value, 0, 'f', 3).arg(unit))
                  .arg(QStringLiteral("%1 %2").arg(diff, 0, 'f', 3).arg(unit))
                  .arg(test_source_text)
                  .arg(ratio_pass ? QStringLiteral("通过") : QStringLiteral("失败"))
                  .arg(fs_pass ? QStringLiteral("FS通过") : QStringLiteral("FS失败")));
}

void SensorWidget::save_test_records_to_csv()
{
    if (test_records_.empty()) {
        QMessageBox::warning(this, "提示", "当前还没有测试记录");
        return;
    }

    QDir dir(current_save_path_);
    if (!dir.exists() && !dir.mkpath(".")) {
        QMessageBox::warning(this, "错误", QStringLiteral("无法创建保存路径：%1").arg(current_save_path_));
        return;
    }

    double fs_reference = 0.0;
    for (const auto& item : test_records_) {
        fs_reference = std::max(fs_reference, std::abs(item.input_pressure));
    }
    if (fs_reference < 1e-9 && pressure_input_) {
        // 记录里的压力已按 force_unit() 存储；界面输入框始终是 g，需要同样换算。
        fs_reference = std::abs(pressure_input_->value()) * (force_in_millinewton() ? kGramToMilliNewton : 1.0);
    }

    const double  error_limit    = error_percent_input_ ? error_percent_input_->value() : 15.0;
    const double  fs_error_limit = fs_error_percent_input_ ? fs_error_percent_input_->value() : 5.0;
    const QString file_path      = current_test_csv_path();

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", QStringLiteral("无法写入测试结果 CSV：%1").arg(file_path));
        return;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#endif
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    out << QStringLiteral("部位ID,测试区域,压力(%1),传感器(%1),误差(%1),误差百分比(%),误差判断,"
                          "满量程误差百分比(%),满量程误差判断,cx,cy\n")
               .arg(force_unit());

    for (const auto& tr : test_records_) {
        const double fs_percent = (fs_reference > 1e-9) ? (tr.diff / fs_reference * 100.0) : 0.0;
        const bool   ratio_pass = std::abs(tr.ratio_percent) <= error_limit;
        const bool   fs_pass    = std::abs(fs_percent) <= fs_error_limit;

        out << QStringLiteral("0x%1").arg(collect_part_protocol_id(), 2, 16, QLatin1Char('0')).toUpper()
            << "," << tr.area_index << "," << QString::number(tr.input_pressure, 'f', 6) << ","
            << QString::number(tr.sensor_value, 'f', 6) << "," << QString::number(tr.diff, 'f', 6) << ","
            << QString::number(tr.ratio_percent, 'f', 6) << "," << (ratio_pass ? "✅" : "❌") << ","
            << QString::number(fs_percent, 'f', 6) << "," << (fs_pass ? "✅" : "❌");

        out << "," << QString::number(tr.cx, 'f', 6) << "," << QString::number(tr.cy, 'f', 6) << "\n";
    }

    logi(
        "Test records saved: file={}, count={}, error_limit={:.3f}%, fs_error_limit={:.3f}%FS, fs_reference={:.3f}",
        file_path.toStdString(), test_records_.size(), error_limit, fs_error_limit, fs_reference);
    showToast(QStringLiteral("测试结果已保存：%1 条，文件=%2")
                  .arg(static_cast<int>(test_records_.size()))
                  .arg(QFileInfo(file_path).fileName()));
}

void SensorWidget::showToast(const QString& text, int duration)
{
    auto *toast = new QLabel(text, this);
    toast->setStyleSheet(R"(
        QLabel {
            background-color: rgba(30,30,30,220);
            color: #4ade80;
            padding: 14px 20px;
            border-radius: 8px;
            font-size: 18px;
            font-weight: bold;
        }
    )");
    toast->setAlignment(Qt::AlignLeft);
    toast->adjustSize();
    const int margin = 20;
    toast->move(width() - toast->width() - margin, margin);
    toast->show();
    toast->setAttribute(Qt::WA_DeleteOnClose);
    QTimer::singleShot(duration, toast, &QWidget::close);
}
