/*!
 * \file   transectstationtablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — QAbstractTableModel over one TransectProvider.
 *
 * Schema: 2 columns (Station / Elevation) by N rows. Edits route through
 * TransectProvider::setElevationAt (col 1) or setPointAt (col 0). The
 * provider's mutationRejected signal surfaces UI errors. Subscribes to
 * the provider's pointsChanged / pointsInserted / pointsRemoved.
 */
#ifndef OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H
#define OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::transect { class TransectProvider; }

namespace openswmmvis::ui {

class TransectStationTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TransectStationTableModel(QObject *parent = nullptr);
    ~TransectStationTableModel() override;

    void setProvider(openswmmvis::transect::TransectProvider *p);
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    /*! \brief Override the header units (e.g. "(ft)" / "(m)"). */
    void setUnitsSuffix(const QString &suffix);
    QString unitsSuffix() const noexcept { return m_unitsSuffix; }

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

private:
    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    QString m_unitsSuffix;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H
