/*!
 * \file   spatialpage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → Spatial & CRS.
 *
 * Ported verbatim from `SimulationOptionsDialog::buildSpatialTab()` and its
 * two CRS slots. The page writes no `[OPTIONS]` key during a write pass: the
 * CRS is written the moment the user picks one, so `write()` returns 0 and
 * reports the change through crsChanged().
 */
#ifndef SPATIALPAGE_H
#define SPATIALPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QLabel;
class QToolButton;

namespace openswmmvis::ui
{

class SpatialPage : public SimOptionsPage
{
    Q_OBJECT

public:
    explicit SpatialPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;

signals:
    /*!
     * \brief Emitted when the CRS pick wrote to the engine.
     *
     * The dialog latches m_wroteChanges from this, preserving the behaviour of
     * the original slot, which set the flag directly.
     */
    void crsChanged();

private slots:
    void onPickCRS();
    void onDetectCRS();

private:
    void buildUi();
    void refreshSummary();

    QLabel      *m_crsLabel        = nullptr;
    QLabel      *m_extentLabel     = nullptr;
    QToolButton *m_crsChangeButton = nullptr;
    QToolButton *m_crsDetectButton = nullptr;
};

} // namespace openswmmvis::ui

#endif // SPATIALPAGE_H
