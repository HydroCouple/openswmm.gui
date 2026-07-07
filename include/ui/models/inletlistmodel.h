/*!
 * \file   inletlistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over an InletRegistry. Mirrors LandUseListModel.
 */
#ifndef OPENSWMMVIS_UI_MODELS_INLETLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_INLETLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::inlet {
class InletProvider;
class InletRegistry;
}

namespace openswmmvis::ui {

class InletListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit InletListModel(QObject *parent = nullptr);
    ~InletListModel() override;

    void setRegistry(openswmmvis::inlet::InletRegistry *registry);
    openswmmvis::inlet::InletRegistry *registry() const noexcept;

    openswmmvis::inlet::InletProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::inlet::InletProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::inlet::InletProvider *p);
    void onProviderRenamed_(openswmmvis::inlet::InletProvider *p,
                              const QString &prev, const QString &now);
    void onProviderParamsChanged_(openswmmvis::inlet::InletProvider *p);

private:
    int indexOf_(openswmmvis::inlet::InletProvider *p) const;

    QPointer<openswmmvis::inlet::InletRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_INLETLISTMODEL_H
