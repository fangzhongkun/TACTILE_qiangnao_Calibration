#ifndef SCANNER_UTILS_H
#define SCANNER_UTILS_H

#pragma once

#include <array>
#include <libtouch/matrix.h>
#include <QString>
#define QTIMESTAMP (QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss_zzz"))
namespace touch
{
    template<typename T> class matrix;
}

class Utils
{
public:
    static bool convert_to_template_by_csv(const touch::matrix<uint16_t>& src, touch::matrix<uint16_t>& dst,
                                           uint32_t part_id = 0U);
    static bool convert_to_brainco_template_by_csv(const touch::matrix<uint16_t>& src,
                                                   touch::matrix<uint16_t>& dst, uint32_t part_id);

    static bool is_valid_cell_by_csv(int row, int col, uint32_t part_id = 0U);
    static bool is_csv_template_size(int rows, int cols, uint32_t part_id = 0U);
    static bool is_valid_cell_by_brainco_csv(int row, int col, uint32_t part_id);
    static bool is_brainco_csv_template_size(int rows, int cols, uint32_t part_id);

    static int get_region_id_by_csv(int row, int col, uint32_t part_id = 0U);
    static int get_region_count_by_csv(uint32_t part_id = 0U);
    static int get_region_point_count_by_csv(int region_id, uint32_t part_id = 0U);
    static int get_region_rows_by_csv(uint32_t part_id = 0U);
    static int get_region_cols_by_csv(uint32_t part_id = 0U);

private:
    struct csv_template_info
    {
        int                rows = 0;
        int                cols = 0;
        touch::matrix<int> map;
        bool               valid = false;
    };

    struct csv_region_info
    {
        int                rows = 0;
        int                cols = 0;
        touch::matrix<int> region_map;
        std::array<int, 7> region_point_count{};
        int                region_count = 0;
        bool               valid        = false;
    };

    static const csv_template_info& get_cached_csv_template(uint32_t part_id = 0U);
    static const csv_template_info& get_cached_brainco_csv_template(uint32_t part_id);
    static bool load_template_from_csv(const QString& file_path, csv_template_info& info);

    static const csv_region_info& get_cached_csv_region(uint32_t part_id = 0U);
    static bool                   load_region_from_csv(const QString& file_path, csv_region_info& info);
};
#endif //! SCANNER_UTILS_H
