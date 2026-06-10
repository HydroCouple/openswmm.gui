/*!
 * \file   culvertcodecombobox.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/culvertcodecombobox.h"

#include "layers/swmmmodellayer.h"
#include "ui/properties/culvertcodes.h"

#include <openswmm/engine/openswmm_links.h>

CulvertCodeComboBox::CulvertCodeComboBox(QWidget *parent)
    : QComboBox(parent)
{
    addItem(culvertCodeLabel(0), 0);
    for (const CulvertCodeInfo &c : culvertCodes())
        addItem(culvertCodeLabel(c.code), c.code);

    connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CulvertCodeComboBox::onCurrentIndexChanged);
}

void CulvertCodeComboBox::setValue(const CulvertCodeRef &ref)
{
    m_ref = ref;
    m_suppressApply = true;
    const int i = findData(ref.code);
    setCurrentIndex(i >= 0 ? i : 0);
    m_suppressApply = false;
}

void CulvertCodeComboBox::onCurrentIndexChanged(int /*index*/)
{
    if (m_suppressApply) return;
    if (!m_ref.engine || m_ref.linkName.isEmpty()) return;

    const int idx = swmm_link_index(m_ref.engine,
                                    m_ref.linkName.toUtf8().constData());
    if (idx < 0) return;

    const int code = currentData().toInt();
    if (m_ref.layer)
        m_ref.layer->applyLinkCulvertCode(idx, code);   // MVC refresh path
    else
        swmm_link_set_culvert_code(m_ref.engine, idx, code);
    m_ref.code = code;
    emit valueChanged();
}
