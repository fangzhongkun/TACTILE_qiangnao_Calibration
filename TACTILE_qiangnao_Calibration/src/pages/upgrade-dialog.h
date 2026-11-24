#ifndef SCANNER_UPGRADE_DIALOG_H
#define SCANNER_UPGRADE_DIALOG_H

#include "whisper/whisper.h"

#include <QDialog>

class PathEdit;
class QProgressBar;
class QLabel;

class UpgradeDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit UpgradeDialog(const std::shared_ptr<whisper::whisper>&, QWidget *parent = nullptr);

    ~UpgradeDialog() override
    {
        if (thread_.joinable()) thread_.join();
    }

signals:
    void percent(int);

public slots:
    void transmitting(const std::string& file);
    void success();
    void failed(const std::string&);

private:
    std::shared_ptr<whisper::whisper> whisper_{};
    std::thread                       thread_{};
    PathEdit                         *path_{};
    QProgressBar                     *percent_{};
    QLabel                           *status_{};
};

#endif //! SCANNER_UPGRADE_DIALOG_H