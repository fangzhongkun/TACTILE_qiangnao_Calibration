#include "multi-part-calibration-dialog.h"

#include "application/multi_part_calibration_service.h"
#include "utils/logging.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

MultiPartCalibrationDialog::MultiPartCalibrationDialog(QWidget *parent) : FormDialog(parent)
{
    setWindowTitle(tr("多部位标定管理"));
    resize(1000, 700);
    setupUI();
}

void MultiPartCalibrationDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout();

    // 标题和说明
    auto *titleLabel = new QLabel(tr("<h2>手部多部位标定系统</h2>"));
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto *descLabel =
        new QLabel(tr("为每个手指部位选择标定CSV文件，然后执行标定。支持单个部位或批量标定所有部位。"));
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // 创建滚动区域容纳所有部位控件
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    auto *scrollContent = new QWidget();
    auto *scrollLayout  = new QVBoxLayout(scrollContent);

    // 为每个部位创建控件组
    auto parts = app::MultiPartCalibrationService::getAllParts();
    for (auto part : parts) {
        auto *groupBox    = new QGroupBox(app::MultiPartCalibrationService::getPartName(part));
        auto *groupLayout = new QVBoxLayout();

        PartControl control;

        // 复选框：启用该部位
        control.checkbox = new QCheckBox(tr("启用标定"));
        control.checkbox->setChecked(false);
        connect(control.checkbox, &QCheckBox::stateChanged, this,
                &MultiPartCalibrationDialog::onPartCheckChanged);
        groupLayout->addWidget(control.checkbox);

        // CSV文件选择
        auto *csvLayout         = new QHBoxLayout();
        control.selectCsvButton = new QPushButton(tr("选择CSV文件..."));
        control.selectCsvButton->setEnabled(false);
        connect(control.selectCsvButton, &QPushButton::clicked, this,
                &MultiPartCalibrationDialog::onSelectCsvClicked);
        csvLayout->addWidget(control.selectCsvButton);
        csvLayout->addStretch();
        groupLayout->addLayout(csvLayout);

        // CSV文件列表
        control.csvListWidget = new QListWidget();
        control.csvListWidget->setMaximumHeight(80);
        control.csvListWidget->setEnabled(false);
        groupLayout->addWidget(control.csvListWidget);

        // 状态标签
        control.statusLabel = new QLabel(tr("未标定"));
        control.statusLabel->setStyleSheet("color: gray;");
        groupLayout->addWidget(control.statusLabel);

        groupBox->setLayout(groupLayout);
        scrollLayout->addWidget(groupBox);

        part_controls_[part] = control;
    }

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // 操作按钮区
    auto *buttonLayout = new QHBoxLayout();

    btn_calibrate_current_ = new QPushButton(tr("标定当前部位"));
    btn_calibrate_current_->setEnabled(false);
    connect(btn_calibrate_current_, &QPushButton::clicked, this,
            &MultiPartCalibrationDialog::onCalibrateCurrentPartClicked);
    buttonLayout->addWidget(btn_calibrate_current_);

    btn_calibrate_all_ = new QPushButton(tr("批量标定所有启用的部位"));
    btn_calibrate_all_->setEnabled(false);
    connect(btn_calibrate_all_, &QPushButton::clicked, this,
            &MultiPartCalibrationDialog::onCalibrateAllPartsClicked);
    buttonLayout->addWidget(btn_calibrate_all_);

    btn_query_status_ = new QPushButton(tr("查询状态"));
    connect(btn_query_status_, &QPushButton::clicked, this,
            &MultiPartCalibrationDialog::onQueryStatusClicked);
    buttonLayout->addWidget(btn_query_status_);

    btn_clear_ = new QPushButton(tr("清除所有标定"));
    connect(btn_clear_, &QPushButton::clicked, this,
            &MultiPartCalibrationDialog::onClearCalibrationClicked);
    buttonLayout->addWidget(btn_clear_);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // 日志显示区
    auto *logGroup  = new QGroupBox(tr("操作日志"));
    auto *logLayout = new QVBoxLayout();
    txt_log_        = new QTextEdit();
    txt_log_->setReadOnly(true);
    txt_log_->setMaximumHeight(150);
    logLayout->addWidget(txt_log_);
    logGroup->setLayout(logLayout);
    mainLayout->addWidget(logGroup);

    // 总体状态
    lbl_overall_status_ = new QLabel(tr("就绪"));
    lbl_overall_status_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    mainLayout->addWidget(lbl_overall_status_);

    setLayout(mainLayout);
}

void MultiPartCalibrationDialog::onPartCheckChanged(int state)
{
    // 查找是哪个部位的复选框
    for (auto it = part_controls_.begin(); it != part_controls_.end(); ++it) {
        if (sender() == it->checkbox) {
            bool enabled = (state == Qt::Checked);
            it->selectCsvButton->setEnabled(enabled);
            it->csvListWidget->setEnabled(enabled);

            if (enabled) {
                current_part_ = it.key();
            }

            // 更新按钮状态
            bool anyEnabled = false;
            for (const auto& control : part_controls_) {
                if (control.checkbox->isChecked() && !control.csvPaths.isEmpty()) {
                    anyEnabled = true;
                    break;
                }
            }
            btn_calibrate_all_->setEnabled(anyEnabled);

            // 当前部位按钮
            auto& currentControl = part_controls_[current_part_];
            btn_calibrate_current_->setEnabled(currentControl.checkbox->isChecked() &&
                                               !currentControl.csvPaths.isEmpty());

            break;
        }
    }
}

void MultiPartCalibrationDialog::onSelectCsvClicked()
{
    // 查找是哪个部位的按钮
    app::HandPart part = current_part_;
    for (auto it = part_controls_.begin(); it != part_controls_.end(); ++it) {
        if (sender() == it->selectCsvButton) {
            part = it.key();
            break;
        }
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("选择标定CSV文件 - %1").arg(app::MultiPartCalibrationService::getPartName(part)),
        QString(), tr("CSV文件 (*.csv);;所有文件 (*.*)"));

    if (!files.isEmpty()) {
        part_controls_[part].csvPaths = files;
        updatePartCsvList(part);

        updateStatus(tr("已为 %1 选择 %2 个CSV文件")
                         .arg(app::MultiPartCalibrationService::getPartName(part))
                         .arg(files.size()));

        // 更新按钮状态
        auto& control = part_controls_[part];
        if (control.checkbox->isChecked()) {
            btn_calibrate_current_->setEnabled(true);
            btn_calibrate_all_->setEnabled(true);
        }
    }
}

void MultiPartCalibrationDialog::onCalibrateCurrentPartClicked()
{
    auto& control = part_controls_[current_part_];
    if (!control.checkbox->isChecked() || control.csvPaths.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请先选择CSV文件"));
        return;
    }

    enableControls(false);
    updateStatus(tr("开始标定 %1 ...").arg(app::MultiPartCalibrationService::getPartName(current_part_)));

    auto result = app::MultiPartCalibrationService::calibratePartFromCsv(current_part_, control.csvPaths);

    if (result.success) {
        updateStatus(
            tr("✓ %1 标定成功！").arg(app::MultiPartCalibrationService::getPartName(current_part_)));
        control.statusLabel->setText(tr("✓ 已标定"));
        control.statusLabel->setStyleSheet("color: green; font-weight: bold;");

        if (result.parts_status.contains(current_part_)) {
            updatePartStatus(current_part_, result.parts_status[current_part_]);
        }
    }
    else {
        updateStatus(tr("✗ %1 标定失败：%2")
                         .arg(app::MultiPartCalibrationService::getPartName(current_part_))
                         .arg(result.error_message),
                     true);
        control.statusLabel->setText(tr("✗ 标定失败"));
        control.statusLabel->setStyleSheet("color: red; font-weight: bold;");
    }

    enableControls(true);
}

void MultiPartCalibrationDialog::onCalibrateAllPartsClicked()
{
    // 收集所有启用且有CSV文件的部位
    std::map<app::HandPart, QStringList> partsMap;
    for (auto it = part_controls_.begin(); it != part_controls_.end(); ++it) {
        if (it->checkbox->isChecked() && !it->csvPaths.isEmpty()) {
            partsMap[it.key()] = it->csvPaths;
        }
    }

    if (partsMap.empty()) {
        QMessageBox::warning(this, tr("错误"), tr("没有启用的部位或未选择CSV文件"));
        return;
    }

    auto reply = QMessageBox::question(this, tr("确认批量标定"),
                                       tr("即将标定 %1 个部位，是否继续？").arg(partsMap.size()),
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    enableControls(false);
    updateStatus(tr("========== 开始批量标定 =========="));

    auto result = app::MultiPartCalibrationService::calibrateAllParts(partsMap);

    updateStatus(tr("========== 批量标定完成 =========="));
    updateStatus(tr("总计: %1, 成功: %2, 失败: %3")
                     .arg(result.total_parts)
                     .arg(result.succeeded_parts)
                     .arg(result.failed_parts));

    // 更新各部位状态
    for (const auto& [part, status] : result.parts_status) {
        updatePartStatus(part, status);
    }

    if (result.success) {
        lbl_overall_status_->setText(tr("✓ 批量标定成功"));
        lbl_overall_status_->setStyleSheet("background-color: #d4edda; color: #155724; padding: 5px;");
        QMessageBox::information(this, tr("成功"), tr("所有部位标定成功！"));
    }
    else {
        lbl_overall_status_->setText(tr("✗ 部分标定失败"));
        lbl_overall_status_->setStyleSheet("background-color: #f8d7da; color: #721c24; padding: 5px;");
        QMessageBox::warning(this, tr("警告"), result.error_message);
    }

    enableControls(true);
}

void MultiPartCalibrationDialog::onClearCalibrationClicked()
{
    auto reply = QMessageBox::question(this, tr("确认清除"), tr("确定要清除所有部位的标定数据吗？"),
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        app::MultiPartCalibrationService::clearAllCalibrations();
        updateStatus(tr("已清除所有标定数据"));

        // 重置UI状态
        for (auto& control : part_controls_) {
            control.statusLabel->setText(tr("未标定"));
            control.statusLabel->setStyleSheet("color: gray;");
        }
        lbl_overall_status_->setText(tr("已清除"));
        lbl_overall_status_->setStyleSheet("");
    }
}

void MultiPartCalibrationDialog::onQueryStatusClicked()
{
    updateStatus(tr("========== 查询标定状态 =========="));

    auto parts = app::MultiPartCalibrationService::getAllParts();
    for (auto part : parts) {
        auto status = app::MultiPartCalibrationService::queryPartStatus(part);
        updatePartStatus(part, status);
    }
}

void MultiPartCalibrationDialog::updatePartCsvList(app::HandPart part)
{
    auto& control = part_controls_[part];
    control.csvListWidget->clear();
    for (const auto& path : control.csvPaths) {
        QFileInfo info(path);
        control.csvListWidget->addItem(info.fileName());
    }
}

void MultiPartCalibrationDialog::updateStatus(const QString& message, bool isError)
{
    QString coloredMsg = message;
    if (isError) {
        coloredMsg = QString("<span style='color: red;'>%1</span>").arg(message);
    }
    txt_log_->append(coloredMsg);
    logi("{}", message.toStdString());
}

void MultiPartCalibrationDialog::updatePartStatus(app::HandPart                     part,
                                                  const app::PartCalibrationStatus& status)
{
    if (!part_controls_.contains(part)) {
        return;
    }

    auto&   control = part_controls_[part];
    QString statusText;

    if (status.lut_loaded) {
        statusText = tr("✓ 已标定 (区域:%1, 力值:%2, 样本:%3)")
                         .arg(status.area_count)
                         .arg(status.force_count)
                         .arg(status.sample_count);
        control.statusLabel->setStyleSheet("color: green; font-weight: bold;");

        updateStatus(tr("%1: %2").arg(app::MultiPartCalibrationService::getPartName(part)).arg(statusText));
    }
    else {
        statusText = tr("未标定");
        if (!status.last_error.isEmpty()) {
            statusText += tr(" - %1").arg(status.last_error);
        }
        control.statusLabel->setStyleSheet("color: gray;");
    }

    control.statusLabel->setText(statusText);
}

void MultiPartCalibrationDialog::enableControls(bool enable)
{
    for (auto& control : part_controls_) {
        control.checkbox->setEnabled(enable);
        control.selectCsvButton->setEnabled(enable && control.checkbox->isChecked());
    }

    btn_calibrate_current_->setEnabled(enable);
    btn_calibrate_all_->setEnabled(enable);
    btn_clear_->setEnabled(enable);
    btn_query_status_->setEnabled(enable);
}
