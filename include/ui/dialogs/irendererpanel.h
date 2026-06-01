/*!
 * \file   irendererpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer-class panel interface + factory registry.
 *
 *         Slice X.2.  Mirrors Qt's IStyleEditorWidget seam but at the
 *         renderer-class layer.  The Symbology tab's top dropdown lists
 *         all available renderer classes (Single / Graduated / Categorized
 *         / Rule-based + any future plugins).  Switching the dropdown
 *         mounts a registered panel matching the picked class.
 *
 *         Each renderer panel:
 *           - reads the current per-kind renderer from its host layer,
 *           - writes through to setKindRenderer / setRenderer on each edit,
 *           - listens for the layer's repaintRequested / rendererChanged
 *             signals to refresh on external mutations.
 *
 *         Plugins register a factory via REGISTER_RENDERER_PANEL.  The
 *         dropdown picks up the new class automatically — no edits to the
 *         dialog code.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H

#include "layers/swmm_category.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render {
class IFeatureRenderer;
class Rule;
}

namespace openswmmvis::ui {

/*!
 * \brief Context passed to renderer-panel factories at construction time.
 *
 *        Three possible target shapes, in priority order:
 *
 *          1. \c rule != nullptr — Slice Z.3b. The panel edits the
 *             Rule's owned renderer via `rule->renderer()` /
 *             `rule->setRenderer(...)`. \c hostLayer / \c category
 *             are ignored when \c rule is set. The Rule outlives the
 *             panel.
 *          2. \c hostLayer + \c category — the panel edits one of
 *             SWMMModelLayer / SWMMResultsLayer's per-kind renderer
 *             slots (the legacy multi-kind path).
 *          3. \c hostLayer only — the panel edits the layer's single
 *             layer-level renderer (the vector / raster case).
 *
 *        Panel implementations check \c ctx.rule first; if null, they
 *        fall back to the layer + category path. Slice Z.3b only
 *        rewires the outer SymbologyTab's renderer-class picker (read
 *        + class-swap). The individual IRendererPanel implementations
 *        are migrated to consult \c ctx.rule in the named Z.3c
 *        follow-up.
 */
struct RendererPanelContext
{
    OpenSWMMVisLayer                                *hostLayer = nullptr;
    std::optional<OpenSWMMVis::SwmmCategory>        category;     // empty => layer-level
    OpenSWMM::Render::Rule                          *rule = nullptr;  // Slice Z.3b
};

/*! Base interface every renderer panel inherits.  Same QWidget contract
 *  as IStyleEditorWidget so the SymbologyTab can swap panels in/out of a
 *  QStackedWidget without per-class casts. */
class IRendererPanel : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    ~IRendererPanel() override = default;

    /*! Re-read the current renderer state from the host layer / kind and
     *  refresh widget values.  Called on construction and again whenever
     *  the host layer fires rendererChanged. */
    virtual void refreshFromModel() = 0;

    /*! User-facing label for the dropdown entry (e.g. "Single Symbol"). */
    virtual QString displayName() const = 0;
};

/*! Picks the matching panel for a (rendererId, context) pair. */
class RendererPanelRegistry
{
public:
    /*! Factory: (context) → IRendererPanel.  The factory installs the
     *  matching renderer-class on the host before returning so the
     *  current panel always reflects the live renderer state. */
    using Factory = std::function<IRendererPanel *(const RendererPanelContext &ctx,
                                                    QWidget *parent)>;

    struct Entry {
        QString rendererId;     // "single" / "graduated" / "categorized" / "rulebased" / ...
        QString displayName;    // shown in the dropdown
        Factory factory;
    };

    static RendererPanelRegistry &instance();

    void registerRenderer(QString rendererId, QString displayName, Factory factory);

    /*! All registered renderer IDs in registration order — the
     *  SymbologyTab walks this to populate the dropdown. */
    [[nodiscard]] const std::vector<Entry> &entries() const { return m_entries; }

    /*! Resolve a registered factory by rendererId.  Returns nullptr when
     *  the id has no factory yet — caller falls back to the dropdown's
     *  first entry. */
    [[nodiscard]] const Entry *find(const QString &rendererId) const;

private:
    RendererPanelRegistry() = default;
    std::vector<Entry> m_entries;
};

/*! RAII registration helper.  Use the macro below. */
class RendererPanelRegistrar
{
public:
    RendererPanelRegistrar(QString rendererId, QString displayName,
                            RendererPanelRegistry::Factory factory)
    {
        RendererPanelRegistry::instance().registerRenderer(
            std::move(rendererId), std::move(displayName), std::move(factory));
    }
};

#define REGISTER_RENDERER_PANEL(ID_STRING, DISPLAY_STRING, FACTORY) \
    static const openswmmvis::ui::RendererPanelRegistrar \
        s_register_renderer_##__LINE__( \
            QStringLiteral(ID_STRING), QStringLiteral(DISPLAY_STRING), FACTORY);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H
