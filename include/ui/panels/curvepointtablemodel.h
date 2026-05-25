/*!
 * \file   curvepointtablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.1 — QAbstractTableModel over one CurveProvider.
 *
 * Schema: 2 columns (X / Y) by N rows. Horizontal header text is type-driven
 * via `CurveProvider::xLabel(type)` / `yLabel(type)` so the grid reads
 * "Depth | Surface Area" or "Head | Flow" or "Volume | Flow" etc.
 *
 * `setData` routes through `CurveProvider::setYAt` (Y-only edit, X locked
 * to preserve monotonicity) or `setPointAt` (X edit — validated against
 * neighbours). The provider's `mutationRejected` signal surfaces UI errors.
 *
 * Subscribes to the provider's `pointsChanged` / `pointsInserted` /
 * `pointsRemoved` / `typeChanged` signals so external mutations refresh
 * the view automatically.
 */
#ifndef OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::curve { class CurveProvider; }

namespace openswmmvis::ui {

class CurvePointTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit CurvePointTableModel(QObject *parent = nullptr);
    ~CurvePointTableModel() override;

    void setProvider(openswmmvis::curve::CurveProvider *p);
    openswmmvis::curve::CurveProvider *provider() const noexcept;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onPointsChanged_(int first, int count);
    void onPointsInserted_(int at, int count);
    void onPointsRemoved_(int at, int count);
    void onTypeChanged_();

private:
    QPointer<openswmmvis::curve::CurveProvider> m_provider;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H
