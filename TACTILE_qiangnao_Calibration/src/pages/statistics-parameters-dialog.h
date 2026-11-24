#ifndef SCANNER_STATISTICS_PARAMETERS_DIALOG_H
#define SCANNER_STATISTICS_PARAMETERS_DIALOG_H

#include "form-dialog.h"

class QSpinBox;

class StatisticsParametersDialog final : public FormDialog
{
    Q_OBJECT
public:
    explicit StatisticsParametersDialog(QWidget *parent = nullptr);

    void setCapacity(int capacity);

    void setInternal(int internal);

    [[nodiscard]] size_t capacity() const;

    [[nodiscard]] int64_t internal() const;

private:
    QSpinBox *capacity_{};
    QSpinBox *internal_{};
};

#endif //! SCANNER_STATISTICS_PARAMETERS_DIALOG_H