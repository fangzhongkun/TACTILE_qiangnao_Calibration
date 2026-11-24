#include "statistics-parameters-dialog.h"

#include "combobox.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTimeEdit>

StatisticsParametersDialog::StatisticsParametersDialog(QWidget *parent) : FormDialog(parent)
{
    setWindowTitle(tr("Parameters"));

    // max points
    capacity_ = new QSpinBox();
    capacity_->setRange(4, 65535);

    form()->addRow(tr("Max Points"), capacity_);

    // internal
    internal_ = new QSpinBox();
    internal_->setRange(0, 3600000); // 1 hour = 60 * 60 * 1000 ms = 3600 000
    form()->addRow(tr("Internal(ms)"), internal_);
}

void StatisticsParametersDialog::setCapacity(int capacity) { capacity_->setValue(capacity); }

void StatisticsParametersDialog::setInternal(int internal) { internal_->setValue(internal); }

size_t StatisticsParametersDialog::capacity() const { return capacity_->value(); }

int64_t StatisticsParametersDialog::internal() const { return internal_->value(); }