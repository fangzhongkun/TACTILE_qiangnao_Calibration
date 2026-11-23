#ifndef SCANNER_ADDRESS_DIALOG_H
#define SCANNER_ADDRESS_DIALOG_H

#include "devices/device.h"

#include <QDialog>

class QSpinBox;
class QLineEdit;

class AddressDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AddressDialog(Device *, QWidget *parent = nullptr);

signals:
    void changed();

private:
    QString sn_{};
    uint8_t address_{};

    QLineEdit *sn_label_{};
    QSpinBox  *address_box_{};

    std::jthread thread_{};
};

#endif //! SCANNER_ADDRESS_DIALOG_H