/*!
 * \file   meshregiondefaultswidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase GG0d — the mesh generation dialog's region-defaults table
 * (`workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` §3.3).
 *
 * One row per region tag the generated mesh will carry, plus a `*` row that
 * always exists, is always first and cannot be removed. Columns:
 *
 *     Region | Manning's n | Initial depth | Method | P1 … P5 | Destination
 *
 * Extracted into its own class rather than added to
 * `MeshGenerationDialog::buildUi()` — that function is already ~734 lines in a
 * 4142-line file (plan §7).
 *
 * Two conventions the dialog depends on:
 *
 *  - **The `*` row's hydraulics are a read-only mirror.** `Roughness
 *    (Manning's n)` and `Initial depth` stay the dialog's two spin boxes;
 *    setStarHydraulics() pushes their values in one way only. The dialog's
 *    collectInputs() still reads those spin boxes, so a user who never opens
 *    this table produces byte-identical output.
 *  - **Blank means inherit.** A region row's Manning's n / initial depth cell
 *    is blank until the user types into it; the table renders the inherited
 *    `*` value in italics and rows() reports NaN, so the dialog stamps only
 *    the rows that were actually given a value.
 *
 * Infiltration follows the engine's D-I3 inheritance model instead: rows go to
 * `MeshResult::infilDefaults` verbatim and are never flattened to per-cell
 * rows. A row left at "None" contributes nothing — region rows then inherit
 * the `*` row, and an all-"None" table emits no `[2D_INFILTRATION*]` section
 * at all.
 */
#ifndef MESHREGIONDEFAULTSWIDGET_H
#define MESHREGIONDEFAULTSWIDGET_H

#include "mesh/meshinfil.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QtNumeric>

class QTableWidget;
class QTableWidgetItem;

class MeshRegionDefaultsWidget : public QWidget
{
    Q_OBJECT

public:
    /*! \brief One table row, as collectInputs() reads it back.
     *
     *  \c manningsN / \c initDepth are NaN on a region row that is still
     *  inheriting the `*` row. They are never NaN on the `*` row itself,
     *  which mirrors the dialog's two spin boxes. */
    struct RegionRow
    {
        QString        tag;
        double         manningsN = qQNaN();
        double         initDepth = qQNaN();
        mesh::InfilRow infil;
    };

    explicit MeshRegionDefaultsWidget(QWidget *parent = nullptr);

    /*! Unit label shown in the "Initial depth" column header ("m", "ft"). */
    void setDepthUnit(const QString &unit);

    /*! Mirror the dialog's roughness / initial-depth fields onto the `*` row
     *  and re-render every region row that is still inheriting them. */
    void setStarHydraulics(double manningsN, double initDepth);

    /*! Replace the region rows with \p tags, keeping whatever the user already
     *  entered for a tag that survives. The `*` row is never touched. */
    void setRegionTags(const QStringList &tags);

    /*! The `*` row first, then the region rows in table order. */
    [[nodiscard]] QVector<RegionRow> rows() const;

    /*! The rows that name a method, as `[2D_INFILTRATION_DEFAULTS]` rows.
     *  Empty when no row names one. */
    [[nodiscard]] QVector<mesh::InfilDefaultRow> infilDefaults() const;

    /*! False, with \p errOut set to a message naming the region, when a row
     *  names a method but leaves one of that method's parameters blank. */
    [[nodiscard]] bool validate(QString *errOut) const;

private slots:
    void onItemChanged(QTableWidgetItem *item);
    void onCurrentCellChanged(int row, int column, int prevRow, int prevColumn);

private:
    void appendRow(const RegionRow &values);
    /*! Re-applies the row's method mask: parameter cells the method does not
     *  use render "—" and drop Qt::ItemIsEditable. */
    void applyMethodMask(int row);
    void renderHydraulicCell(int row, int column);
    void renderParamCell(int row, int slot);
    /*! Retitles the P1…P5 headers from \p row's method (mesh::infilParamLabel). */
    void refreshParamHeaders(int row);

    [[nodiscard]] mesh::InfilMethod methodAt(int row) const;
    [[nodiscard]] RegionRow rowAt(int row) const;
    [[nodiscard]] QString rowLabel(int row) const;

    QTableWidget *m_table         = nullptr;
    double        m_starMannings  = 0.035;
    double        m_starDepth     = 0.0;
    /*! Re-entrancy guard: every programmatic write to the table goes through
     *  QTableWidgetItem::setData, which re-emits itemChanged. */
    bool          m_updating      = false;
};

#endif // MESHREGIONDEFAULTSWIDGET_H
