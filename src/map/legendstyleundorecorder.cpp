/**
 * @file      legendstyleundorecorder.cpp
 * @brief     Puts map legend setting changes on the map's undo stack.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "map/legendstyleundorecorder.h"

#include "render/legendoverlaystyle.h"

#include <QCoreApplication>
#include <QTimer>
#include <QUndoStack>

namespace openswmmvis::map
{

namespace
{
/** @brief Keys whose values differ between two saved settings objects. */
QSet<QString> differingKeys(const QJsonObject &a, const QJsonObject &b)
{
    QSet<QString> keys;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        if (b.value(it.key()) != it.value())
            keys.insert(it.key());
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        if (!a.contains(it.key()))
            keys.insert(it.key());
    return keys;
}
} // namespace

LegendStyleUndoRecorder::LegendStyleUndoRecorder(OpenSWMM::Render::LegendOverlayStyle *style,
                                                 QUndoStack *stack)
    : QObject(style), m_style(style), m_stack(stack)
{
    setObjectName(QStringLiteral("legendStyleUndoRecorder"));
    if (m_style) {
        m_settings = m_style->toJson();
        connect(m_style, &OpenSWMM::Render::LegendOverlayStyle::changed, this,
                &LegendStyleUndoRecorder::onChanged);
    }
}

LegendStyleUndoRecorder::~LegendStyleUndoRecorder() = default;

LegendStyleUndoRecorder *
LegendStyleUndoRecorder::forStyle(const OpenSWMM::Render::LegendOverlayStyle *style)
{
    return style ? style->findChild<LegendStyleUndoRecorder *>(
                       QStringLiteral("legendStyleUndoRecorder"), Qt::FindDirectChildrenOnly)
                 : nullptr;
}

void LegendStyleUndoRecorder::resync()
{
    m_pending = false;
    if (m_style)
        m_settings = m_style->toJson();
}

void LegendStyleUndoRecorder::apply(const QJsonObject &settings)
{
    if (!m_style)
        return;
    m_applying = true;
    m_style->fromJson(settings);
    m_applying = false;
    m_pending = false;
    m_settings = m_style->toJson();
}

void LegendStyleUndoRecorder::onChanged()
{
    if (m_applying || m_pending)
        return;
    // One action can call many setters: record once control returns to the event loop.
    m_pending = true;
    QTimer::singleShot(0, this, &LegendStyleUndoRecorder::record);
}

void LegendStyleUndoRecorder::record()
{
    if (!m_pending || !m_style)
        return;
    m_pending = false;
    const QJsonObject after = m_style->toJson();
    if (after == m_settings)
        return;
    const QJsonObject before = m_settings;
    m_settings = after;
    if (m_stack)
        m_stack->push(new LegendStyleSnapshotCommand(this, before, after));
}

LegendStyleSnapshotCommand::LegendStyleSnapshotCommand(LegendStyleUndoRecorder *recorder,
                                                       QJsonObject before, QJsonObject after)
    : QUndoCommand(QCoreApplication::translate("LegendStyleUndoRecorder", "Change legend")),
      m_recorder(recorder), m_before(std::move(before)), m_after(std::move(after))
{
    m_keys = differingKeys(m_before, m_after);
    m_age.start();
}

void LegendStyleSnapshotCommand::undo()
{
    if (m_recorder)
        m_recorder->apply(m_before);
}

void LegendStyleSnapshotCommand::redo()
{
    if (m_firstRedo) {
        m_firstRedo = false; // already applied when pushed
        return;
    }
    if (m_recorder)
        m_recorder->apply(m_after);
}

bool LegendStyleSnapshotCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const LegendStyleSnapshotCommand *>(other);
    if (!next || next->m_recorder != m_recorder || next->m_keys != m_keys ||
        m_age.elapsed() > LegendStyleUndoRecorder::kMergeMs)
        return false;
    m_after = next->m_after;
    m_age.restart();
    return true;
}

} // namespace openswmmvis::map
