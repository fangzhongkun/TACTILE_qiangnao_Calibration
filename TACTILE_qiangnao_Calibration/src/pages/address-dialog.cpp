#include "address-dialog.h"

#include "frameless/frameless-maker.h"
#include "logging.h"
#include "message.h"

#include <probe/thread.h>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

// 构造函数 AddressDialog 的实现
AddressDialog::AddressDialog(Device *device, QWidget *parent) : QDialog(parent)
{
    // 创建并初始化一个自定义的标题栏 (TitleBar)，作为窗口的标题部分
    const auto titlebar = new TitleBar(this);
    // 使用 FramelessMaker 来使标题栏支持无边框窗口
    new FramelessMaker(titlebar, this);

    // 设置对话框的内容边距为空
    setContentsMargins({});
    // 设置对话框为应用程序模式（模态窗口），即阻止与其他窗口的交互
    setWindowModality(Qt::ApplicationModal);

    // 设置对话框的最小宽度为 475
    setMinimumWidth(475);

    // 创建垂直布局，用于添加标题栏和表单控件
    const auto layout = new QVBoxLayout();
    layout->setSpacing(0);  // 设置布局控件之间的间距为 0
    layout->setContentsMargins({});  // 设置布局的内容边距为空
    setLayout(layout);  // 将布局设置到当前对话框上

    // 将标题栏添加到布局中
    layout->addWidget(titlebar);

    // 设置对话框的标题
    setWindowTitle(tr("Device Address"));

    // 创建一个表单布局，用于放置 SN 和 Address 的输入控件
    auto form = new QFormLayout();
    form->setSpacing(10);  // 设置表单控件之间的间距为 10
    form->setContentsMargins({ 15, 15, 15, 10 });  // 设置表单布局的内容边距
    layout->addLayout(form);  // 将表单布局添加到主布局中

    // 创建 SN 标签，使用 QLineEdit 控件显示设备的序列号
    sn_label_ = new QLineEdit();
    sn_label_->setReadOnly(true);  // 设置为只读
    sn_label_->setDisabled(true);  // 禁用控件，防止编辑
    sn_label_->setContextMenuPolicy(Qt::NoContextMenu);  // 禁用右键菜单

    // 创建 Address 输入框，使用 QSpinBox 控件，设置范围为 0 到 255，并以 16 进制显示
    address_box_ = new QSpinBox();
    address_box_->setRange(0, 255);  // 设置地址范围
    address_box_->setDisplayIntegerBase(16);  // 使用十六进制显示
    address_box_->setPrefix("0x");  // 设置前缀为 "0x"

    // 启动一个新线程，模拟从设备读取序列号和地址
    thread_ = std::jthread([=, this]() {
        // 设置线程名称，便于调试
        probe::thread::set_name("DEVICE-ADDRESS");

        // 从设备获取序列号和地址
        sn_      = device->serial_number().c_str();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        address_ = device->address();

        // 触发 `changed` 信号，通知 UI 更新
        emit changed();
    });

    // 连接 `changed` 信号，当线程更新数据后，UI 更新序列号和地址
    connect(
        this, &AddressDialog::changed, this,
        [this]() {
            sn_label_->setPlaceholderText(sn_);  // 设置 SN 字段的占位符为序列号
            address_box_->setValue(address_);  // 设置地址输入框的值为设备地址
        },
        Qt::QueuedConnection);  // 使用队列连接，确保跨线程安全

    // 将序列号和地址输入框添加到表单布局中
    form->addRow(tr("SN"), sn_label_);
    form->addRow(tr("Address"), address_box_);

    // 添加一个伸缩空白区，使得布局元素适当分布
    layout->addStretch();

    // 创建一个对话框按钮框，包含 "Apply" 和 "Cancel" 按钮
    const auto box = new QDialogButtonBox(this);
    box->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);  // 添加应用按钮
    box->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);  // 添加取消按钮
    box->layout()->setSpacing(10);  // 设置按钮之间的间距
    box->setContentsMargins({ 15, 10, 15, 10 });  // 设置按钮框的内容边距
    layout->addWidget(box);  // 将按钮框添加到布局中

    // 连接 "Apply" 按钮的接受信号，保存设备地址
    connect(box, &QDialogButtonBox::accepted,
            [=, this] { device->address(static_cast<uint8_t>(address_box_->value())); });
    // 连接 "Cancel" 按钮的拒绝信号，关闭对话框
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
