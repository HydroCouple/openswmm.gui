/**
 * @file      lidstaterecorder.h
 * @brief     Worker-owned recording of LID node states during simulation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_LIDSTATERECORDER_H
#define OPENSWMMVIS_LIDSTATERECORDER_H
#include "io/lidstates.h"
#include "core/swmmdatetime.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::simulation
{
// Owned exclusively by the worker. No running engine handles cross into UI.
class LidStateRecorder
{
public:
    /**
     * @brief Bind an initialized worker-owned engine and begin its LID companion history.
     * @param[in] engine Borrowed initialized engine with immutable node/LID geometry, accessed exclusively by this recorder worker.
     * @param[in] outputPath Associated engine .out path; its .lidstate companion is overwritten when porous LID nodes exist.
     * @return True when no eligible nodes exist or after opening and force-sampling; false on companion write failure.
     * @details Use a fresh recorder per run because accumulated geometry/timing state is not reset here. Captures porous cells top-to-bottom in project length units; report interval falls back to 300 seconds.
     */
    bool start(SWMM_Engine engine, const QString &outputPath)
    {
        m_engine = engine;
        int units = 0;
        swmm_get_flow_units(engine, &units);
        m_geometry.si = units >= 3;
        double start = 0;
        swmm_get_start_time(engine, &start);
        m_start = core::swmmDateTimeToQDateTime(start);
        char step[64]{};
        if (swmm_options_get(engine, "REPORT_STEP", step, sizeof(step)) == SWMM_OK) {
            const auto parts = QString::fromUtf8(step).split(':');
            m_interval = parts.size() == 3 ? parts[0].toDouble() * 3600 + parts[1].toDouble() * 60 +
                                                 parts[2].toDouble()
                                           : QString::fromUtf8(step).toDouble();
        }
        if (!(m_interval > 0))
            m_interval = 300;
        for (int node = 0; node < swmm_node_count(engine); ++node) {
            int control = -1;
            double initial = 0;
            if (swmm_node_get_lid(engine, node, &control, &initial) != SWMM_OK || control < 0)
                continue;
            io::LidNodeState state;
            state.name = QString::fromUtf8(swmm_node_id(engine, node));
            SWMM_LidRichardsOptions options{};
            state.richards = swmm_lid_richards_options_get(engine, control, &options) == SWMM_OK &&
                             options.model == 1;
            double invert = 0;
            swmm_node_get_invert_elev(engine, node, &invert);
            RuntimeNode runtime{node, {}};
            for (int row = 0; row < swmm_lid_node_state_count(engine, node); ++row) {
                io::LidCellState cell;
                if (swmm_lid_node_state_get(engine, node, row, &cell.layer, &cell.bottom, &cell.top,
                                            &cell.moisture) != SWMM_OK)
                    continue;
                SWMM_LidNodeLayer physical{};
                if (swmm_lid_node_layer_get(engine, control, cell.layer - 1, &physical) !=
                        SWMM_OK ||
                    (physical.kind != SWMM_LID_NODE_MEDIA &&
                     physical.kind != SWMM_LID_NODE_AGGREGATE) ||
                    cell.top <= cell.bottom)
                    continue;
                cell.bottom += invert;
                cell.top += invert;
                cell.porosity = physical.params[1];
                state.cells.push_back(cell);
                runtime.rows.push_back(row);
            }
            if (!state.cells.isEmpty()) {
                m_geometry.nodes.push_back(state);
                m_runtime.push_back(runtime);
            }
        }
        if (m_runtime.isEmpty())
            return true;
        m_active = m_writer.open(outputPath, m_geometry);
        if (!m_active)
            return false;
        return sample(true);
    }
    /**
     * @brief Capture current accepted LID state and publish report-boundary samples.
     * @param[in] force Write now regardless of the next report threshold, also appending a previous accepted state when present.
     * @return True for inactive recording or successful appends; false disables further recording after a writer failure.
     * @details Keeps one previous frame so a routing step crossing a report instant preserves both sides for causal playback. Failed moisture reads become NaN; classical pressure/head remain unavailable. Engine stays worker-owned.
     */
    bool sample(bool force = false)
    {
        if (!m_active)
            return true;
        double time = 0;
        swmm_get_current_time(m_engine, &time);
        auto frame = m_geometry;
        frame.time = m_start.addMSecs(qRound64(time * 1000));
        for (int i = 0; i < m_runtime.size(); ++i) {
            for (int j = 0; j < m_runtime[i].rows.size(); ++j) {
                auto &cell = frame.nodes[i].cells[j];
                int layer;
                double bottom, top, moisture;
                if (swmm_lid_node_state_get(m_engine, m_runtime[i].node, m_runtime[i].rows[j],
                                            &layer, &bottom, &top, &moisture) == SWMM_OK)
                    cell.moisture = moisture;
                else
                    cell.moisture = std::numeric_limits<double>::quiet_NaN();
                if (frame.nodes[i].richards) {
                    double water;
                    swmm_lid_richards_state_get(m_engine, m_runtime[i].node, m_runtime[i].rows[j],
                                                &cell.pressure, &cell.head, &water);
                }
            }
        }
        // Keep one bounded in-memory state. When routing steps straddle a
        // report instant, save both sides so causal playback uses the last
        // completed step before that instant, not the previous report period.
        bool ok = true;
        if (force || time >= m_next) {
            if (m_previous.time.isValid())
                ok = m_writer.append(m_previous);
            if (ok)
                ok = m_writer.append(frame);
            m_next = (std::floor(time / m_interval) + 1) * m_interval;
        }
        m_previous = std::move(frame);
        if (!ok)
            m_active = false;
        return ok;
    }
    /**
     * @brief Finalize the companion output binding after the engine closes its .out.
     * @return True when inactive, otherwise the writer finalization result; prior errors remain available from errorString().
     */
    bool finish() { return !m_active || m_writer.finish(); }
    /**
     * @brief Read the companion writer diagnostic.
     * @return Copied latest writer error text.
     */
    QString errorString() const { return m_writer.errorString(); }

private:
    struct RuntimeNode {
        int node;
        QVector<int> rows;
    };
    SWMM_Engine m_engine = nullptr;
    bool m_active = false;
    double m_interval = 300, m_next = 0;
    QDateTime m_start;
    io::LidStateFrame m_geometry, m_previous;
    QVector<RuntimeNode> m_runtime;
    io::LidStateWriter m_writer;
};
} // namespace openswmmvis::simulation
#endif
