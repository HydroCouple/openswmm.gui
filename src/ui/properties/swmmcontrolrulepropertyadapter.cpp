#include "ui/panels/dataobjectattributetablemodel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
/**
 * @file      swmmcontrolrulepropertyadapter.cpp
 * @brief     Property-tree adapter for [CONTROLS] rule blocks.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmmcontrolrulepropertyadapter.h"

#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_controls.h>

#include <vector>
#include <string>

QString SWMMControlRulePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))
        return tr("Name");
    if (property == QLatin1String("ruleText"))
        return tr("Rule Text");
    return {};
}

int SWMMControlRulePropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty())
        return -1;
    const int n = swmm_control_count(m_engine);
    const QByteArray want = m_name.toUtf8();
    for (int i = 0; i < n; ++i) {
        char buf[256] = {};
        if (swmm_control_get_id(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
            continue;
        if (want == buf)
            return i;
    }
    return -1;
}

QString SWMMControlRulePropertyAdapter::ruleText() const
{
    const int i = idx();
    if (i < 0)
        return {};
    char buf[8192] = {};
    if (swmm_control_get_rule(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

void SWMMControlRulePropertyAdapter::setRuleText(const QString &text)
{
    if(!m_engine)return;
    if(m_layer){
        if(DataObjectAttributeTableModel::applyValues(m_layer,SWMMModelLayer::DataControls,{m_name},"body",text,m_layer->editCanvas()?m_layer->editCanvas()->undoStack():nullptr))emit changed();
        return; // A refused checked edit must never fall through to an unchecked rewrite.
    }
    const int target=idx();if(target<0)return;
    QVector<QByteArray> blocks;QVector<const char *> ptrs;
    for(int j=0;j<swmm_control_count(m_engine);++j){QByteArray block(8192,0);for(;;){if(swmm_control_get_rule(m_engine,j,block.data(),block.size())!=SWMM_OK)return;if(qstrlen(block.constData())<block.size()-1)break;block.resize(block.size()*2);}blocks<<QByteArray(block.constData());}
    blocks[target]=text.toUtf8();for(const auto &b:blocks)ptrs<<b.constData();
    if(swmm_control_replace_program(m_engine,ptrs.constData(),ptrs.size(),1,nullptr,0)==SWMM_OK)emit changed();
}
