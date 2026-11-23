#ifndef SCANNER_OPEN_DEVICE_DIALOG_H
#define SCANNER_OPEN_DEVICE_DIALOG_H

#include "form-dialog.h"
#include "serial/options.h"

class ComboBox;

class OpenDeviceDialog final : public FormDialog
{
    Q_OBJECT

public:
    explicit OpenDeviceDialog(QWidget *parent = nullptr);

    [[nodiscard]] serial::options options() const;
    [[nodiscard]] int             device() const;

private:
    ComboBox *ports_{};
    ComboBox *baudrate_{};
    ComboBox *databits_{};
    ComboBox *stopbits_{};
    ComboBox *parity_{};
    ComboBox *device_{};
};

#endif //! SCANNER_OPEN_DEVICE_DIALOG_H