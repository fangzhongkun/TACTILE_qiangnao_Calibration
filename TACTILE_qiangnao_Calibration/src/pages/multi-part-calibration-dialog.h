#ifndef MULTI_PART_CALIBRATION_DIALOG_H
#define MULTI_PART_CALIBRATION_DIALOG_H

#include "application/multi_part_calibration_service.h"
#include "widgets/form-dialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QTextEdit>

/**
 * @brief 多部位标定对话框
 *
 * 功能：
 * 1. 为每个手指部位选择标定CSV文件
 * 2. 批量执行标定
 * 3. 显示标定进度和结果
 * 4. 支持单个部位或全部部位标定
 */
class MultiPartCalibrationDialog : public FormDialog
{
    Q_OBJECT

public:
    explicit MultiPartCalibrationDialog(QWidget *parent = nullptr);
    ~MultiPartCalibrationDialog() override = default;

private slots:
    void onPartCheckChanged(int state);
    void onSelectCsvClicked();
    void onCalibrateCurrentPartClicked();
    void onCalibrateAllPartsClicked();
    void onClearCalibrationClicked();
    void onQueryStatusClicked();

private:
    void setupUI();
    void updatePartCsvList(app::HandPart part);
    void updateStatus(const QString& message, bool isError = false);
    void updatePartStatus(app::HandPart part, const app::PartCalibrationStatus& status);
    void enableControls(bool enable);

    // 部位选择
    struct PartControl
    {
        QCheckBox   *checkbox;
        QPushButton *selectCsvButton;
        QListWidget *csvListWidget;
        QLabel      *statusLabel;
        QStringList  csvPaths;
    };

    QMap<app::HandPart, PartControl> part_controls_;

    // 操作按钮
    QPushButton *btn_calibrate_current_;
    QPushButton *btn_calibrate_all_;
    QPushButton *btn_clear_;
    QPushButton *btn_query_status_;

    // 状态显示
    QTextEdit *txt_log_;
    QLabel    *lbl_overall_status_;

    // 当前选中的部位
    app::HandPart current_part_ = app::HandPart::THUMB;
};

#endif // MULTI_PART_CALIBRATION_DIALOG_H
