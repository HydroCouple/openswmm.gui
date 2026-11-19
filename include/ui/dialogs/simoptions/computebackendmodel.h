/**
 * @file      computebackendmodel.h
 * @brief     Which compute backends this machine can run, as the engine reports them.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef COMPUTEBACKENDMODEL_H
#define COMPUTEBACKENDMODEL_H

#include <QHash>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

class QComboBox;

namespace openswmmvis::ui
{

/**
 * @brief The engine's answers about compute backends, held once for every view that shows them.
 *
 * Two questions, both asked of the engine so the GUI carries no copy of its rules:
 * whether a backend (OpenMP, CUDA, HIP, SYCL) is installed and has a device, per solver
 * (swmm_backend_info), and whether the open model's options are ones the 1D finite volume
 * device backend integrates (swmm_fv_device_supported).
 *
 * The Simulation Options dialog owns one instance through its SimOptionsContext and refreshes
 * it before its pages read, so the 1D and the 2D backend editors and their notes agree.
 */
class ComputeBackendModel
{
public:
    /// What the engine said about one backend for one solver.
    struct Entry {
        bool usable = true;
        QString device; ///< Device the backend would run on; empty if none.
        QString reason; ///< Why it is not usable; empty when usable.
    };

    /**
     * @brief Ask the engine again.
     * @param engine Open engine for the model-scope question; null leaves that answer as "in scope".
     */
    void refresh(SWMM_Engine engine);

    /**
     * @brief The answer for one backend.
     * @param solver A SWMM_BackendSolver value.
     * @param backend Backend token as saved in the model (CPU, AUTO, OMP, CUDA, HIP, SYCL).
     * @return The engine's answer; "usable" for a token never asked about (CPU, AUTO).
     */
    [[nodiscard]] Entry entry(int solver, const QString &backend) const;

    /// True when the token names a plugin backend rather than the built-in CPU solver.
    [[nodiscard]] static bool isDevice(const QString &backend);

    /// Whether the open model's options are in the 1D device backend's scope.
    [[nodiscard]] bool fvDeviceSupported() const { return m_fvSupported; }

    /// The first option outside that scope; empty when in scope.
    [[nodiscard]] QString fvDeviceReason() const { return m_fvReason; }

    /**
     * @brief Mark a backend combo's items with what is installed.
     *
     * Each item's data is a backend token. An item whose backend cannot be used here gets
     * " (not installed)" after its label and the engine's reason as its tooltip; it stays
     * selectable, because a model made on another machine may ask for it. The label an item
     * had when first seen is kept, so calling this again never stacks suffixes.
     *
     * @param combo Combo to mark; null is ignored.
     * @param solver A SWMM_BackendSolver value.
     */
    void decorate(QComboBox *combo, int solver) const;

private:
    QHash<QString, Entry> m_entries; ///< key "<solver>:<TOKEN>"
    bool m_fvSupported = true;
    QString m_fvReason;
};

} // namespace openswmmvis::ui

#endif // COMPUTEBACKENDMODEL_H
