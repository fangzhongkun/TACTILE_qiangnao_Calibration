#ifndef SCANNER_TABLE_WINDOW_H
#define SCANNER_TABLE_WINDOW_H

#include "../force-calibration/product_model.h"
#include "colormap.h"
#include "dock-widget.h"

#include <libtouch/matrix.h>
#include <array>
#include <shared_mutex>
class QStandardItemModel;
class QTableView;
class Menu;
class QTimer;
class QLabel;

class TableDockWidget final : public DockWidget
{
    Q_OBJECT
public:
    explicit TableDockWidget(const QString& title, QWidget *parent = nullptr,
                             Qt::WindowFlags flags = Qt::WindowFlags());

    void setData(uint32_t id, const touch::matrix<uint16_t>& m);

    // 把当前表格正在显示的 AD 值追加写入 CSV。文件不存在则创建，已存在则续写。
    bool appendDisplayedAdCsv(const QString& file_path, double pressure_kpa, QString* error);

    void setDataRange(int, int max);

    void setColormap(cmap::colormap_t cmap);

    void product_model_changed(uint8_t idx);
signals:
    void grabbed(const QPixmap&, std::wstring path);

protected:
    void initContextMenu();
    void initToolbar();

    void contextMenuEvent(QContextMenuEvent *event) override;
public slots:
    void saveImage(std::wstring path);
private slots:
    void refresh();
    void save();

    void hideHeaders(bool);
    void updateIcons();

private:
    void render(QStandardItemModel *model);
    void renderBrainCoRegions();
    void renderBrainCoRegion(QStandardItemModel *model, const touch::matrix<uint16_t>& data,
                             int global_row_offset, uint32_t part_id);

    QPixmap grabTable();

    QTableView         *view_{};
    QStandardItemModel *model_{};
    std::array<QTableView *, 3>         brainco_views_{};
    std::array<QStandardItemModel *, 3> brainco_models_{};
    std::array<QLabel *, 3>             brainco_labels_{};
    Menu               *menu_{};
    QPointer<QAction>   pause_action_{};
    QPointer<QAction>   save_action_{};
    QPointer<QAction>   save_img_action_{};
    QPointer<QAction>   hide_headers_action_{};

    std::shared_mutex       mtx_{};
    touch::matrix<uint16_t> data_{};
    uint32_t                data_id_{ 0U };

    cmap::colormap_t cmap_{ cmap::colormap_t::JET };
    float            factor_{ 4095 / 256.0f };
    bool             paused_{ false };
    QTimer          *timer_{};

    ProductModel product_type = ProductModel::Unknown;
};

#endif //! SCANNER_TABLE_WINDOW_H
