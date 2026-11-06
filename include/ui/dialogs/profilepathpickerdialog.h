/**
 * @file      profilepathpickerdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BC's path-picker — modal dialog shown by
 *         OpenSWMMVisMapToolSelectProfile when ProfileRouter returns more
 *         than one candidate between the user-picked endpoints.
 *
 *         Lists each candidate with length / conduit count / non-conduit
 *         count / total invert drop.  Focusing or selecting a row emits
 *         `hoveredPathChanged(index)` so the host can promote that path on
 *         the map overlay; clicking OK confirms.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILEPATHPICKERDIALOG_H
#define PROFILEPATHPICKERDIALOG_H

#include "plot/profilerouter.h"

#include <QDialog>
#include <QVector>

class QTableWidget;
class QDialogButtonBox;
class SWMMModelLayer;

class ProfilePathPickerDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] model Optional borrowed RAW model; must outlive summary population/repopulation,
     *   engine indices in supplied paths must still match.
     * @param[in] paths Copied ordered candidates, each linkIds/nodes interpreted as engine indices
     *   for summary; arbitrary weight displayed as supplied.
     * @param[in] truncated True adds advisory that search omitted possible paths; does not alter
     *   list or rerun search.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build nonmodal sortable path summary table, select first row when available and leave
     *   accepted index -1 until OK.
     * @note Host chooses modality/window flags and handles map overlay/zoom signals. Current-row
     *   change emits highlight; mouse tracking alone has no cell-enter/hover handler. Double-click
     *   requests zoom without accepting. OK resolves original path index from sorted row, fallback
     *   first candidate when no current; empty list still accepts with -1. No model, router or
     *   source selection mutation.
     */
    ProfilePathPickerDialog(SWMMModelLayer *model, const QVector<ProfileRouter::Path> &paths,
                            bool truncated = false, QWidget *parent = nullptr);

    /*!
     * \brief Index (0-based) of the path the user accepted, or -1 when
     *        the dialog was cancelled.
     * @return Last index stored by OK, initially -1; Cancel BUTTON explicitly sets -1. Plain
     *   QDialog rejection/window close after reuse does not independently reset prior accepted
     *   value, so check result. No live focused-row query, ownership transfer or candidate
     *   validation.
     */
    [[nodiscard]] int selectedPathIndex() const;

signals:
    /*!
     * \brief Emitted whenever the focused/selected row changes — the host
     *        listens to update the map overlay's highlighted path.
     *        Index `-1` means no row is focused.
     * @param[in] index Original copied candidate index from current row payload, -1 if no current
     *   item; zero-based regardless of visual sorting.
     * @note Emitted for current-row changes, not passive mouse hover alone. Host updates overlay;
     *   does not select model nodes or accept. Initial row selection may emit during construction
     *   before host connects.
     */
    void hoveredPathChanged(int index);

    /*!
     * \brief Emitted on row double-click — the host listens to zoom the
     *        main map to that candidate path's extent.
     * @param[in] index Original zero-based candidate index associated with double-clicked row.
     * @note Host computes/zooms extent; signal itself does not accept dialog, change current model
     *   selection or retain path/model ownership.
     */
    void zoomToPathRequested(int index);

private slots:
    /**
     * @param[in] currentRow Current visual table row, may be invalid/-1.
     * @param[in] previousRow Ignored prior visual row.
     * @brief Read stable original index from first-column payload and emit hoveredPathChanged, or
     *   -1 without item. Does not set accepted index, reroute or change engine.
     */
    void onCurrentRowChanged(int currentRow, int previousRow);

private:
    /**
     * @brief Replace/populate one summary row per copied candidate with palette swatch and stable
     *   original-index payload.
     * @note Length is path weight formatted one decimal, counts integer, endpoint invert drop two
     *   decimals; no unit conversion or units in headings. Construction runs before sorting
     *   enabled; later callers must coordinate active sorting. No model writes or source/path
     *   refresh subscription.
     */
    void populateTable();
    /**
     * @param[in] pathIdx Original candidate index; invalid leaves all outputs initialized zero.
     * @param[out] lengthOut Stored path.weight verbatim, router cost rather than independently
     *   measured polyline length; units depend on caller graph.
     * @param[out] conduitsOut Number of path link IDs whose engine type getter yields zero; failed
     *   getter retains initialized zero and counts as conduit.
     * @param[out] nonConduitsOut Number with nonzero returned engine link type; zero without
     *   engine.
     * @param[out] dropOut First-node minus last-node invert in project ft/m, zero without
     *   engine/empty nodes; getter failures retain zero endpoint fallback.
     * @brief Initialize summary outputs, copy supplied weight and query current engine types/end
     *   elevations when available.
     * @note No engine index/status validation, length recomputation, node orientation adjustment or
     *   source mutation. Borrowed raw model must remain live.
     */
    void summarizePath(int pathIdx, double &lengthOut, int &conduitsOut, int &nonConduitsOut,
                       double &dropOut) const;

    SWMMModelLayer *m_model = nullptr;
    QVector<ProfileRouter::Path> m_paths;
    QTableWidget *m_table = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    int m_selectedIdx = -1;
};

#endif // PROFILEPATHPICKERDIALOG_H
