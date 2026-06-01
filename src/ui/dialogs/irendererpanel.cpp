/*!
 * \file   irendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/irendererpanel.h"

namespace openswmmvis::ui {

RendererPanelRegistry &RendererPanelRegistry::instance()
{
    static RendererPanelRegistry s;
    return s;
}

void RendererPanelRegistry::registerRenderer(QString rendererId,
                                              QString displayName,
                                              RendererPanelRegistry::Factory factory)
{
    m_entries.push_back({std::move(rendererId), std::move(displayName), std::move(factory)});
}

const RendererPanelRegistry::Entry *
RendererPanelRegistry::find(const QString &rendererId) const
{
    for (const auto &e : m_entries)
        if (e.rendererId == rendererId) return &e;
    return nullptr;
}

} // namespace openswmmvis::ui
