#ifndef SCANNER_DEVICE_PROPERTIES_DIALOG_H
#define SCANNER_DEVICE_PROPERTIES_DIALOG_H

#include "devices/device.h"

#include <QDialog>

class DevicePropertiesDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DevicePropertiesDialog(const Device *device, QWidget *parent = nullptr);
};

#endif //! SCANNER_DEVICE_PROPERTIES_DIALOG_H