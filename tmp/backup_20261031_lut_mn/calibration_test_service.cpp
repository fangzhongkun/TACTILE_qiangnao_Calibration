#include "calibration_test_service.h"

#include "logging.h"
#include "mcu/force_lut_calculator.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <vector>

namespace app
{
    namespace
    {

        constexpr double kEps                  = 1e-6;
        constexpr double kIgnoreSyntheticForce = 9999.0;

        struct RawRecord
        {
            double sensor            = 0.0; // diff_ad / sensor_sum
            double force             = 0.0;
            double cx                = 0.5;
            double cy                = 0.5;
            double rows              = 0.0;
            double cols              = 0.0;
            double peak              = 0.0;
            double active_count      = 0.0;
            double area_eff          = 0.0;
            double var_x             = 0.0;
            double var_y             = 0.0;
            bool   has_shape_feature = false;
        };

        struct AreaCurve
        {
            QString                  source;
            double                   cx = 0.5;
            double                   cy = 0.5;
            std::vector<RawRecord>   records;
            std::map<double, double> sensor_by_force;
            std::map<double, double> area_eff_by_force;
            std::map<double, double> active_count_by_force;
            std::map<double, double> peak_by_force;
            bool                     has_shape_feature = false;
        };

        struct BuiltLut
        {
            std::vector<AreaCurve> areas;
            std::vector<double>    force_grid;
            std::vector<double>    area_cx;
            std::vector<double>    area_cy;
            std::vector<double>    sensor_table;       // [area][force]
            std::vector<double>    area_eff_table;     // [area][force], optional
            std::vector<double>    active_count_table; // [area][force], optional
            std::vector<double>    peak_table;         // [area][force], optional
            bool                   has_shape_feature = false;
            int                    sensor_rows       = 0;
            int                    sensor_cols       = 0;
            int                    sample_count      = 0;

            int areaNum() const { return static_cast<int>(areas.size()); }
            int forceNum() const { return static_cast<int>(force_grid.size()); }
        };

        static bool g_mcu_lut_loaded            = false;
        static bool g_mcu_lut_loaded_by_part[6] = { false, false, false, false, false, false };

        static int partSlot(int part_protocol_id)
        {
            switch (part_protocol_id) {
            case 0x11: return 0;
            case 0x21: return 1;
            case 0x31: return 2;
            case 0x41: return 3;
            case 0x51: return 4;
            case 0x61: return 5;
            default:   return -1;
            }
        }

        static double normalizeCoord01(double v)
        {
            if (v < 0.0) v = (v + 1.0) * 0.5;
            return std::clamp(v, 0.0, 1.0);
        }

        static bool nearlyEqual(double a, double b) { return std::fabs(a - b) <= kEps; }

        static std::vector<float> toFloatVector(const std::vector<double>& src)
        {
            std::vector<float> out;
            out.reserve(src.size());
            for (double v : src) out.push_back(static_cast<float>(v));
            return out;
        }

        static bool loadCsvAsArea(const QString& path, AreaCurve& out, QString& error)
        {
            QFile file(path);
            if (!file.exists()) {
                error = QStringLiteral("标定 CSV 不存在：%1").arg(path);
                return false;
            }
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                error = QStringLiteral("无法打开标定 CSV：%1").arg(path);
                return false;
            }

            QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            in.setEncoding(QStringConverter::Utf8);
#endif

            out        = AreaCurve{};
            out.source = path;

            int line_no = 0;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                ++line_no;
                if (line.isEmpty()) continue;
                if (line_no == 1 && line.contains(QStringLiteral("diff_ad"), Qt::CaseInsensitive)) continue;

                const auto cols = line.split(',', Qt::KeepEmptyParts);
                if (cols.size() < 4) continue;

                bool      ok0 = false, ok1 = false, ok2 = false, ok3 = false;
                RawRecord r;
                r.sensor = cols[0].trimmed().toDouble(&ok0);
                r.force  = cols[1].trimmed().toDouble(&ok1);
                r.cx     = normalizeCoord01(cols[2].trimmed().toDouble(&ok2));
                r.cy     = normalizeCoord01(cols[3].trimmed().toDouble(&ok3));
                if (!(ok0 && ok1 && ok2 && ok3)) continue;

                // 新版 CSV 兼容扩展列：rows,cols,peak,active_count,area_eff,var_x,var_y。
                // 旧 CSV 只有 diff_ad,force,cx,cy 时仍可建表，但不会启用接触面积补偿。
                if (cols.size() >= 11) {
                    bool ok4 = false, ok5 = false, ok6 = false, ok7 = false, ok8 = false, ok9 = false,
                         ok10           = false;
                    r.rows              = cols[4].trimmed().toDouble(&ok4);
                    r.cols              = cols[5].trimmed().toDouble(&ok5);
                    r.peak              = cols[6].trimmed().toDouble(&ok6);
                    r.active_count      = cols[7].trimmed().toDouble(&ok7);
                    r.area_eff          = cols[8].trimmed().toDouble(&ok8);
                    r.var_x             = cols[9].trimmed().toDouble(&ok9);
                    r.var_y             = cols[10].trimmed().toDouble(&ok10);
                    r.has_shape_feature = ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10;
                }
                if (r.force >= kIgnoreSyntheticForce) {
                    // 旧版本自动追加的 10000g 合成外推点不再参与 MCU 建表。
                    continue;
                }
                out.records.push_back(r);
            }

            if (out.records.empty()) {
                error = QStringLiteral("标定 CSV 中没有有效数据：%1").arg(path);
                return false;
            }

            bool has_zero = false;
            for (const auto& r : out.records) {
                if (std::fabs(r.force) <= kEps) {
                    has_zero = true;
                    break;
                }
            }
            if (!has_zero) {
                RawRecord zero = out.records.front();
                zero.force     = 0.0;
                zero.sensor    = 0.0;
                out.records.insert(out.records.begin(), zero);
            }

            // area 中心只用真实受力档计算，避免 0g 默认档影响位置中心。
            double sx           = 0.0;
            double sy           = 0.0;
            int    center_count = 0;
            for (const auto& r : out.records) {
                if (r.force > kEps) {
                    sx += r.cx;
                    sy += r.cy;
                    ++center_count;
                }
            }
            if (center_count <= 0) {
                sx           = out.records.front().cx;
                sy           = out.records.front().cy;
                center_count = 1;
            }
            out.cx = sx / static_cast<double>(center_count);
            out.cy = sy / static_cast<double>(center_count);

            struct AccumValue
            {
                double sensor        = 0.0;
                double area_eff      = 0.0;
                double active_count  = 0.0;
                double peak          = 0.0;
                int    count         = 0;
                int    feature_count = 0;
            };

            std::map<double, AccumValue> accum;
            for (const auto& r : out.records) {
                double key = r.force;
                // 合并同一压力档的重复采集。
                for (const auto& kv : accum) {
                    if (nearlyEqual(kv.first, r.force)) {
                        key = kv.first;
                        break;
                    }
                }
                auto& acc = accum[key];
                acc.sensor += r.sensor;
                acc.count += 1;
                if (r.has_shape_feature) {
                    acc.area_eff += r.area_eff;
                    acc.active_count += r.active_count;
                    acc.peak += r.peak;
                    acc.feature_count += 1;
                    out.has_shape_feature = true;
                }
            }
            for (const auto& kv : accum) {
                if (kv.second.count > 0) {
                    out.sensor_by_force[kv.first] = kv.second.sensor / static_cast<double>(kv.second.count);
                }
                if (kv.second.feature_count > 0) {
                    out.area_eff_by_force[kv.first] =
                        kv.second.area_eff / static_cast<double>(kv.second.feature_count);
                    out.active_count_by_force[kv.first] =
                        kv.second.active_count / static_cast<double>(kv.second.feature_count);
                    out.peak_by_force[kv.first] =
                        kv.second.peak / static_cast<double>(kv.second.feature_count);
                }
            }
            if (out.sensor_by_force.empty()) {
                error = QStringLiteral("标定 CSV 无法生成 force/sensor 曲线：%1").arg(path);
                return false;
            }
            return true;
        }

        static double interpolateSensorAtForce(const AreaCurve& area, double force)
        {
            if (area.sensor_by_force.empty()) return 0.0;
            auto it = area.sensor_by_force.lower_bound(force);
            if (it == area.sensor_by_force.begin()) return it->second;
            if (it == area.sensor_by_force.end()) return std::prev(it)->second;
            if (nearlyEqual(it->first, force)) return it->second;

            auto         hi = it;
            auto         lo = std::prev(it);
            const double f0 = lo->first;
            const double f1 = hi->first;
            const double s0 = lo->second;
            const double s1 = hi->second;
            if (std::fabs(f1 - f0) <= kEps) return s0;
            const double t = (force - f0) / (f1 - f0);
            return s0 + t * (s1 - s0);
        }

        static double interpolateMapAtForce(const std::map<double, double>& curve, double force)
        {
            if (curve.empty()) return 0.0;
            auto it = curve.lower_bound(force);
            if (it == curve.begin()) return it->second;
            if (it == curve.end()) return std::prev(it)->second;
            if (nearlyEqual(it->first, force)) return it->second;

            auto         hi = it;
            auto         lo = std::prev(it);
            const double f0 = lo->first;
            const double f1 = hi->first;
            const double v0 = lo->second;
            const double v1 = hi->second;
            if (std::fabs(f1 - f0) <= kEps) return v0;
            const double t = (force - f0) / (f1 - f0);
            return v0 + t * (v1 - v0);
        }

        static bool buildPointLutFromCsvFiles(const QStringList& csv_paths, BuiltLut& out, QString& error)
        {
            out = BuiltLut{};
            if (csv_paths.isEmpty()) {
                error = QStringLiteral("没有可用的标定 CSV");
                return false;
            }

            int shape_area_count = 0;
            for (const QString& path : csv_paths) {
                AreaCurve area;
                if (!loadCsvAsArea(path, area, error)) return false;
                out.sample_count += static_cast<int>(area.records.size());
                if (area.has_shape_feature) ++shape_area_count;
                for (const auto& r : area.records) {
                    if (r.has_shape_feature && r.rows > 0.0 && r.cols > 0.0) {
                        if (out.sensor_rows <= 0) out.sensor_rows = static_cast<int>(std::llround(r.rows));
                        if (out.sensor_cols <= 0) out.sensor_cols = static_cast<int>(std::llround(r.cols));
                    }
                }
                for (const auto& kv : area.sensor_by_force) out.force_grid.push_back(kv.first);
                out.areas.push_back(std::move(area));
            }

            std::sort(out.force_grid.begin(), out.force_grid.end());
            out.force_grid.erase(std::unique(out.force_grid.begin(), out.force_grid.end(),
                                             [](double a, double b) { return std::fabs(a - b) <= kEps; }),
                                 out.force_grid.end());

            if (out.areas.empty() || out.force_grid.empty()) {
                error = QStringLiteral("建表失败：area 或 force 为空");
                return false;
            }
            // 只有所有 area 都包含扩展特征时，才启用面积/峰值补偿；否则自动退化为纯位置 KNN。
            out.has_shape_feature = (shape_area_count == static_cast<int>(out.areas.size()));
            if (out.areas.size() > FORCE_LUT_MAX_AREA_NUM) {
                error = QStringLiteral("建表失败：area_count=%1，超过最大区域容量 %2")
                            .arg(out.areas.size())
                            .arg(FORCE_LUT_MAX_AREA_NUM);
                return false;
            }
            if (out.force_grid.size() > FORCE_LUT_MAX_FORCE_NUM) {
                error = QStringLiteral("建表失败：force_count=%1，超过单区域 LUT 容量 %2")
                            .arg(out.force_grid.size())
                            .arg(FORCE_LUT_MAX_FORCE_NUM);
                return false;
            }

            out.area_cx.reserve(out.areas.size());
            out.area_cy.reserve(out.areas.size());
            out.sensor_table.resize(out.areas.size() * out.force_grid.size(), 0.0);
            if (out.has_shape_feature) {
                out.area_eff_table.resize(out.areas.size() * out.force_grid.size(), 0.0);
                out.active_count_table.resize(out.areas.size() * out.force_grid.size(), 0.0);
                out.peak_table.resize(out.areas.size() * out.force_grid.size(), 0.0);
            }

            for (size_t a = 0; a < out.areas.size(); ++a) {
                out.area_cx.push_back(out.areas[a].cx);
                out.area_cy.push_back(out.areas[a].cy);
                for (size_t k = 0; k < out.force_grid.size(); ++k) {
                    const size_t idx      = a * out.force_grid.size() + k;
                    out.sensor_table[idx] = interpolateSensorAtForce(out.areas[a], out.force_grid[k]);
                    if (out.has_shape_feature) {
                        out.area_eff_table[idx] =
                            interpolateMapAtForce(out.areas[a].area_eff_by_force, out.force_grid[k]);
                        out.active_count_table[idx] =
                            interpolateMapAtForce(out.areas[a].active_count_by_force, out.force_grid[k]);
                        out.peak_table[idx] =
                            interpolateMapAtForce(out.areas[a].peak_by_force, out.force_grid[k]);
                    }
                }
            }

            return true;
        }

        static bool sendPointLutToMcuForPart(int part_protocol_id, const BuiltLut& lut, QString& error)
        {
            const std::vector<float> area_cx            = toFloatVector(lut.area_cx);
            const std::vector<float> area_cy            = toFloatVector(lut.area_cy);
            const std::vector<float> force_grid         = toFloatVector(lut.force_grid);
            const std::vector<float> sensor_table       = toFloatVector(lut.sensor_table);
            const std::vector<float> area_eff_table     = toFloatVector(lut.area_eff_table);
            const std::vector<float> active_count_table = toFloatVector(lut.active_count_table);
            const std::vector<float> peak_table         = toFloatVector(lut.peak_table);

            const int status = force_lut_load_feature_table_for_part_ex(
                static_cast<uint8_t>(part_protocol_id), static_cast<uint16_t>(std::max(0, lut.sensor_rows)),
                static_cast<uint16_t>(std::max(0, lut.sensor_cols)), static_cast<uint16_t>(area_cx.size()),
                static_cast<uint16_t>(force_grid.size()), area_cx.data(), area_cy.data(), force_grid.data(),
                sensor_table.data(), lut.has_shape_feature ? area_eff_table.data() : nullptr,
                lut.has_shape_feature ? active_count_table.data() : nullptr,
                lut.has_shape_feature ? peak_table.data() : nullptr);
            if (status != FORCE_LUT_OK) {
                error =
                    QStringLiteral(
                        "force_lut_load_area_table_for_part 失败：part=0x%1, area=%2, force=%3, status=%4")
                        .arg(part_protocol_id, 2, 16, QLatin1Char('0'))
                        .arg(area_cx.size())
                        .arg(force_grid.size())
                        .arg(status)
                        .toUpper();
                return false;
            }

            const int slot = partSlot(part_protocol_id);
            if (slot >= 0) g_mcu_lut_loaded_by_part[slot] = true;
            g_mcu_lut_loaded = true;
            return true;
        }

        static void logPointLutSummary(int part_protocol_id, const BuiltLut& lut)
        {
            logi(
                "[模拟MCU] 部位 0x{:02X} 使用增强 area LUT：area_count={}, force_count={}, sample_count={}, shape_feature={}, sensor_size={}x{}",
                part_protocol_id, lut.areaNum(), lut.forceNum(), lut.sample_count,
                lut.has_shape_feature ? "on" : "off", lut.sensor_rows, lut.sensor_cols);
            for (size_t a = 0; a < lut.areas.size(); ++a) {
                logi("[模拟MCU] Area {} center: cx={:.6f}, cy={:.6f}, file={}", a + 1, lut.areas[a].cx,
                     lut.areas[a].cy, lut.areas[a].source.toStdString());
            }
        }

        static int validPointCountForPart(int part_protocol_id)
        {
            switch ((part_protocol_id >> 4) & 0x0f) {
            case 1:  return 158;
            case 2:  return 196;
            case 3:  return 244;
            case 4:  return 196;
            case 5:  return 110;
            case 6:  return 839;
            default: return 0;
            }
        }

        static bool buildWireLut(int part_protocol_id, const BuiltLut& lut, std::vector<float>& adc,
                                 std::vector<float>& force_cn, QString& error)
        {
            const int valid_points = validPointCountForPart(part_protocol_id);
            if (valid_points <= 0 || lut.force_grid.empty() || lut.areas.empty()) {
                error = QStringLiteral("无法生成固件 LUT：部位或表格为空");
                return false;
            }
            for (size_t k = 0; k < lut.force_grid.size(); ++k) {
                std::vector<double> values;
                values.reserve(lut.areas.size());
                for (size_t a = 0; a < lut.areas.size(); ++a)
                    // CSV 采集值、PC 增强表和传感器固件统一使用“当前区域总 AD”。
                    // 这里旧逻辑除以整只传感器的有效点数，既与分区点数不一致，也会使
                    // 下发横轴缩小几十/上百倍，运行时几乎所有按压都会命中 LUT 满量程。
                    values.push_back(lut.sensor_table[a * lut.force_grid.size() + k]);
                std::sort(values.begin(), values.end());
                const size_t mid = values.size() / 2U;
                const double median =
                    values.size() % 2U ? values[mid] : (values[mid - 1U] + values[mid]) * 0.5;
                // 固件要求 AD 严格递增。重复/回落点不参与 V1 一维 LUT，保留原增强 LUT 供上位机测试。
                if (!adc.empty() && median <= static_cast<double>(adc.back()) + 1.0e-5) continue;
                adc.push_back(static_cast<float>(median));
                // 标准力由上位机直接给出，线协议不再做重力单位换算。
                force_cn.push_back(static_cast<float>(lut.force_grid[k]));
            }
            if (adc.size() < 2U) {
                error = QStringLiteral("固件 LUT 有效单调点不足 2 个，请检查采集数据或增加载荷档位");
                return false;
            }
            return true;
        }

    } // namespace

    CalibrationProgramResult CalibrationTestService::simulateCalibrationToMcu(const QString& csv_path)
    {
        return simulateCalibrationToMcu(QStringList() << csv_path);
    }

    CalibrationProgramResult CalibrationTestService::simulateCalibrationToMcu(const QStringList& csv_paths)
    {
        // 兼容旧入口：默认写入 0x11。
        return simulateCalibrationToMcuForPart(0x11, csv_paths);
    }

    CalibrationProgramResult
    CalibrationTestService::simulateCalibrationToMcuForPart(int                part_protocol_id,
                                                            const QStringList& csv_paths)
    {
        CalibrationProgramResult result;
        BuiltLut                 lut;
        QString                  error;

        if (!buildPointLutFromCsvFiles(csv_paths, lut, error)) {
            result.error_message = error;
            loge("模拟MCU失败：部位 0x{:02X}，{}", part_protocol_id, error.toStdString());
            return result;
        }

        if (!sendPointLutToMcuForPart(part_protocol_id, lut, error)) {
            result.error_message = error;
            loge("模拟MCU失败：{}", error.toStdString());
            return result;
        }

        logPointLutSummary(part_protocol_id, lut);

        result.success           = true;
        result.cx_num            = lut.areaNum(); // 兼容旧字段：这里表示 area_count。
        result.cy_num            = 1;             // 兼容旧字段：离散 area 方案不再使用 cy 网格。
        result.force_num         = lut.forceNum();
        result.sample_count      = lut.sample_count;
        result.point_group_count = lut.areaNum();
        result.part_protocol_id  = part_protocol_id;
        result.sensor_rows       = lut.sensor_rows;
        result.sensor_cols       = lut.sensor_cols;
        result.area_num          = lut.areaNum();
        result.has_shape_feature = lut.has_shape_feature;
        result.cx_grid           = toFloatVector(lut.area_cx);
        result.cy_grid           = toFloatVector(lut.area_cy);
        result.force_grid        = toFloatVector(lut.force_grid);
        result.diff_ad_table     = toFloatVector(lut.sensor_table);
        if (!buildWireLut(part_protocol_id, lut, result.wire_adc, result.wire_force_cn,
                          result.error_message)) {
            result.success = false;
        }
        return result;
    }

    CalibrationTestResult CalibrationTestService::runOnce(const CaptureSample& realtime_sample)
    {
        return runOnceForPart(0x11, realtime_sample);
    }

    CalibrationTestResult CalibrationTestService::runOnceForPart(int                  part_protocol_id,
                                                                 const CaptureSample& realtime_sample)
    {
        CalibrationTestResult result;
        const int             slot = partSlot(part_protocol_id);
        if (slot < 0) {
            result.error_message = QStringLiteral("不支持的部位ID：0x%1")
                                       .arg(part_protocol_id, 2, 16, QLatin1Char('0'))
                                       .toUpper();
            return result;
        }
        if (!realtime_sample.valid) {
            result.error_message = QStringLiteral("当前实时样本无效，无法测试");
            return result;
        }
        if (!g_mcu_lut_loaded_by_part[slot] ||
            !force_lut_is_part_loaded(static_cast<uint8_t>(part_protocol_id))) {
            result.error_message =
                QStringLiteral("部位 0x%1 尚未模拟MCU，请先点击“模拟MCU”加载所有部位采集数据")
                    .arg(part_protocol_id, 2, 16, QLatin1Char('0'))
                    .toUpper();
            return result;
        }

        ForceLutRuntimeFeatureC feature{};
        feature.triggered    = realtime_sample.triggered ? 1u : 0u;
        feature.rows         = static_cast<uint16_t>(std::max(0, realtime_sample.rows));
        feature.cols         = static_cast<uint16_t>(std::max(0, realtime_sample.cols));
        feature.sensor_sum   = static_cast<float>(realtime_sample.diff_ad);
        feature.cx           = static_cast<float>(realtime_sample.cx);
        feature.cy           = static_cast<float>(realtime_sample.cy);
        feature.touch_weight = static_cast<float>(realtime_sample.touch_weight);
        feature.peak_value   = static_cast<float>(realtime_sample.peak_value);
        feature.active_count = static_cast<float>(realtime_sample.active_count);
        feature.area_eff     = static_cast<float>(realtime_sample.area_eff);
        feature.var_x        = static_cast<float>(realtime_sample.var_x);
        feature.var_y        = static_cast<float>(realtime_sample.var_y);

        ForceLutPredictDebugC debug;
        const int             status = force_lut_debug_predict_for_part_from_feature(
            static_cast<uint8_t>(part_protocol_id), &feature, &debug);
        if (status != FORCE_LUT_OK || !debug.success) {
            result.error_message = QStringLiteral("force LUT calculation failed: status=%1").arg(status);
            return result;
        }
        // 增强 area LUT：每个 area 先做 1D 反查，再按位置/面积/峰值差异做 KNN 加权融合。
        // 距离阈值仅用于日志提示，不再拒绝预测。

        // 日志保留首邻域表格与融合计算方式。
        logi("==============================");
        logi("ID 0x{:02X} MCU LUT TABLE", part_protocol_id);
        logi(
            "Selected area={} / {}, area_cx={:.6f}, area_cy={:.6f}, input_cx={:.6f}, input_cy={:.6f}, distance={:.6f}{}",
            debug.selected_area + 1, debug.area_num, debug.selected_area_cx, debug.selected_area_cy,
            debug.input_cx, debug.input_cy, debug.selected_area_distance,
            debug.area_reject ? "，距离较远但仍参与KNN融合" : (debug.area_warn ? "，边界区域" : ""));
        logi("Idx\tSensor(diff_ad)\tForce(g)");
        for (uint16_t i = 0; i < debug.force_num; ++i) {
            logi("{}\t{:.6f}\t{:.6f}", i, debug.sensor_curve[i], debug.force_grid[i]);
        }
        logi("------------------------------");
        logi("Predict method: KNN weighted area LUT + 1D reverse lookup; shape_feature={}",
             debug.feature_curve_available ? "on" : "off");
        for (uint8_t ni = 0; ni < debug.used_neighbor_count; ++ni) {
            logi(
                "Neighbor {}: area={}, weight={:.6f}, force={:.6f}, score_distance={:.6f}, expected_area_eff={:.6f}",
                ni + 1, debug.neighbor_area[ni] + 1, debug.neighbor_weight[ni], debug.neighbor_force[ni],
                debug.neighbor_distance[ni], debug.neighbor_expected_area_eff[ni]);
        }
        logi("Sensor(diff_ad) = {:.6f}", debug.input_diff_ad);
        logi("Use LUT[{}] ~ LUT[{}]: sensor {:.6f}->{:.6f}, force {:.6f}->{:.6f}", debug.force_low,
             debug.force_high, debug.sensor_low, debug.sensor_high, debug.force_low_grid,
             debug.force_high_grid);
        logi("t = (sensor_now - sensor_low) / (sensor_high - sensor_low) = {:.6f}", debug.force_t);
        logi("Predict Force = KNN weighted fusion = {:.6f} g", debug.predicted_force);

        result.success         = true;
        result.predicted_force = static_cast<double>(debug.predicted_force);
        result.diff_ad         = realtime_sample.diff_ad;
        result.cx              = realtime_sample.cx;
        result.cy              = realtime_sample.cy;
        result.rows            = realtime_sample.rows;
        result.cols            = realtime_sample.cols;
        return result;
    }

    CalibrationTestResult
    CalibrationTestService::runOnceForPartFromMatrix(int                            part_protocol_id,
                                                     const touch::matrix<uint16_t>& realtime_matrix)
    {
        CalibrationTestResult result;
        const int             slot = partSlot(part_protocol_id);
        if (slot < 0) {
            result.error_message = QStringLiteral("不支持的部位ID：0x%1")
                                       .arg(part_protocol_id, 2, 16, QLatin1Char('0'))
                                       .toUpper();
            return result;
        }
        if (realtime_matrix.empty() || realtime_matrix.rows <= 0 || realtime_matrix.cols <= 0) {
            result.error_message = QStringLiteral("当前实时矩阵为空，无法测试");
            return result;
        }
        if (!g_mcu_lut_loaded_by_part[slot] ||
            !force_lut_is_part_loaded(static_cast<uint8_t>(part_protocol_id))) {
            result.error_message =
                QStringLiteral("部位 0x%1 尚未模拟MCU，请先点击“模拟MCU”加载所有部位采集数据")
                    .arg(part_protocol_id, 2, 16, QLatin1Char('0'))
                    .toUpper();
            return result;
        }

        ForceLutPredictDebugC debug;
        const int             status = force_lut_debug_predict_for_part_from_u16(
            static_cast<uint8_t>(part_protocol_id), realtime_matrix.ptr(),
            static_cast<uint16_t>(realtime_matrix.rows), static_cast<uint16_t>(realtime_matrix.cols),
            &debug);
        if (status != FORCE_LUT_OK || !debug.success) {
            result.error_message =
                QStringLiteral("force LUT raw-matrix calculation failed: status=%1, rows=%2, cols=%3")
                    .arg(status)
                    .arg(realtime_matrix.rows)
                    .arg(realtime_matrix.cols);
            return result;
        }

        logi("==============================");
        logi("ID 0x{:02X} MCU LUT TABLE", part_protocol_id);
        logi("Runtime matrix: rows={}, cols={}, sensor_sum={:.6f}, cx={:.6f}, cy={:.6f}",
             debug.runtime_rows, debug.runtime_cols, debug.input_diff_ad, debug.input_cx, debug.input_cy);
        logi("Selected area={} / {}, area_cx={:.6f}, area_cy={:.6f}, distance={:.6f}{}",
             debug.selected_area + 1, debug.area_num, debug.selected_area_cx, debug.selected_area_cy,
             debug.selected_area_distance,
             debug.area_reject ? "，距离较远但仍参与KNN融合" : (debug.area_warn ? "，边界区域" : ""));
        logi("Idx\tSensor(sensor_sum)\tForce(g)");
        for (uint16_t i = 0; i < debug.force_num; ++i) {
            logi("{}\t{:.6f}\t{:.6f}", i, debug.sensor_curve[i], debug.force_grid[i]);
        }
        logi("------------------------------");
        logi(
            "Predict method: MCU C/H extracts features from raw[rows*cols], then uses KNN weighted area LUT + 1D reverse lookup; shape_feature={}",
            debug.feature_curve_available ? "on" : "off");
        logi(
            "Runtime features: peak={:.6f}, active_count={:.0f}, area_eff={:.6f}, var_x={:.6f}, var_y={:.6f}",
            debug.input_peak, debug.input_active_count, debug.input_area_eff, debug.input_var_x,
            debug.input_var_y);
        for (uint8_t ni = 0; ni < debug.used_neighbor_count; ++ni) {
            logi(
                "Neighbor {}: area={}, weight={:.6f}, force={:.6f}, score_distance={:.6f}, expected_area_eff={:.6f}",
                ni + 1, debug.neighbor_area[ni] + 1, debug.neighbor_weight[ni], debug.neighbor_force[ni],
                debug.neighbor_distance[ni], debug.neighbor_expected_area_eff[ni]);
        }
        logi("Use LUT[{}] ~ LUT[{}]: sensor {:.6f}->{:.6f}, force {:.6f}->{:.6f}", debug.force_low,
             debug.force_high, debug.sensor_low, debug.sensor_high, debug.force_low_grid,
             debug.force_high_grid);
        logi("t = (sensor_now - sensor_low) / (sensor_high - sensor_low) = {:.6f}", debug.force_t);
        logi("Predict Force = KNN weighted fusion = {:.6f} g", debug.predicted_force);

        result.success         = true;
        result.predicted_force = static_cast<double>(debug.predicted_force);
        result.diff_ad         = static_cast<double>(debug.input_diff_ad);
        result.cx              = static_cast<double>(debug.input_cx);
        result.cy              = static_cast<double>(debug.input_cy);
        result.rows            = static_cast<int>(debug.runtime_rows);
        result.cols            = static_cast<int>(debug.runtime_cols);
        return result;
    }

    bool CalibrationTestService::hasLoadedLut() { return g_mcu_lut_loaded; }

    bool CalibrationTestService::hasLoadedLutForPart(int part_protocol_id)
    {
        const int slot = partSlot(part_protocol_id);
        return slot >= 0 && g_mcu_lut_loaded_by_part[slot] &&
               force_lut_is_part_loaded(static_cast<uint8_t>(part_protocol_id));
    }

    void CalibrationTestService::clearLoadedLut()
    {
        g_mcu_lut_loaded = false;
        for (bool& loaded : g_mcu_lut_loaded_by_part) loaded = false;
        force_lut_clear_all_parts();
        logi("MCU force LUT runtime state cleared");
    }

} // namespace app
