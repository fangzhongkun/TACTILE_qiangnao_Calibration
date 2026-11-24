#include "mult-lut.h"

void MultLut::clear_all() { measurements_.clear(); lut_.clear(); }
void MultLut::clear() { clear_all(); }
void MultLut::add_measurement(const Measurement& m) { measurements_.push_back(m); }
bool MultLut::fit() { return true; }
double MultLut::predict(double ad_values) const { return ad_values; }
double MultLut::predict_by_feature(double feature) const { return predict(feature); }
const std::vector<std::pair<double, double>>& MultLut::get_lut() const { return lut_; }
void MultLut::set_lut(const std::vector<std::pair<double, double>>& lut) { lut_ = lut; }
