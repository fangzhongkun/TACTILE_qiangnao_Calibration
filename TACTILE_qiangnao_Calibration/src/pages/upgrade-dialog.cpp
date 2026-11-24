#include "upgrade-dialog.h"

#include "frameless/frameless-maker.h"
#include "logging.h"
#include "message.h"
#include "path-edit.h"

#include <fstream>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>

UpgradeDialog::UpgradeDialog(const std::shared_ptr<whisper::whisper>& whisper, QWidget *parent)
    : QDialog(parent), whisper_(whisper)
{
    const auto titlebar = new TitleBar(this);
    new FramelessMaker(titlebar, this);

    setContentsMargins({});
    setWindowModality(Qt::ApplicationModal);

    setMinimumWidth(475);

    const auto layout = new QVBoxLayout();
    layout->setSpacing(0);
    layout->setContentsMargins({});
    setLayout(layout);

    layout->addWidget(titlebar);

    setWindowTitle(tr("Firmware Upgrade"));

    {
        const auto vbox = new QVBoxLayout();
        vbox->setContentsMargins({ 15, 15, 15, 10 });
        vbox->setSpacing(10);
        layout->addLayout(vbox);

        {
            {
                const auto hbox = new QHBoxLayout();
                hbox->setContentsMargins({});
                hbox->setSpacing(10);
                vbox->addLayout(hbox);

                hbox->addWidget(new QLabel("Select"));

                path_ = new PathEdit("", true, this);
                hbox->addWidget(path_);
            }

            //

            percent_ = new QProgressBar(this);
            percent_->setRange(0, 100);
            percent_->setValue(20);
            vbox->addWidget(percent_);

            {
                const auto hbox = new QHBoxLayout();
                hbox->setContentsMargins({});

                hbox->addStretch();

                const auto upgrade = new QPushButton(tr("Upgrade"), this);
                hbox->addWidget(upgrade, 0, Qt::AlignRight);

                vbox->addLayout(hbox);

                connect(upgrade, &QPushButton::clicked,
                        [this] { transmitting(path_->path().toStdString()); });

                connect(path_, &PathEdit::changed, this, [this] { percent_->setValue(0); });

                connect(this, &UpgradeDialog::percent, this,
                        [this](int value) { percent_->setValue(value); });
            }
        }
    }
}

void UpgradeDialog::transmitting(const std::string& file)
{
    if (thread_.joinable()) thread_.join();

    thread_ = std::thread([=, this] {
        probe::thread::set_name("UPGRADE");

        whisper_->get(
            0x09, { 0x01 },
            [=, this](whisper::payload& res) {
                if (res.read<uint8_t>() == 0x01) {
                    std::ifstream in(file, std::ios::in | std::ios::binary);
                    constexpr int BUFFER_SIZE = 2048;

                    if (in.is_open()) {
                        auto size = std::filesystem::file_size(file);
                        auto N    = (size + BUFFER_SIZE - 1) / BUFFER_SIZE;

                        logi("file: {}, size: {}", file, size);

                        for (uint16_t i = 0; i < N; i++) {
                            char   buffer[BUFFER_SIZE + 2]{};
                            size_t bytes = std::min<size_t>(size - i * BUFFER_SIZE, BUFFER_SIZE);

                            *reinterpret_cast<uint16_t *>(buffer) = i;
                            in.read(buffer + 2, bytes);

                            logi("sending {}/{}: {}", i + 1, N, bytes);

                            whisper_->get(
                                0x0A, { buffer, bytes },
                                [=, this](whisper::payload& res) {
                                    if (res.read<uint8_t>() == 0x01) {
                                        emit percent(static_cast<int>((float(i + 1) / N) * 100.0));

                                        if (i + 1 == N) {
                                            whisper_->get(
                                                0x09, { 0xff },
                                                [](whisper::payload& res) {
                                                    if (res.read<uint8_t>() == 0x01) {
                                                        logi("OK");
                                                    }
                                                },
                                                [](auto&& err) { loge("{}", err); });
                                        }
                                    }
                                },
                                [](auto&& err) { loge("{}", err); });
                        }
                    }
                }
            },
            [](auto&& err) { loge("{}", err); });
    });
}

void UpgradeDialog::success() { percent_->setValue(100); }

void UpgradeDialog::failed(const std::string& msg)
{
    Message::warning(this, QString::fromStdString(msg));

    loge("{}", msg);
}