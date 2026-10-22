/**
 * @file      sublayerselectiondialog.h
 * @brief     Modal multi-select picker for the layers inside a multi-layer vector datasource
 *            (GeoPackage, Esri File GDB, multi-layer GML/KML, …).
 * @author    SWMMVis
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Modal multi-select picker for the layers inside a multi-layer vector
 * datasource (GeoPackage, Esri File GDB, multi-layer GML/KML, …). Presents one
 * checkable row per OGR sublayer (name, geometry, feature count, CRS); the
 * caller creates one GISVectorLayer per checked entry.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SUBLAYERSELECTIONDIALOG_H
#define SUBLAYERSELECTIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>

#include "layers/gisvectorlayer.h" // GISVectorLayer::OgrSublayerInfo

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
     * @brief Copy supplied descriptors and build a sortable, initially all-checked table; source
     *   path is displayed only, no GDAL open or layer creation.
     * @note GUI-thread widget constructor, parent ownership follows QDialog. Empty descriptor list
     *   is permitted and OK remains available. Source basename is inserted unescaped into rich-text
     *   header. Nonmodal by default unless caller execs/sets modal.
     */
    explicit SublayerSelectionDialog(const QString &sourcePath,
                                     const QList<GISVectorLayer::OgrSublayerInfo> &sublayers,
                                     QWidget *parent = nullptr);

    /*! \brief OGR names of the checked sublayers, in listed order.
     * @return Copied names of exactly Checked rows in CURRENT visual/sort order, including hidden
     *   rows and duplicate names. Row UserRole resolves original descriptor even after sorting.
     * @note Does not check accepted/rejected result or create layers; caller checks exec result.
     *   Filter hides rows without unchecking them, and returned names retain source spelling.
 */
    [[nodiscard]] QStringList selectedLayerNames() const;

    /*! \brief The checked sublayer descriptors, in listed order.
     * @return Value copies of exactly Checked descriptors in current visual/sort order, including
     *   hidden rows, duplicates and unsupported/unknown metadata. Original index stored in name
     *   item preserves association after sorting.
     * @note No acceptance check, datasource validation or ownership transfer of GDAL objects;
     *   caller opens/creates selected layers.
 */
    [[nodiscard]] QList<GISVectorLayer::OgrSublayerInfo> selectedSublayers() const;

private:
    /**
     * @param[in] sourcePath Raw source path used only for basename in header; no existence/open
     *   check or retained data-source handle.
     * @brief Create owned filter, sortable four-column table and visible-row check
     *   actions/OK/Cancel.
     * @note Every descriptor starts checked; negative feature count displays question mark, empty
     *   CRS unknown. Source values copied, no async enumeration, model writes or validation before
     *   accepting empty selection.
     */
    void buildUi(const QString &sourcePath);
    /**
     * @param[in] checked True checks all currently visible rows, false unchecks them.
     * @brief Change only visible row name-item check states, retaining hidden selections.
     * @note No signal blocking, datasource/model changes or dialog acceptance. Sorting item
     *   identities remain independent of descriptor index.
     */
    void setAllChecked(bool checked);
    /**
     * @brief Toggle visible Checked rows to Unchecked and every other visible state to Checked;
     *   hidden rows unchanged. No datasource writes or acceptance.
     */
    void invertChecked();
    /**
     * @param[in] text Literal untrimmed case-insensitive substring of displayed sublayer name;
     *   empty shows all.
     * @brief Set each row hidden according to name match without altering checks or ordering.
     * @note Geometry/count/CRS ignored; selected getters still include checked hidden rows. No
     *   debounce/source query or dialog result change.
     */
    void applyNameFilter(const QString &text);

    QList<GISVectorLayer::OgrSublayerInfo> m_sublayers;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_filter = nullptr;
};

#endif // SUBLAYERSELECTIONDIALOG_H
