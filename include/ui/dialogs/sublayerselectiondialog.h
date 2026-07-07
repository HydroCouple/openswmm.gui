/*!
 * \file   sublayerselectiondialog.h
 * \author OpenSWMM GUI
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Modal multi-select picker for the layers inside a multi-layer vector
 * datasource (GeoPackage, Esri File GDB, multi-layer GML/KML, …). Presents one
 * checkable row per OGR sublayer (name, geometry, feature count, CRS); the
 * caller creates one GISVectorLayer per checked entry.
 */
#ifndef SUBLAYERSELECTIONDIALOG_H
#define SUBLAYERSELECTIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>

#include "layers/gisvectorlayer.h"   // GISVectorLayer::OgrSublayerInfo

class QLineEdit;
class QTableWidget;

/*!
 * \class SublayerSelectionDialog
 * \brief Lets the user choose which sublayers of a vector datasource to add.
 */
class SublayerSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param sourcePath  The datasource path (for the header label).
     * \param sublayers   The enumerated sublayers (see
     *                    GISVectorLayer::enumerateSublayers()).
     * \param parent      Parent widget.
     */
    explicit SublayerSelectionDialog(
        const QString &sourcePath,
        const QList<GISVectorLayer::OgrSublayerInfo> &sublayers,
        QWidget *parent = nullptr);

    /*! \brief OGR names of the checked sublayers, in listed order. */
    [[nodiscard]] QStringList selectedLayerNames() const;

    /*! \brief The checked sublayer descriptors, in listed order. */
    [[nodiscard]] QList<GISVectorLayer::OgrSublayerInfo> selectedSublayers() const;

private:
    void buildUi(const QString &sourcePath);
    void setAllChecked(bool checked);
    void invertChecked();
    void applyNameFilter(const QString &text);

    QList<GISVectorLayer::OgrSublayerInfo> m_sublayers;
    QTableWidget *m_table  = nullptr;
    QLineEdit    *m_filter = nullptr;
};

#endif // SUBLAYERSELECTIONDIALOG_H
