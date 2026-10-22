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
    static TraceAnalysisDialog *showFor(SWMMVisProjectWindow *, bool upstream, bool travel,
                                        const QString &seed = {},
                                        SWMMResultsLayer *output = nullptr);
    static void showProperties(TraceSublayer *, QWidget *parent);
    static void showDetails(TraceSublayer *, QWidget *parent);
    /*! Discard the run's saved analysis and recompute its estimates from the
     *  current output, with the usual progress dialog. */
    static void recalculate(SWMMVisProjectWindow *, const QString &runId);
    explicit TraceAnalysisDialog(SWMMVisProjectWindow *);
    void setSeedNodes(const QStringList &);
    QJsonObject state() const;
    void restore(const QJsonObject &);

protected:
    bool eventFilter(QObject *, QEvent *) override;
    void reject() override;

private:
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
