#include "device-properties-dialog.h"

#include "fmt/format.h"
#include "frameless/frameless-maker.h"

#include <QHeaderView>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

DevicePropertiesDialog::DevicePropertiesDialog(const Device *device, QWidget *parent) : QDialog(parent)
{
    const auto titlebar = new TitleBar(this);
    new FramelessMaker(titlebar, this);

    setContentsMargins({});
    setWindowModality(Qt::ApplicationModal);

    setMinimumSize(320, 400);

    const auto layout = new QVBoxLayout();
    layout->setSpacing(0);
    layout->setContentsMargins({});
    setLayout(layout);

    layout->addWidget(titlebar);
    setWindowTitle(tr("Device Properties"));

    const auto view = new QTreeView();
    view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view->setHeaderHidden(true);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(view);

    const auto model = new QStandardItemModel();

    if (device) {
        model->appendRow(
            { new QStandardItem(tr("SN")), new QStandardItem(device->serial_number().c_str()) });
        model->appendRow({ new QStandardItem(tr("Address")),
                           new QStandardItem(fmt::format("{:#04x}", device->address()).c_str()) });

        model->appendRow({ new QStandardItem(tr("Version")),
                           new QStandardItem(probe::to_string(device->version()).c_str()) });
        model->appendRow({ new QStandardItem(tr("Protocol")), new QStandardItem(device->bus().c_str()) });

        model->appendRow({ new QStandardItem(tr("Protocol Version")),
                           new QStandardItem(probe::to_string(device->bus_version()).c_str()) });

        auto [min, max] = device->range();
        model->appendRow({ new QStandardItem(tr("Data Range")),
                           new QStandardItem(QString("%1 ~ %2").arg(min).arg(max)) });

        for (const auto& [id, sensor] : device->sensors()) {
            auto item = new QStandardItem(QString{ "Sensor #%1" }.arg(id));
            model->appendRow({ item, new QStandardItem() });

            item->appendRow(
                { new QStandardItem(tr("Resolution")),
                  new QStandardItem(QString{ "%1 x %2" }.arg(sensor.width).arg(sensor.height)) });
            item->appendRow({ new QStandardItem(tr("Data Format")),
                              new QStandardItem(QString::number(sensor.format)) });
            item->appendRow(
                { new QStandardItem(tr("Physical Size")),
                  new QStandardItem(
                      QString{ "%1 x %2" }.arg(sensor.physical_width).arg(sensor.physical_height)) });
            item->appendRow(
                { new QStandardItem(tr("SAR")),
                  new QStandardItem(QString("%1 / %2").arg(sensor.sar.num).arg(sensor.sar.den)) });
        }
    }

    view->setModel(model);
    view->expandAll();
}