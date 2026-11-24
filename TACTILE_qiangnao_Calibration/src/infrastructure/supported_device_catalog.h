#ifndef SUPPORTED_DEVICE_CATALOG_H
#define SUPPORTED_DEVICE_CATALOG_H

#include "../force-calibration/product_model.h"

#include <QString>
#include <utility>
#include <vector>

namespace infra {

inline std::vector<std::pair<int, QString>> supportedDeviceCatalog()
{
    return {
        { static_cast<int>(ProductModel::ZY_T2_XP), QStringLiteral("章鱼动力") },
        { static_cast<int>(ProductModel::OP3),      QStringLiteral("OP3") },
        { static_cast<int>(ProductModel::BRAINCO_GLOVE_V2), QStringLiteral("强脑") },
        { static_cast<int>(ProductModel::HAOCUN_GLOVE), QStringLiteral("灏存") }
    };
}

inline bool isSupportedDeviceType(int device_type)
{
    return productModelFromId(static_cast<uint8_t>(device_type)) != ProductModel::Unknown;
}

inline QString deviceTypeName(int device_type)
{
    return productModelToString(productModelFromId(static_cast<uint8_t>(device_type)));
}

} // namespace infra

#endif // SUPPORTED_DEVICE_CATALOG_H
