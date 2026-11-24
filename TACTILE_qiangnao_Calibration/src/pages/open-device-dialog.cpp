#include "open-device-dialog.h"

#include "combobox.h"
#include "infrastructure/supported_device_catalog.h"

#include <probe/serial-port.h>
#include <QFormLayout>

OpenDeviceDialog::OpenDeviceDialog(QWidget *parent) : FormDialog(parent)
{
    setWindowTitle(tr("Open Device"));

    // serial ports
    ports_ = new ComboBox();
    for (const auto ports = probe::port::ports(); const auto& port : ports) {
        ports_->add(QString::fromUtf8(port.device.c_str()),
                    QString::fromUtf8((port.name + " (" + port.description + ")").c_str()));
    }
    ports_->select(std::string("COM4"));
    form()->addRow(tr("Port"), ports_);

    // baud rate
    baudrate_ = new ComboBox();
    baudrate_
        ->add({
            { 3000000, "3000000" },
            { 2000000, "2000000" },
            { 1500000, "1500000" },
            { 921600, "921600" },
            { 460800, "460800" },
            { 256000, "256000" },
            { 115200, "115200" },
            { 57600, "57600" },
            { 38400, "38400" },
            { 19200, "19200" },
            { 9600, "9600" },
            { 4800, "4800" },
            { 2400, "2400" },
            { 1200, "1200" },
        })
        .select(921600);
    form()->addRow(tr("Baud Rate"), baudrate_);

    // data bits
    databits_ = new ComboBox();
    databits_->add({ { 5, "5" }, { 6, "6" }, { 7, "7" }, { 8, "8" } }).select(8);
    form()->addRow(tr("Data Bits"), databits_);

    // stop bits
    stopbits_ = new ComboBox();
    stopbits_->add({ { 0, "1" }, { 1, "1.5" }, { 2, "2" } });
    form()->addRow(tr("Stop Bits"), stopbits_);

    // parity
    parity_ = new ComboBox();
    parity_->add({ { 0, "None" }, { 1, "Odd" }, { 2, "Even" }, { 3, "Mark" }, { 4, "Space" } });
    form()->addRow(tr("Parity"), parity_);

    // device type
    device_ = new ComboBox();
    device_->add(infra::supportedDeviceCatalog());
    device_->onselected([this](const QVariant& value) {
        const auto model = productModelFromId(static_cast<uint8_t>(value.toUInt()));
        if (model == ProductModel::BRAINCO_GLOVE_V2 || model == ProductModel::HAOCUN_GLOVE) {
            ports_->select(std::string("COM4"));
            baudrate_->select(2000000);
        }
    });
    form()->addRow(tr("Format"), device_);
}

serial::options OpenDeviceDialog::options() const
{
    return serial::options{
        .port     = ports_->currentData().toString().toStdString(),
        .baudrate = static_cast<uint32_t>(baudrate_->currentData().toUInt()),
        .databits = static_cast<uint8_t>(databits_->currentData().toUInt()),
        .stopbits = static_cast<uint8_t>(stopbits_->currentData().toUInt()),
        .parity   = static_cast<uint8_t>(parity_->currentData().toUInt()),
    };
}

int OpenDeviceDialog::device() const { return device_->currentData().toInt(); }
