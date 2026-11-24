#include "utils.h"

#include <map>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <vector>

namespace
{
    const uint32_t *finger_valid_masks(uint32_t part_id, int& columns)
    {
        static const uint32_t thumb[]      = { 0x00000600UL, 0x00000F00UL, 0x00000F00UL, 0x00000F00UL,
                                               0x0000C718UL, 0x0001C738UL, 0x0001FFF8UL, 0x0003FFFCUL,
                                               0x0003FFFCUL, 0x0007FFFEUL, 0x0001FFF8UL, 0x00007FE0UL,
                                               0x000FFFFFUL, 0x000FFFFFUL };
        static const uint32_t index_ring[] = { 0x00000080UL, 0x000001C0UL, 0x000001C0UL, 0x00002188UL,
                                               0x0000318CUL, 0x000039CEUL, 0x00003FFEUL, 0x00003FFEUL,
                                               0x00001FFCUL, 0x000003E0UL, 0x0000FFFFUL, 0x0000FFFFUL,
                                               0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL,
                                               0x0000FFFFUL, 0x0000FFFFUL };
        static const uint32_t middle[]     = { 0x00000080UL, 0x000001C0UL, 0x000001C0UL, 0x00002188UL,
                                               0x0000318CUL, 0x000039CEUL, 0x00003FFEUL, 0x00003FFEUL,
                                               0x00001FFCUL, 0x000003E0UL, 0x0000FFFFUL, 0x0000FFFFUL,
                                               0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL,
                                               0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL,
                                               0x0000FFFFUL };
        static const uint32_t pinky[]      = { 0x00000380UL, 0x00000380UL, 0x00000380UL, 0x00001188UL,
                                               0x00003198UL, 0x00003FF8UL, 0x00003FF8UL, 0x000007C0UL,
                                               0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL, 0x0000FFFFUL };
        switch ((part_id >> 4U) & 0x0FU) {
        case 1U: columns = static_cast<int>(std::size(thumb)); return thumb;
        case 2U:
        case 4U: columns = static_cast<int>(std::size(index_ring)); return index_ring;
        case 3U: columns = static_cast<int>(std::size(middle)); return middle;
        case 5U: columns = static_cast<int>(std::size(pinky)); return pinky;
        default: columns = 0; return nullptr;
        }
    }

    QString find_config_csv(const QString& file_name)
    {
        const QString app_dir = QCoreApplication::applicationDirPath();

        const QStringList candidates = {
            QDir(app_dir).filePath(QStringLiteral("config/") + file_name),
            QDir::current().filePath(QStringLiteral("config/") + file_name),

            // 开发环境兼容：未安装、直接从 build 目录运行时使用源码目录下的 resources/config。
            QDir(app_dir).filePath(QStringLiteral("../resources/config/") + file_name),
            QDir::current().filePath(QStringLiteral("../resources/config/") + file_name),
            // Visual Studio 多配置生成器把程序放在 build/Release 或 build/Debug，
            // 从该目录访问源码配置需要再向上一级。
            QDir(app_dir).filePath(QStringLiteral("../../resources/config/") + file_name),
            QDir::current().filePath(QStringLiteral("../../resources/config/") + file_name),

            // 最后兜底：如果以后重新放回 qrc，也可以读取资源内置文件。
            QStringLiteral(":/config/") + file_name
        };

        for (const QString& path : candidates) {
            if (QFileInfo::exists(path)) {
                return path;
            }
        }

        // 返回安装目录下的目标路径，便于调用方失败时路径含义明确。
        return QDir(app_dir).filePath(QStringLiteral("config/") + file_name);
    }
} // namespace

bool Utils::load_template_from_csv(const QString& file_path, csv_template_info& info)
{
    info = csv_template_info{};

    QFile file(file_path);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QList<QStringList> csv_rows;
    int                max_cols       = 0;
    int                raw_line_count = 0;

    while (!file.atEnd()) {
        QByteArray line_data = file.readLine();
        ++raw_line_count;

        QString line = QString::fromUtf8(line_data);
        if (raw_line_count == 1) {
            line.remove(QChar(0xFEFF));
        }

        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QStringList cells = line.split(',', Qt::KeepEmptyParts);
        for (QString& cell : cells) {
            cell = cell.trimmed();
        }

        csv_rows.append(cells);
        if (cells.size() > max_cols) {
            max_cols = cells.size();
        }
    }

    file.close();

    if (csv_rows.isEmpty() || max_cols <= 0) {
        return false;
    }

    int row_count = static_cast<int>(csv_rows.size());
    int col_count = max_cols;

    info.rows = row_count;
    info.cols = col_count;
    info.map  = touch::matrix<int>(row_count, col_count);

    for (int row = 0; row < row_count; ++row) {
        for (int col = 0; col < col_count; ++col) {
            info.map(row, col) = 0;
        }
    }

    for (int row = 0; row < row_count; ++row) {
        const QStringList& cells = csv_rows[row];

        for (int col = 0; col < cells.size(); ++col) {
            const QString& cell = cells[col];

            if (cell.isEmpty()) {
                info.map(row, col) = 0;
                continue;
            }

            bool ok    = false;
            int  index = cell.toInt(&ok);

            if (!ok || index <= 0) {
                info.map(row, col) = 0;
            }
            else {
                info.map(row, col) = index;
            }
        }
    }

    info.valid = true;
    return true;
}

const Utils::csv_template_info& Utils::get_cached_csv_template(uint32_t part_id)
{
    static std::map<uint32_t, csv_template_info> templates;
    auto [it, inserted] = templates.try_emplace(part_id);
    if (inserted) {
        const uint32_t protocol_id = part_id <= 6U ? ((part_id << 4U) | 1U) : part_id;
        const QString  hex = QStringLiteral("%1").arg(protocol_id, 2, 16, QLatin1Char('0')).toUpper();
        QString        english_name;
        QString        chinese_name;
        switch ((protocol_id >> 4U) & 0x0FU) {
        case 1U:
            english_name = QStringLiteral("thumb");
            chinese_name = QStringLiteral("大拇指");
            break;
        case 2U:
            english_name = QStringLiteral("index");
            chinese_name = QStringLiteral("食指");
            break;
        case 3U:
            english_name = QStringLiteral("middle");
            chinese_name = QStringLiteral("中指");
            break;
        case 4U:
            english_name = QStringLiteral("ring");
            chinese_name = QStringLiteral("无名指");
            break;
        case 5U:
            english_name = QStringLiteral("pinky");
            chinese_name = QStringLiteral("小指");
            break;
        case 6U:
            english_name = QStringLiteral("palm");
            chinese_name = QStringLiteral("掌心");
            break;
        default: break;
        }
        const QStringList names = {
            QStringLiteral("id_%1/mapping.csv").arg(hex), QStringLiteral("0x%1/mapping.csv").arg(hex),
            QStringLiteral("%1/mapping.csv").arg(hex), english_name + QStringLiteral("/mapping.csv"),
            chinese_name + QStringLiteral("/mapping.csv"), QStringLiteral("mapping_%1.csv").arg(hex),
            QStringLiteral("mapping_0x%1.csv").arg(hex),
            // 兼容原先只有一个公共 mapping.csv 的工程。
            QStringLiteral("mapping.csv")
        };
        for (const QString& name : names) {
            if (load_template_from_csv(find_config_csv(name), it->second)) break;
        }
    }
    return it->second;
}

const Utils::csv_template_info& Utils::get_cached_brainco_csv_template(uint32_t part_id)
{
    static std::map<uint32_t, csv_template_info> templates;
    const uint32_t base_id = (part_id & 0xF0U) | 0x01U;
    auto [it, inserted] = templates.try_emplace(base_id);
    if (inserted) {
        const QString hex = QStringLiteral("%1").arg(base_id, 2, 16, QLatin1Char('0')).toUpper();
        load_template_from_csv(find_config_csv(QStringLiteral("mapping_brainco_%1.csv").arg(hex)),
                               it->second);
    }
    return it->second;
}

bool Utils::convert_to_brainco_template_by_csv(const touch::matrix<uint16_t>& src,
                                               touch::matrix<uint16_t>& dst, uint32_t part_id)
{
    const csv_template_info& info = get_cached_brainco_csv_template(part_id);
    if (!info.valid || info.rows != src.rows || info.cols != src.cols) return false;
    dst = touch::matrix<uint16_t>(info.rows, info.cols, uint16_t{});
    for (int row = 0; row < info.rows; ++row)
        for (int col = 0; col < info.cols; ++col)
            if (info.map(row, col) > 0) dst(row, col) = src(row, col);
    return true;
}

bool Utils::is_valid_cell_by_brainco_csv(int row, int col, uint32_t part_id)
{
    const csv_template_info& info = get_cached_brainco_csv_template(part_id);
    return info.valid && row >= 0 && row < info.rows && col >= 0 && col < info.cols &&
           info.map(row, col) > 0;
}

bool Utils::is_brainco_csv_template_size(int rows, int cols, uint32_t part_id)
{
    const csv_template_info& info = get_cached_brainco_csv_template(part_id);
    return info.valid && info.rows == rows && info.cols == cols;
}

bool Utils::convert_to_template_by_csv(const touch::matrix<uint16_t>& src, touch::matrix<uint16_t>& dst,
                                       uint32_t part_id)
{
    const csv_template_info& info = get_cached_csv_template(part_id);
    if (!info.valid || info.rows <= 0 || info.cols <= 0) {
        return false;
    }

    if (src.rows <= 0 || src.cols <= 0) {
        return false;
    }

    std::vector<uint16_t> compact;
    int                   mask_columns = 0;
    const uint32_t       *masks        = finger_valid_masks(part_id, mask_columns);
    // 旧版 F103 会把部分手指补齐为更宽的矩形（例如拇指 14 个物理列按 16 列上传）。
    // 掩码列数才是真实采样列数，末尾协议填充列必须丢弃，否则 point id 会整体错位。
    if (masks != nullptr && src.rows > 1 && src.cols >= mask_columns) {
        for (int col = 0; col < mask_columns; ++col) {
            for (int row = 0; row < src.rows; ++row) {
                if ((masks[col] & (1UL << row)) != 0UL) compact.push_back(src(row, col));
            }
        }
    }

    dst = touch::matrix<uint16_t>(info.rows, info.cols);

    for (int row = 0; row < info.rows; ++row) {
        for (int col = 0; col < info.cols; ++col) {
            int value = info.map(row, col);

            if (value <= 0) {
                dst(row, col) = 0;
                continue;
            }

            int index = value - 1;
            if (!compact.empty()) {
                dst(row, col) = index < static_cast<int>(compact.size()) ? compact[index] : 0U;
            }
            else {
                int src_row   = index / src.cols;
                int src_col   = index % src.cols;
                dst(row, col) = (src_row >= 0 && src_row < src.rows && src_col >= 0 && src_col < src.cols)
                                    ? src(src_row, src_col)
                                    : 0U;
            }
        }
    }

    return true;
}

bool Utils::is_valid_cell_by_csv(int row, int col, uint32_t part_id)
{
    const csv_template_info& info = get_cached_csv_template(part_id);
    if (!info.valid) {
        return false;
    }

    if (row < 0 || row >= info.rows || col < 0 || col >= info.cols) {
        return false;
    }

    return info.map(row, col) > 0;
}

bool Utils::is_csv_template_size(int rows, int cols, uint32_t part_id)
{
    const csv_template_info& info = get_cached_csv_template(part_id);
    if (!info.valid) {
        return false;
    }

    return info.rows == rows && info.cols == cols;
}

bool Utils::load_region_from_csv(const QString& file_path, csv_region_info& info)
{
    info = csv_region_info{};

    QFile file(file_path);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QList<QStringList> csv_rows;
    int                max_cols       = 0;
    int                raw_line_count = 0;

    while (!file.atEnd()) {
        QByteArray line_data = file.readLine();
        ++raw_line_count;

        QString line = QString::fromUtf8(line_data);
        if (raw_line_count == 1) {
            line.remove(QChar(0xFEFF));
        }

        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QStringList cells = line.split(',', Qt::KeepEmptyParts);
        for (QString& cell : cells) {
            cell = cell.trimmed();
        }

        csv_rows.append(cells);
        if (cells.size() > max_cols) {
            max_cols = cells.size();
        }
    }

    file.close();

    if (csv_rows.isEmpty() || max_cols <= 0) {
        return false;
    }

    int row_count = static_cast<int>(csv_rows.size());
    int col_count = max_cols;

    info.rows       = row_count;
    info.cols       = col_count;
    info.region_map = touch::matrix<int>(row_count, col_count);
    info.region_point_count.fill(0);
    info.region_count = 0;

    for (int row = 0; row < row_count; ++row) {
        for (int col = 0; col < col_count; ++col) {
            info.region_map(row, col) = 0;
        }
    }

    for (int row = 0; row < row_count; ++row) {
        const QStringList& cells = csv_rows[row];

        for (int col = 0; col < cells.size(); ++col) {
            const QString& cell = cells[col];

            if (cell.isEmpty()) {
                info.region_map(row, col) = 0;
                continue;
            }

            bool ok        = false;
            int  region_id = cell.toInt(&ok);

            if (!ok || region_id < 0 || region_id > 6) {
                info.region_map(row, col) = 0;
            }
            else {
                info.region_map(row, col) = region_id;
                if (region_id > 0) {
                    ++info.region_point_count[region_id];
                    if (region_id > info.region_count) {
                        info.region_count = region_id;
                    }
                }
            }
        }
    }

    info.valid = true;
    return true;
}

const Utils::csv_region_info& Utils::get_cached_csv_region(uint32_t part_id)
{
    static std::map<uint32_t, csv_region_info> regions;
    auto [it, inserted] = regions.try_emplace(part_id);
    if (inserted) {
        const uint32_t    protocol_id = part_id <= 6U ? ((part_id << 4U) | 1U) : part_id;
        const QString     hex   = QStringLiteral("%1").arg(protocol_id, 2, 16, QLatin1Char('0')).toUpper();
        const QStringList names = { QStringLiteral("region_%1.csv").arg(hex),
                                    QStringLiteral("region_0x%1.csv").arg(hex),
                                    QStringLiteral("id_%1/region.csv").arg(hex),
                                    QStringLiteral("region.csv") };
        for (const QString& name : names) {
            if (load_region_from_csv(find_config_csv(name), it->second)) break;
        }
    }
    return it->second;
}

int Utils::get_region_id_by_csv(int row, int col, uint32_t part_id)
{
    const csv_region_info& info = get_cached_csv_region(part_id);
    if (!info.valid) {
        return 0;
    }

    if (row < 0 || row >= info.rows || col < 0 || col >= info.cols) {
        return 0;
    }

    return info.region_map(row, col);
}

int Utils::get_region_count_by_csv(uint32_t part_id)
{
    const csv_region_info& info = get_cached_csv_region(part_id);
    if (!info.valid) {
        return 0;
    }

    return info.region_count;
}

int Utils::get_region_point_count_by_csv(int region_id, uint32_t part_id)
{
    const csv_region_info& info = get_cached_csv_region(part_id);
    if (!info.valid) {
        return 0;
    }

    if (region_id < 1 || region_id > 6) {
        return 0;
    }

    return info.region_point_count[region_id];
}

int Utils::get_region_rows_by_csv(uint32_t part_id)
{
    const csv_region_info& info = get_cached_csv_region(part_id);
    return info.valid ? info.rows : 0;
}

int Utils::get_region_cols_by_csv(uint32_t part_id)
{
    const csv_region_info& info = get_cached_csv_region(part_id);
    return info.valid ? info.cols : 0;
}
