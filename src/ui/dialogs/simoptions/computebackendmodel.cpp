/**
 * @file      computebackendmodel.cpp
 * @brief     Which compute backends this machine can run, as the engine reports them.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/simoptions/computebackendmodel.h"

#include <QComboBox>
#include <QCoreApplication>

namespace openswmmvis::ui
{

namespace
{
const char *const kDevices[] = {"OMP", "CUDA", "HIP", "SYCL"};
const int kBaseLabelRole = Qt::UserRole + 41;

QString key(int solver, const QString &backend)
{
    return QString::number(solver) + QLatin1Char(':') + backend.toUpper();
}
} // namespace

bool ComputeBackendModel::isDevice(const QString &backend)
{
    const QString token = backend.toUpper();
    for (const char *name : kDevices)
        if (token == QLatin1String(name))
            return true;
    return false;
}

void ComputeBackendModel::refresh(SWMM_Engine engine)
{
    m_entries.clear();
    for (int solver : {int(SWMM_BACKEND_SOLVER_FV_NETWORK), int(SWMM_BACKEND_SOLVER_2D_SURFACE)}) {
        for (const char *name : kDevices) {
            SWMM_BackendInfo info{};
            Entry e;
            if (swmm_backend_info(solver, name, &info) == SWMM_OK) {
                e.usable = info.usable != 0;
                e.device = QString::fromUtf8(info.device_name);
                e.reason = QString::fromUtf8(info.reason);
            } else {
                e.usable = false;
            }
            m_entries.insert(key(solver, QLatin1String(name)), e);
        }
    }

    m_fvSupported = true;
    m_fvReason.clear();
    if (engine) {
        int supported = 1;
        char why[256] = "";
        if (swmm_fv_device_supported(engine, &supported, why, int(sizeof(why))) == SWMM_OK) {
            m_fvSupported = supported != 0;
            m_fvReason = QString::fromUtf8(why);
        }
    }
}

ComputeBackendModel::Entry ComputeBackendModel::entry(int solver, const QString &backend) const
{
    return m_entries.value(key(solver, backend), Entry{});
}

void ComputeBackendModel::decorate(QComboBox *combo, int solver) const
{
    if (!combo)
        return;
    for (int i = 0; i < combo->count(); ++i) {
        QVariant base = combo->itemData(i, kBaseLabelRole);
        if (!base.isValid()) {
            base = combo->itemText(i);
            combo->setItemData(i, base, kBaseLabelRole);
        }
        const QString token = combo->itemData(i).toString();
        const Entry e = entry(solver, token);
        if (!isDevice(token) || e.usable) {
            combo->setItemText(i, base.toString());
            combo->setItemData(
                i,
                e.device.isEmpty() || !isDevice(token)
                    ? QVariant()
                    : QVariant(QCoreApplication::translate("ComputeBackendModel", "Runs on: %1")
                                   .arg(e.device)),
                Qt::ToolTipRole);
        } else {
            combo->setItemText(
                i, QCoreApplication::translate("ComputeBackendModel", "%1 (not installed)")
                       .arg(base.toString()));
            combo->setItemData(i, e.reason, Qt::ToolTipRole);
        }
    }
}

} // namespace openswmmvis::ui
