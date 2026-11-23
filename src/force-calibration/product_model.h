#ifndef PRODUCT_MODEL_H
#define PRODUCT_MODEL_H

#include <cstdint>
#include <QString>

enum class ProductModel : uint8_t
{
    ZY_T2_XP = 3,
    OP3      = 5,
    BRAINCO_GLOVE_V2 = 6,
    HAOCUN_GLOVE = 7,
    Unknown  = 255
};

inline QString productModelToString(ProductModel model)
{
    switch (model) {
    case ProductModel::ZY_T2_XP: return QStringLiteral("章鱼动力");
    case ProductModel::OP3:      return QStringLiteral("OP3");
    case ProductModel::BRAINCO_GLOVE_V2: return QStringLiteral("强脑");
    case ProductModel::HAOCUN_GLOVE: return QStringLiteral("灏存");
    default:                     return QStringLiteral("NULL");
    }
}

inline ProductModel productModelFromId(uint8_t id)
{
    switch (id) {
    case static_cast<uint8_t>(ProductModel::ZY_T2_XP): return ProductModel::ZY_T2_XP;
    case static_cast<uint8_t>(ProductModel::OP3):      return ProductModel::OP3;
    case static_cast<uint8_t>(ProductModel::BRAINCO_GLOVE_V2): return ProductModel::BRAINCO_GLOVE_V2;
    case static_cast<uint8_t>(ProductModel::HAOCUN_GLOVE): return ProductModel::HAOCUN_GLOVE;
    default:                                           return ProductModel::Unknown;
    }
}

#endif // PRODUCT_MODEL_H
