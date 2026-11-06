// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      traceanalysisdialog.h
 * @brief     User interface for flow balance and travel-time analyses.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "output/tracedata.h"
#include <QDialog>
#include <QPointer>
class SWMMVisProjectWindow;
class SWMMResultsLayer;
class QLineEdit;
class QLabel;
class QCheckBox;
class QProgressDialog;
namespace openswmmvis::trace
{
class TraceController;
class TraceSublayer;
// A small node picker used only when a tool has no selected node. Computation
// and saved data belong to TraceController; appearance belongs to the sublayer.
class TraceAnalysisDialog final : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] upstream True computes direction 1 (upstream), false direction 0.
     * @param[in] travel True attaches travel-time sublayer; false flow-balance view.
     * @param[in] seed Optional single exact node ID; empty uses currently selected model nodes.
     * @param[in] output Optional borrowed output layer; null tries active output then asks for a
     *   file.
     * @param[in] p Optional borrowed live owning project; null or absent model returns null.
     * @brief For the first project argument (nullable), reuse/create its direct-child trace picker,
     *   capture run topology once, then start for seeds or show map/node picker.
     * @return Borrowed project-owned reusable dialog, null only for absent project/model. Busy
     *   controller raises progress and ignores new request arguments; canceled file chooser or
     *   unavailable run can return an existing hidden dialog.
     * @note File selection opens/adds a new results layer and makes it active; failures warn. Run
     *   must be analyzable. Project, model, canvas and registry must remain live through callbacks;
     *   no ownership transfer for explicit output. GUI thread only.
     */
    static TraceAnalysisDialog *showFor(SWMMVisProjectWindow *, bool upstream, bool travel,
                                        const QString &seed = {},
                                        SWMMResultsLayer *output = nullptr);
    /**
     * @param[in] parent Optional Qt owner for newly allocated nonmodal WA_DeleteOnClose style
     *   window.
     * @param[in] part Required borrowed live sublayer with accessible layer/style.
     * @brief First argument is required live TraceSublayer with live layer/style; open immediate
     *   style editing with original JSON snapshot restored on rejection.
     * @note No undo command. Link/node fields include ratio, flow, gross, time, local, coverage and
     *   node volume; flow m3/s, time minutes in label presentation. Contribution editor is 0..100
     *   percent converted to fraction, three decimals. Size bounds 0..1e15/eight decimals,
     *   diameter/width max 80 px; label font 5..40 pt, precision 0..8, halo 0.1..6 px.
     * @note Invalid color/size drafts block OK; valid edits emit style changed immediately. Node
     *   marker area follows scale while editor uses diameter. Layer/sublayer destruction rejects
     *   with signals blocked, deliberately skips rollback. Other guarded callbacks tolerate
     *   destroyed style/layer. Multiple windows can hold competing snapshots; closing without
     *   rejection signal is not independently rolled back.
     */
    static void showProperties(TraceSublayer *, QWidget *parent);
    /**
     * @param[in] parent Optional QWidget owner for nonmodal WA_DeleteOnClose result window.
     * @param[in] part Required borrowed sublayer with accessible layer/result on entry.
     * @brief First argument is required live TraceSublayer/layer/result; snapshot currently
     *   contributing rows into sortable read-only table and retain shared result for
     *   export/retry-save.
     * @note Fraction remains ratio, mean flow m3/s, time seconds converted to minutes, coverage
     *   fraction to percent; nonfinite values display Unavailable. Boundary/residual volumes m3.
     *   Table does not follow later style/result changes. CSV quotes/escapes all cells and
     *   atomically commits displayed sorted rows; IO failure warns. Retry saves analysis package
     *   and sets shared saved flag only on success.
     */
    static void showDetails(TraceSublayer *, QWidget *parent);
    /*! Discard the run's saved analysis and recompute its estimates from the
     *  current output, with the usual progress dialog.
     * @param[in] runId Nonempty run identity looked up in project registry; empty no-op.
     * @param[in] p Optional borrowed owning project; null is a no-op.
     * @brief First argument is optional project (null no-op); reuse/create picker and request
     *   full-run saved-analysis recomputation with progress.
     * @note Busy controller only raises existing progress. No canAnalyze preflight here: failures
     *   delegated to controller. Captures current model snapshot only when stored run snapshot
     *   absent. Existing open layers refresh rather than duplicating attached results. No
     *   synchronous result or rollback.
 */
    static void recalculate(SWMMVisProjectWindow *, const QString &runId);
    /**
     * @param[in] p Required owning project retained raw; must outlive dialog callbacks.
     * @brief Required first argument is live owning project with canvas/registry; create reusable
     *   node picker and bind project TraceController, installing application-wide map event filter.
     * @note Dialog owns editor/completers; project owns progress dialog and controller, so
     *   destroying picker alone need not destroy progress. Controller progress/results handled only
     *   during pending session; matching run results attach unless recalculating an already open
     *   result. Project must outlive all callbacks. Direct construction requires later run/snapshot
     *   setup before starting.
     */
    explicit TraceAnalysisDialog(SWMMVisProjectWindow *);
    /**
     * @param[in] nodes Borrowed IDs copied into editor as comma-separated text.
     * @brief Copy first argument (borrowed node-ID list) into comma-space joined editor, without
     *   trimming, validation, deduplication or starting a job. IDs containing commas cannot
     *   round-trip as one seed.
     */
    void setSeedNodes(const QStringList &);
    /**
     * @return Owning JSON with current runId, raw node editor text, direction 1 upstream/0
     *   downstream and tab 1 travel/0 balance. Does not include snapshot, recalculation checkbox,
     *   progress or results.
     */
    QJsonObject state() const;
    /**
     * @param[in] j Borrowed saved state; omitted/incorrect typed fields use Qt defaults.
     * @brief Read first argument JSON into run ID, raw node text, direction/tab booleans (only
     *   integer 1 true); missing keys default empty/false.
     * @note No schema/run/node validation, snapshot/completer/prompt refresh, checkbox reset or
     *   computation; caller must establish matching context before starting.
     */
    void restore(const QJsonObject &);

protected:
    /**
     * @param[in] object Borrowed Qt target object used to check canvas ancestry.
     * @param[in] event Required borrowed Qt event, inspected synchronously only.
     * @brief First argument is borrowed event target; second is required borrowed Qt event. While
     *   visible, consume left-button presses on canvas/descendants and choose nearest snapshot node
     *   strictly within 16 logical pixels.
     * @return Base result while hidden; false for nonmouse/noncanvas/nonleft events; true for
     *   canvas left click even no node or failed required CRS transform.
     * @note Lazily load/capture snapshot if empty; transform node coordinates to current canvas CRS
     *   when available, skipping failed points. Missing CRS can use untransformed coordinates.
     *   Nearest tie keeps earlier node. Match replaces all seeds and starts job; no live geometry
     *   mutation.
     */
    bool eventFilter(QObject *, QEvent *) override;
    /**
     * @brief Unset project canvas cursor and reject picker. Does not cancel controller or clear
     *   pending work; progress Cancel is separately connected to controller cancel.
     */
    void reject() override;

private:
    /**
     * @brief Split nonempty trimmed comma-separated seeds, hide picker/unset map cursor and save
     *   state property, then request run or one-shot recalculation with captured snapshot.
     * @note Empty list silently stays open. Does not deduplicate/validate IDs or recheck controller
     *   busy/analyzability itself. Snapshot is reused unless both local nodes and saved snapshot
     *   are empty. Shows project-owned progress, resets recalculate checkbox only for
     *   recalculation; controller completion/error closes progress, failures warn. Computation is
     *   asynchronous, no dialog acceptance needed.
     */
    void start();
    SWMMVisProjectWindow *m_project;
    TraceController *m_controller;
    QLineEdit *m_nodes;
    QCheckBox *m_recalculate = nullptr;
    QLabel *m_prompt;
    QProgressDialog *m_progress;
    QString m_runId;
    Snapshot m_snapshot; // captured once per picker session
    bool m_upstream = false, m_travel = false, m_pending = false;
    bool m_recalculating = false; ///< Open layers refresh in place; only new seeds attach.
};
} // namespace openswmmvis::trace
