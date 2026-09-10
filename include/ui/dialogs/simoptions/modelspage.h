/*!
 * \file   modelspage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → Models / Processes.
 *
 * Tabs: Domains & Processes · Modules · Flags.
 *
 * FLOW_ROUTING was *built* here and *wired* on Routing & Hydraulics; T4 gave
 * the combo to the page that gates on it and left a read-only mirror behind
 * (PLAN §2.1) — one key, one editor, no synchronisation burden.
 *
 * The Domain × Species transport matrix is the engine's own view of what will
 * be carried where. Its 2D column is editable and mirrors the 2D page's
 * TRANSPORT_* boxes (CLAUDE.md §5.1 — one model, two views), reached through
 * the hooks below rather than by touching that page's widgets (PLAN §4.1).
 */
#ifndef MODELSPAGE_H
#define MODELSPAGE_H

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;

namespace openswmmvis::ui
{

class ModelsPage : public SimOptionsPage
{
    Q_OBJECT

public:
    explicit ModelsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabDomains = 0, TabModules, TabFlags };

    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    // ---- 2D module toggle — the dialog's 2D row gate reads this ----------
    [[nodiscard]] bool module2DEnabled() const;
    void setModule2DEnabled(bool on);

    /*! \brief Read-only FLOW_ROUTING mirror text, pushed by the dialog from
     *         the page that owns the combo. */
    void setFlowRoutingText(const QString &routing);

    /*!
     * \brief Where the transport matrix's 2D column reads and writes.
     *
     * Wired by the dialog to the 2D page's TRANSPORT_* checkboxes. Both
     * default-construct to no-ops, so a build without the 2D module shows the
     * matrix read-only rather than crashing.
     */
    void setTransport2DHooks(std::function<bool(int)> getter,
                             std::function<void(int, bool)> setter);

public slots:
    /*! \brief Re-ask the engine for the matrix. Also called by the dialog when
     *         the 2D page reports a TRANSPORT_* change. */
    void refreshTransportMatrix();

signals:
    /*! \brief The mirror's link was clicked — show Routing & Hydraulics. */
    void showFlowRoutingPageRequested();

private:
    void buildUi();
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;

    QComboBox      *m_infiltrationCombo = nullptr;
    QLabel         *m_routingMirror     = nullptr;
    QCheckBox      *m_allowPondingBox   = nullptr;
    QCheckBox      *m_ignoreRainfallBox = nullptr;
    QCheckBox      *m_ignoreSnowmeltBox = nullptr;
    QCheckBox      *m_ignoreGroundwaterBox = nullptr;
    QCheckBox      *m_ignoreRDIIBox     = nullptr;
    QCheckBox      *m_ignoreQualityBox  = nullptr;
    QCheckBox      *m_ignoreRoutingBox  = nullptr;

    QCheckBox      *m_module1DBox       = nullptr;  ///< Always-on, disabled (1D core).
    QCheckBox      *m_module2DBox       = nullptr;  ///< Toggle 2D surface routing.

    /*! \brief True when the 2D-module checkbox reflects a real IGNORE_2D
     *         intent rather than one inferred from the .inp's contents.
     *
     *  Set when a per-project QSettings preference exists, or as soon as the
     *  user toggles the box. While it is false the checkbox is only a
     *  *description* of the model ("this .inp has no 2D sections"), and
     *  writing IGNORE_2D from it would invent a preference the user never
     *  expressed — which is what made an unedited Apply dirty every 1D
     *  project. */
    bool m_module2DIntentKnown = false;

    QTableWidget   *m_transportMatrixTable = nullptr;
    bool            m_matrixSyncing        = false;
    std::function<bool(int)>       m_transport2DGet;
    std::function<void(int, bool)> m_transport2DSet;
};

} // namespace openswmmvis::ui

#endif // MODELSPAGE_H
