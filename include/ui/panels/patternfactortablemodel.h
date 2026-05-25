/*!
 * \file   patternfactortablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.2 — QAbstractTableModel over one PatternProvider.
 *
 * Schema is fixed: one column "Factor" with N rows where N = the bound
 * provider's `factorCount()` (12 monthly / 7 daily / 24 hourly / 24 weekend).
 * Vertical-header text uses `PatternProvider::rowLabel(type, i)` so the
 * grid reads "Jan / Feb / …" or "Sun / Mon / …" or "00:00 / 01:00 / …"
 * depending on the bound pattern's type.
 *
 * `setData` validates via `PatternProvider::setFactor` (no negative values,
 * in-range index); the model relies on the provider's `mutationRejected`
 * signal for surface-level error feedback. Live drag-edit (slider, future)
 * should bypass the model and call `setFactorLive` directly to avoid undo
 * churn.
 *
 * Subscribes to the provider's `factorChanged` / `factorsChanged` /
 * `typeChanged` signals so external mutations refresh the view automatically.
 */
#ifndef OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::pattern { class PatternProvider; }

namespace openswmmvis::ui {

class PatternFactorTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PatternFactorTableModel(QObject *parent = nullptr);
    ~PatternFactorTableModel() override;

    /*! \brief Bind to a provider (or nullptr to clear). Resets the model. */
    void setProvider(openswmmvis::pattern::PatternProvider *p);
    openswmmvis::pattern::PatternProvider *provider() const noexcept;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderFactorChanged_(int i);
    void onProviderFactorsChanged_();
    void onProviderTypeChanged_();

private:
    QPointer<openswmmvis::pattern::PatternProvider> m_provider;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H
