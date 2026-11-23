#ifndef MULT_LUT_H
#define MULT_LUT_H

#include <utility>
#include <vector>

// Legacy compatibility stub. The active force prediction path is mcu/force_lut_calculator.*.
class MultLut
{
public:
    struct Measurement
    {
        double ad_values = 0.0;
        double calibrate_force = 0.0;
        double actual_force = 0.0;
    };

    void clear_all();
    void clear();
    void add_measurement(const Measurement& m);
    bool fit();
    double predict(double ad_values) const;
    double predict_by_feature(double feature) const;
    const std::vector<std::pair<double, double>>& get_lut() const;
    void set_lut(const std::vector<std::pair<double, double>>& lut);

private:
    std::vector<Measurement> measurements_;
    std::vector<std::pair<double, double>> lut_;
};

#endif // MULT_LUT_H
