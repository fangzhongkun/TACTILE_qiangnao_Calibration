#include "calibration_capture_service.h"

#include "logging.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace app {

CaptureSample CalibrationCaptureService::calculateSample(const touch::matrix<uint16_t>& matrix,
                                                         double force,
                                                         bool enable_log,
                                                         bool use_external_diff_ad,
                                                         double external_diff_ad,
                                                         bool ignore_leading_bridge_points)
{
    CaptureSample sample;
    sample.force = force;
    sample.rows = matrix.rows;
    sample.cols = matrix.cols;

    if (matrix.empty() || matrix.size() < 2) {
        if (enable_log) {
            logw("Capture sample calculation failed: empty matrix or size < 2, rows={}, cols={}, size={}",
                 matrix.rows, matrix.cols, matrix.size());
        }
        return sample;
    }

    const auto *data = matrix.ptr();
    double matrix_sum = 0.0;
    double sum_sq = 0.0;
    double peak = 0.0;
    double active_count = 0.0;
    for (int i = 0; i < matrix.size(); ++i) {
        const double v = static_cast<double>(data[i]);
        if (v <= 0.0) continue;
        matrix_sum += v;
        sum_sq += v * v;
        peak = std::max(peak, v);
        active_count += 1.0;
    }
    // 新规则：diff_ad 字段名保留用于兼容 CSV/LUT 建表代码，实际含义改为矩阵总值。
    sample.diff_ad = use_external_diff_ad ? external_diff_ad : matrix_sum;

    const auto centroid = computeWeightedCentroid01(matrix, ignore_leading_bridge_points);
    sample.cx = centroid.cx;
    sample.cy = centroid.cy;
    sample.touch_weight = centroid.touch_weight;
    sample.peak_value = peak;
    sample.active_count = active_count;
    sample.area_eff = sum_sq > 1e-9 ? (matrix_sum * matrix_sum) / sum_sq : 0.0;

    if (matrix_sum > 1e-9) {
        double vx = 0.0;
        double vy = 0.0;
        for (int r = 0; r < matrix.rows; ++r) {
            for (int c = 0; c < matrix.cols; ++c) {
                const int index = r * matrix.cols + c;
                const double w = static_cast<double>(data[index]);
                if (w <= 0.0) continue;
                const double x = matrix.cols > 1 ? static_cast<double>(c) / static_cast<double>(matrix.cols - 1) : 0.0;
                const double y = matrix.rows > 1 ? static_cast<double>(r) / static_cast<double>(matrix.rows - 1) : 0.0;
                const double dx = x - sample.cx;
                const double dy = y - sample.cy;
                vx += dx * dx * w;
                vy += dy * dy * w;
            }
        }
        sample.var_x = vx / matrix_sum;
        sample.var_y = vy / matrix_sum;
    }

    sample.triggered = centroid.triggered;
    sample.valid = centroid.triggered;

    if (enable_log) {
        if (sample.triggered) {
            logi("Capture sample calculated: triggered=true, sensor_sum={:.6f}, force={:.6f}, cx={:.6f}, cy={:.6f}, peak={:.6f}, active_count={:.0f}, area_eff={:.6f}, var_x={:.6f}, var_y={:.6f}, rows={}, cols={}",
                 sample.diff_ad, sample.force, sample.cx, sample.cy, sample.peak_value, sample.active_count, sample.area_eff, sample.var_x, sample.var_y, sample.rows, sample.cols);
        }
        else {
            logw("Capture sample calculated: triggered=false, default centroid=(0,0), diff_ad={:.6f}, force={:.6f}, touch_weight={:.6f}, rows={}, cols={}. This sample will not be recorded or used for calculation.",
                 sample.diff_ad, sample.force, sample.touch_weight, sample.rows, sample.cols);
        }
    }
    return sample;
}

bool CalibrationCaptureService::appendSampleToCsv(const QString& file_path, const CaptureSample& sample, QString *error_message)
{
    if (!sample.valid) {
        if (error_message) *error_message = QStringLiteral("当前未触发，质心默认为 0,0；该样本不参与记录");
        logw("Append capture sample skipped: invalid or not-triggered sample, cx={:.6f}, cy={:.6f}, touch_weight={:.6f}", sample.cx, sample.cy, sample.touch_weight);
        return false;
    }

    QFileInfo info(file_path);
    const bool need_header = !info.exists() || info.size() == 0;

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QString err = QStringLiteral("无法打开采集文件：%1").arg(file_path);
        if (error_message) *error_message = err;
        loge("Append capture sample failed: cannot open file={}", file_path.toStdString());
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#endif
    if (need_header) {
        out << "diff_ad,force,cx,cy,rows,cols,peak,active_count,area_eff,var_x,var_y\n";
    }
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);
    out << sample.diff_ad << ','
        << sample.force << ','
        << sample.cx << ','
        << sample.cy << ','
        << sample.rows << ','
        << sample.cols << ','
        << sample.peak_value << ','
        << sample.active_count << ','
        << sample.area_eff << ','
        << sample.var_x << ','
        << sample.var_y << '\n';

    logi("Capture sample appended: file={}, sensor_sum={:.6f}, force={:.6f}, cx={:.6f}, cy={:.6f}, peak={:.6f}, active_count={:.0f}, area_eff={:.6f}",
         file_path.toStdString(), sample.diff_ad, sample.force, sample.cx, sample.cy, sample.peak_value, sample.active_count, sample.area_eff);
    return true;
}

CalibrationCaptureService::CentroidResult CalibrationCaptureService::computeWeightedCentroid01(const touch::matrix<uint16_t>& matrix,
                                                                                              bool ignore_leading_bridge_points)
{
    CentroidResult result;

    if (matrix.empty() || matrix.rows <= 0 || matrix.cols <= 0) {
        return result;
    }

    double sum_w = 0.0;
    double sum_x = 0.0;
    double sum_y = 0.0;

    const auto *data = matrix.ptr();
    for (int r = 0; r < matrix.rows; ++r) {
        for (int c = 0; c < matrix.cols; ++c) {
            const int index = r * matrix.cols + c;
            // 旧协议/旧固件曾把 matrix[0] 和 matrix[1] 作为差分力觉输入；
            // OP3 新协议中 8×10 阵列完整显示，电桥数据来自第 2 个逻辑包，此处不再跳过前两点。
            const double w = (ignore_leading_bridge_points && (index == 0 || index == 1))
                                 ? 0.0
                                 : static_cast<double>(data[index]);
            if (w <= 0.0) continue;
            sum_w += w;
            sum_x += w * static_cast<double>(c);
            sum_y += w * static_cast<double>(r);
        }
    }

    result.touch_weight = sum_w;

    // 没有触发时，默认质心显示为 (0,0)，但该样本不参与实际计算和记录。
    if (sum_w <= 1e-9) {
        result.cx = 0.0;
        result.cy = 0.0;
        result.triggered = false;
        return result;
    }

    const double cx_pixel = sum_x / sum_w;
    const double cy_pixel = sum_y / sum_w;

    const double cx = matrix.cols > 1 ? cx_pixel / static_cast<double>(matrix.cols - 1) : 0.0;
    const double cy = matrix.rows > 1 ? cy_pixel / static_cast<double>(matrix.rows - 1) : 0.0;

    result.cx = std::clamp(cx, 0.0, 1.0);
    result.cy = std::clamp(cy, 0.0, 1.0);
    result.triggered = true;
    return result;
}

} // namespace app
