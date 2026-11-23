#include "calibration_service.h"

namespace scannerpt::application {

CalibrationService::LutArray CalibrationService::fit(const std::vector<domain::PressureMeasurement>& measurements,
                                                     MultLut& multLut,
                                                     int)
{
    LutArray luts{};
    multLut.clear_all();
    for (const auto& measurement : measurements) {
        MultLut::Measurement m;
        m.ad_values = measurement.diff_ad;
        m.calibrate_force = measurement.force;
        multLut.add_measurement(m);
    }
    multLut.fit();
    luts[0] = multLut.get_lut();
    return luts;
}

} // namespace scannerpt::application
