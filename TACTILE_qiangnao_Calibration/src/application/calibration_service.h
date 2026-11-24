#ifndef SCANNERPT_APPLICATION_CALIBRATION_SERVICE_H
#define SCANNERPT_APPLICATION_CALIBRATION_SERVICE_H

#include "../domain/calibration_record.h"
#include "../force-calibration/mult-lut.h"
#include <array>
#include <utility>
#include <vector>

namespace scannerpt::application {

// Legacy compatibility service. Active calibration is handled by CalibrationCaptureService / CalibrationTestService.
class CalibrationService
{
public:
    using LutArray = std::array<std::vector<std::pair<double, double>>, 4>;

    static LutArray fit(const std::vector<domain::PressureMeasurement>& measurements,
                        MultLut& multLut,
                        int lutCount);
};

} // namespace scannerpt::application

#endif // SCANNERPT_APPLICATION_CALIBRATION_SERVICE_H
