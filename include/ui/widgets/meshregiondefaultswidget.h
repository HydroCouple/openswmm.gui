/**
 * @file      meshregiondefaultswidget.h
 * @brief     the mesh generation dialog's region-defaults table
 *            (`workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` §3.3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    struct RegionRow {
        QString tag;
        double manningsN = qQNaN();
        double initDepth = qQNaN();
        mesh::InfilRow infil;
    };

    /**
     * @brief Create the initial-values table with a permanent mesh-wide wildcard row.
     * @param[in] parent Optional QWidget owner; all edits are local drafts and do not write an engine.
     */
    explicit MeshRegionDefaultsWidget(QWidget *parent = nullptr);

    /*! Unit label shown in the "Initial depth" column header ("m", "ft").
     * @param[in] unit Header-only depth unit text, optionally empty. Does not convert stored depth values.
     */
    void setDepthUnit(const QString &unit);

    /*! Mirror the dialog's roughness / initial-depth fields onto the `*` row
     *  and re-render every region row that is still inheriting them.
     * @param[in] manningsN Mesh-wide roughness in the host's Manning coefficient convention, copied without validation.
     * @param[in] initDepth Mesh-wide depth in the host's displayed length units; copied without conversion/validation.
     * @note Updates wildcard and inheriting region display under a reentrancy guard.
     */
    void setStarHydraulics(double manningsN, double initDepth);

    /*! Replace the region rows with \p tags, keeping whatever the user already
     *  entered for a tag that survives. The `*` row is never touched.
     * @param[in] tags Region tags in desired table order; caller should exclude wildcard/duplicates because they are not filtered here. Existing exact-tag drafts are retained, obsolete rows removed.
     */
    void setRegionTags(const QStringList &tags);

    /*! The `*` row first, then the region rows in table order.
     * @return Value snapshot in table order; unset region hydraulics remain NaN, infiltration exposes only parameters used by its current method. No validation is performed.
     */
    [[nodiscard]] QVector<RegionRow> rows() const;

    /*! Restore values for currently available tags; ignore obsolete tags.
     * @param[in] values Saved records matched by exact tag; the first match wins. Only current tags are restored and wildcard hydraulics are then replaced by current mirrored host values.
     */
    void restoreRows(const QVector<RegionRow> &values);

    /*! The rows that name a method, as `[2D_INFILTRATION_DEFAULTS]` rows.
     *  Empty when no row names one.
     * @return Value rows with non-None infiltration methods, preserving table order and raw positional parameter units; no per-cell flattening or full physical validation occurs.
     */
    [[nodiscard]] QVector<mesh::InfilDefaultRow> infilDefaults() const;

    /*! False, with \p errOut set to a message naming the region, when a row
     *  names a method but leaves one of that method's parameters blank.
     * @param[out] errOut Optional first missing-required-parameter message; unchanged on success.
     * @return True if every used infiltration parameter is non-NaN; false on the first blank. Does not check general physical ranges, infinity, destinations or hydraulic values.
     */
    [[nodiscard]] bool validate(QString *errOut) const;

private slots:
    /**
     * @brief Reformat an edited draft item and reapply method masks/headings with recursive updates guarded.
     * @param[in] item Changed table item; null or programmatic guarded updates are ignored.
     */
    void onItemChanged(QTableWidgetItem *item);
    /**
     * @brief Retitle positional parameter headings for the newly current row.
     * @param[in] row Current zero-based row; negative uses generic parameter headings.
     * @param[in] column Current column payload, unused.
     * @param[in] prevRow Previous row payload, unused.
     * @param[in] prevColumn Previous column payload, unused.
     */
    void onCurrentCellChanged(int row, int column, int prevRow, int prevColumn);

private:
    /**
     * @brief Append a row and render its hydraulic/method/parameter/destination state under a reentrancy guard.
     * @param[in] values Draft record copied without validation; the first table row is treated as wildcard and uses mirrored host hydraulics.
     */
    void appendRow(const RegionRow &values);
    /*! Re-applies the row's method mask: parameter cells the method does not
     *  use render "—" and drop Qt::ItemIsEditable.
     * @param[in] row Existing table row whose used parameter/destination editability and display are refreshed. Stored masked values are preserved for later method switching.
     */
    void applyMethodMask(int row);
    /**
     * @brief Format one raw hydraulic value or an italic inherited wildcard display.
     * @param[in] row Existing table row; missing item is ignored.
     * @param[in] column Manning or initial-depth column. Raw inheritance state remains unset rather than flattened.
     */
    void renderHydraulicCell(int row, int column);
    /**
     * @brief Render an applicable infiltration parameter or a disabled em-dash placeholder.
     * @param[in] row Existing table row.
     * @param[in] slot Zero-based positional parameter slot 0–4. Masking does not discard the stored value.
     */
    void renderParamCell(int row, int slot);
    /*! Retitles the P1…P5 headers from \p row's method (mesh::infilParamLabel).
     * @param[in] row Current row whose method supplies positional labels; negative selects None/generic P1–P5 headings.
     */
    void refreshParamHeaders(int row);

    /**
     * @brief Read a row's raw stored infiltration-method selector.
     * @param[in] row Table row.
     * @return Stored enum or None for a missing/unset item; enum range is not independently checked.
     */
    [[nodiscard]] mesh::InfilMethod methodAt(int row) const;
    /**
     * @brief Collect one raw row, preserving unset numeric states.
     * @param[in] row Zero-based table row.
     * @return Draft value; invalid rows return defaults. Missing raw hydraulics/used parameters become NaN, missing destination becomes Lost.
     */
    [[nodiscard]] RegionRow rowAt(int row) const;
    /**
     * @brief Format a region identity for missing-parameter diagnostics.
     * @param[in] row Existing row; zero denotes wildcard.
     * @return Translated wildcard description or quoted tag.
     */
    [[nodiscard]] QString rowLabel(int row) const;

    QTableWidget *m_table = nullptr;
    double m_starMannings = 0.035;
    double m_starDepth = 0.0;
    /*! Re-entrancy guard: every programmatic write to the table goes through
     *  QTableWidgetItem::setData, which re-emits itemChanged. */
    bool m_updating = false;
};

#endif // MESHREGIONDEFAULTSWIDGET_H
