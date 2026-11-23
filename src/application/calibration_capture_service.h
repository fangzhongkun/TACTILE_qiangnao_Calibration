#ifndef CALIBRATION_CAPTURE_SERVICE_H
#define CALIBRATION_CAPTURE_SERVICE_H

#include <libtouch/matrix.h>
#include <QString>

#include <cstdint>

namespace app {

struct CaptureSample
{
    double diff_ad = 0.0;
    double force = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double touch_weight = 0.0;
    double peak_value = 0.0;
    double active_count = 0.0;
    double area_eff = 0.0;
    double var_x = 0.0;
    double var_y = 0.0;
    int rows = 0;
    int cols = 0;
    bool triggered = false;
    bool valid = false;
};

class CalibrationCaptureService
{
public:
    static CaptureSample calculateSample(const touch::matrix<uint16_t>& matrix,
                                         double force,
                                         bool enable_log = false,
                                         bool use_external_diff_ad = false,
                                         double external_diff_ad = 0.0,
                                         bool ignore_leading_bridge_points = true);
    static bool appendSampleToCsv(const QString& file_path, const CaptureSample& sample, QString *error_message = nullptr);

private:
    struct CentroidResult
    {
        double cx = 0.0;
        double cy = 0.0;
        double touch_weight = 0.0;
        bool triggered = false;
    };

    static CentroidResult computeWeightedCentroid01(const touch::matrix<uint16_t>& matrix,
                                                    bool ignore_leading_bridge_points = true);
};

} // namespace app

#endif // CALIBRATION_CAPTURE_SERVICE_H
