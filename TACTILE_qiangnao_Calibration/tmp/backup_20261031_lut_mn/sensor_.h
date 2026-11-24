#ifndef SCANNERPT_TEST_MODE_H
#define SCANNERPT_TEST_MODE_H

#include "application/calibration_capture_service.h"
#include "application/calibration_status.h"
#include "devices/calibration_protocol.h"
#include "dock-widget.h"
#include "domain/calibration_record.h"
#include "product_model.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <libtouch/matrix.h>
#include <map>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <shared_mutex>
#include <vector>

class SensorWidget final : public DockWidget
{
    Q_OBJECT
public:
    explicit SensorWidget(const QString& title, QWidget *parent = nullptr,
                          Qt::WindowFlags flags = Qt::WindowFlags());

    void setData(uint32_t id, const touch::matrix<uint16_t>& m);
    void setBridgeData(uint16_t positive, uint16_t negative);
    void set_firmware_version(std::string ver);
    void set_sn(std::string sn);
    void set_address(int id);
    void set_calibration_state(uint8_t calibrated);
    void set_lut_download_result(uint8_t part_id, bool success, uint8_t status, uint32_t crc32,
                                 uint32_t version, const QString& error);
    void set_part_calibration_status(uint8_t part_id, uint8_t region_id, bool success, uint8_t status,
                                     uint8_t points, uint8_t mode, uint32_t crc32, uint32_t version,
                                     const QString& error);
    // 强脑：用 STATUS_ALL 的结果刷新 14 个区域的状态格子和"是否标定"。
    void set_region_mask_status(const CalibrationRegionMaskResult& result);
    void product_model_changed(uint8_t idx);

signals:
    void read_address_signals();
    void read_sn_signals();
    void read_firmsoft_signals();
    void is_calibration_query();
    void choose_id(uint8_t id);
    void modify_address_requested(uint8_t id);
    // 模拟标定时将生成好的 LUT 分包下发到设备端。
    void send_calibration_curve_float(QVector<float> data, int package_id);
    // 重新标定：擦除标定表。region_id 为 0 擦整根手指，1～3 只擦该区域。
    void re_calibration_requested(uint8_t part_id, uint8_t region_id);
    // 标定 LUT 分包下发期间暂停/恢复设备主动查询。
    void set_device_active_query(bool enabled);
    void download_calibration_lut(uint8_t part_id, QVector<float> adc, QVector<float> force_cn,
                                  uint32_t version, uint8_t region_id);
    void query_part_calibration_status(uint8_t part_id);

private:
    void init_style();
    void init_ui();
    void init_connections();

    QWidget *create_system_settings_group();
    QWidget *create_device_info_group();
    QWidget *create_function_test_group(QWidget *parent);
    QWidget *create_calibration_test_group(QWidget *parent);

    void set_calibration_status_ui(app::CalibrationStatus state);
    bool query_calibration_state_from_device(int timeout_ms = 500);
    bool read_device_address_once(int timeout_ms = 300);
    bool read_serial_number_once(int timeout_ms = 300);
    bool read_firmware_once(int timeout_ms = 300);
    void run_self_check();
    void reset_device_info_ui();
    void showToast(const QString& text, int duration = 1600);
    touch::matrix<uint16_t>
    matrix_for_current_part(const touch::matrix<uint16_t>& fallback = touch::matrix<uint16_t>{}) const;
    touch::matrix<uint16_t>
                       matrix_for_part_id(int                            part_id,
                                          const touch::matrix<uint16_t>& fallback = touch::matrix<uint16_t>{}) const;
    app::CaptureSample make_capture_sample(const touch::matrix<uint16_t>& m, double force,
                                           bool enable_log = false) const;
    app::CaptureSample make_capture_sample_for_part(int part_id, const touch::matrix<uint16_t>& m,
                                                    double force, bool enable_log = false) const;
    void               update_current_centroid_display(const touch::matrix<uint16_t>& m);
    QString            current_calibration_csv_path() const;
    QString            collect_part_tag_for_protocol_id(int protocol_id) const;
    QStringList        calibration_csv_paths_for_part_id(int part_id) const;
    QStringList        brainco_calibration_csv_paths_for_part(int part_id) const;
    QStringList        calibration_csv_paths_for_current_sn() const;
    QString            current_test_csv_path() const;
    double             current_sensor_matrix_sum() const;
    QVector<double>    current_load_gradient() const;
    QVector<double>    load_configured_force_offsets() const;
    QString            load_steps_config_path() const;
    void               reset_pressure_to_default();
    void               reset_collect_zero_baseline();
    void               advance_pressure_to_next_gradient();
    bool               collect_pressure_limit_reached() const;
    void               reset_test_pressure_sequence();
    double             current_test_force_from_config();
    void               switch_test_area_to_next();
    void               sync_collect_area_from_ui();
    void               collect_current_sample();
    void               switch_collect_area_to_next();
    void               reset_current_collect_point();
    void               update_collected_points_ui();
    bool               is_collect_point_file_exists(int point) const;
    QString            calibration_csv_path_for_point(int point) const;
    int                collect_part_protocol_id() const;
    int                collect_part_protocol_id_for_part(int part_id) const;
    QString            current_collect_part_tag() const;
    void               set_collect_part_id(int part_id, bool emit_switch_device = true);
    void               set_collect_region_id(int region_id, bool emit_switch_device = true);
    int                max_collect_region_for_part(int part_id) const;
    void               bind_device_to_collect_part_id();
    void               sync_collect_part_from_ui();
    void               switch_collect_part_to_next();
    bool ensure_calibration_csv_file(const QString& file_path, QString *error_message = nullptr) const;
    bool calibration_csv_has_data_rows(const QString& file_path) const;
    bool read_centroid_from_csv(const QString& file_path, double *cx, double *cy) const;
    int  find_duplicate_collected_point(double cx, double cy, int ignore_point = -1) const;
    void check_current_point_duplicate_hint();
    void simulate_calibration_to_mcu();
    void send_brainco_all_parts_lut_to_device();
    void send_all_parts_lut_to_device();
    void simulate_mcu_load_current_part_capture_data();
    void run_calibration_test_once();
    void save_test_records_to_csv();

private:
    QWidget *page_ = nullptr;

    QString      current_save_path_;
    QString      current_sn_;
    ProductModel product_type_      = ProductModel::ZY_T2_XP;
    bool         bridge_data_valid_ = false;
    uint16_t     bridge_positive_   = 0;
    uint16_t     bridge_negative_   = 0;

    QLabel      *product_model_label_   = nullptr;
    QLabel      *save_path_value_label_ = nullptr;
    QPushButton *path_set_button_       = nullptr;

    QLabel      *firmware_version_label_    = nullptr;
    QLabel      *serial_number_label_       = nullptr;
    QPushButton *serial_number_read_button_ = nullptr;

    QLabel       *sensor_id_label_          = nullptr;
    QPushButton  *sensor_id_read_button_    = nullptr;
    QSpinBox     *sensor_modify_id_spinbox_ = nullptr;
    QPushButton  *sensor_modify_id_button_  = nullptr;
    QLabel       *calibration_status_label_ = nullptr;
    QStringList   region_status_text_;
    QVector<bool> region_calibrated_;
    QVector<bool> region_query_received_;
    QVector<bool> region_query_succeeded_;
    QPushButton  *calibration_query_button_ = nullptr;

    // 强脑 14 个区域的状态格子，下标与 STATUS_ALL 掩码的位序一致。
    QWidget                  *region_status_title_ = nullptr;
    QWidget                  *region_status_host_  = nullptr;
    std::array<QLabel *, 14>  region_status_cells_{};
    void                      reset_region_status_cells();

    QPushButton *self_check_button_        = nullptr;
    QSpinBox    *sensor_choose_id_spinbox_ = nullptr;
    QPushButton *sensor_switch_button_     = nullptr;

    QPushButton    *collect_button_              = nullptr;
    QSpinBox       *collect_area_spinbox_        = nullptr;
    QLabel         *collect_point_label_         = nullptr;
    QPushButton    *reset_collect_point_button_  = nullptr;
    QPushButton    *switch_collect_area_button_  = nullptr;
    QSpinBox       *collect_part_spinbox_        = nullptr;
    QSpinBox       *collect_region_spinbox_      = nullptr;
    QPushButton    *switch_collect_part_button_  = nullptr;
    QLabel         *collected_points_label_      = nullptr;
    QPushButton    *send_button_                 = nullptr;
    QPushButton    *re_calibration_button_       = nullptr;
    QPushButton    *test_button_                 = nullptr;
    QPushButton    *save_test_result_button_     = nullptr;
    QLabel         *test_result_label_           = nullptr;
    QDoubleSpinBox *pressure_input_              = nullptr;
    QDoubleSpinBox *pressure_step_input_         = nullptr;
    QDoubleSpinBox *error_percent_input_         = nullptr;
    QDoubleSpinBox *fs_error_percent_input_      = nullptr;
    QLabel         *current_centroid_label_      = nullptr;

    int     address_id_                 = -1;
    bool    address_received_           = false;
    bool    serial_received_            = false;
    bool    firmware_received_          = false;
    bool    calibration_query_received_ = false;
    uint8_t calibration_state_value_    = 0;

    int                                                       collect_area_index_            = 1;
    int                                                       collect_part_id_               = 0x11;
    int                                                       collect_part_protocol_id_      = 0x11;
    int                                                       collect_region_id_             = 1;
    int                                                       test_area_index_               = 1;
    int                                                       max_test_area_index_           = 1;
    int                                                       test_force_index_              = 0;
    int                                                       collect_force_index_           = 0;
    bool                                                      collect_zero_matrix_sum_valid_ = false;
    double                                                    collect_zero_matrix_sum_       = 0.0;
    // 采集时未触发的样本先暂存；等后续第一次触发到有效质心后，
    // 使用该有效质心作为这些待定样本的 cx/cy，再统一写入当前区域 CSV。
    std::vector<app::CaptureSample>                           pending_collect_samples_;
    std::vector<scannerpt::domain::TestRecord>                test_records_;
    std::map<int, std::vector<scannerpt::domain::TestRecord>> test_records_by_part_;

    std::string firmware_version_;
    bool        last_lut_download_success_ = false;
    QString     last_lut_download_error_;

    touch::matrix<uint16_t>                     sensor_data_;
    std::map<uint32_t, touch::matrix<uint16_t>> sensor_data_by_id_;
    mutable std::shared_mutex                   data_mutex_;
};

#endif // SCANNERPT_TEST_MODE_H
