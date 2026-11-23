#include "dock-widget.h"

#include "dock-titlebar.h"
#include "frameless/frameless-maker.h"

#include <QVBoxLayout>

// DockWidget 构造函数定义
DockWidget::DockWidget(const QString& title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)  // 调用基类 QDockWidget 的构造函数
{
    // 设置最小宽度
    setMinimumWidth(320);

    // 设置控件的内容边距为空
    setContentsMargins({});
    layout()->setSpacing(0);  // 设置布局间距为 0
    layout()->setContentsMargins({});  // 设置布局的边距为空

    // 创建一个自定义的标题栏
    titlebar_ = new DockTitleBar(this);
    setTitleBarWidget(titlebar_);  // 将自定义的标题栏设置为 DockWidget 的标题栏

    // 创建一个 QWidget 作为容器，用于承载窗口内容
    const auto container = new QWidget();
    setContentsMargins({});  // 再次设置内容边距为空
    setWidget(container);  // 设置容器为 DockWidget 的主要内容区域

    // 创建一个垂直布局管理器
    layout_ = new QVBoxLayout();
    layout_->setContentsMargins({});  // 设置布局边距为空
    layout_->setSpacing(0);  // 设置布局的间距为 0
    container->setLayout(layout_);  // 将布局设置给容器

    // 监听 DockWidget 是否为浮动窗口的变化
    connect(this, &DockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating) {
            // 如果变为浮动窗口，则设置为顶层窗口并带有标题、最大/最小化按钮及关闭按钮
            setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint |
                           Qt::WindowCloseButtonHint);
            // 创建并应用无边框窗口样式（通常用于自定义窗口）
            frameless_ = new FramelessMaker(this);
            show();  // 显示浮动窗口
        }
        else if (frameless_) {
            // 如果窗口不再是浮动窗口，则删除 FramelessMaker
            delete frameless_.get();
        }
    });

    // 监听活跃状态的变化，更新 `alive_` 成员变量
    connect(this, &DockWidget::aliveChanged, [this](auto&& state) {
        alive_ = state;
    });
}

// 设置 DockWidget 的生存状态（是否显示）
void DockWidget::setAlive(bool alive)
{
    if (alive)
        show();  // 如果活跃，则显示窗口
    else
        close();  // 如果不活跃，则关闭窗口
}

// 重写 showEvent 函数，窗口显示时发出信号
void DockWidget::showEvent(QShowEvent *event)
{
    emit aliveChanged(true);  // 发出窗口显示信号，表示窗口处于“活跃”状态
    QDockWidget::showEvent(event);  // 调用基类的 showEvent 函数
}

// 重写 closeEvent 函数，窗口关闭时发出信号
void DockWidget::closeEvent(QCloseEvent *event)
{
    emit aliveChanged(false);  // 发出窗口关闭信号，表示窗口处于“不活跃”状态
    QDockWidget::closeEvent(event);  // 调用基类的 closeEvent 函数
}

