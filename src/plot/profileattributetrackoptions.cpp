/*!
 * \file   profileattributetrackoptions.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See profileattributetrackoptions.h for the design contract.
 */
#include "plot/profileattributetrackoptions.h"

#include <QMetaObject>
#include <QMetaProperty>
#include <QSettings>
#include <QSignalBlocker>

#include <algorithm>

using openswmmvis::plot::PlotAttribute;
using openswmmvis::plot::linkPlotAttributes;
using openswmmvis::plot::nodePlotAttributes;

namespace {

// Seed colors — one distinguishable hue per attribute so freshly-enabled
// tracks don't all come up the same color. Users restyle via the pen
// properties; these are only the first-run defaults.
QColor seedColorFor(PlotAttribute a)
{
    switch (a) {
    case PlotAttribute::NodeDepth:          return QColor(0x1F, 0x77, 0xB4);
    case PlotAttribute::NodeHead:           return QColor(0x17, 0xBE, 0xCF);
    case PlotAttribute::NodeVolume:         return QColor(0x94, 0x67, 0xBD);
    case PlotAttribute::NodeLateralInflow:  return QColor(0x8C, 0x56, 0x4B);
    case PlotAttribute::NodeTotalInflow:    return QColor(0x2C, 0xA0, 0x2C);
    case PlotAttribute::NodeOverflow:       return QColor(0xD6, 0x27, 0x28);
    case PlotAttribute::LinkFlow:           return QColor(0x1F, 0x77, 0xB4);
    case PlotAttribute::LinkDepth:          return QColor(0xFF, 0x7F, 0x0E);
    case PlotAttribute::LinkVelocity:       return QColor(0x94, 0x67, 0xBD);
    case PlotAttribute::LinkVolume:         return QColor(0x8C, 0x56, 0x4B);
    case PlotAttribute::LinkCapacity:       return QColor(0xE3, 0x77, 0xC2);
    default:                                return QColor(0x7F, 0x7F, 0x7F);
    }
}

} // namespace

ProfileAttributeTrackOptions::ProfileAttributeTrackOptions(QObject *parent)
    : QObject(parent)
{
    const auto seed = [this](const QVector<PlotAttribute> &attrs) {
        for (PlotAttribute a : attrs) {
            m_visible.insert(int(a), false);
            m_pens.insert(int(a), QPen(seedColorFor(a), 1.6));
        }
    };
    seed(nodePlotAttributes());
    seed(linkPlotAttributes());
}

// ── Generic accessors ──────────────────────────────────────────────────

bool ProfileAttributeTrackOptions::isAttributeVisible(PlotAttribute a) const
{
    return m_visible.value(int(a), false);
}

void ProfileAttributeTrackOptions::setAttributeVisible(PlotAttribute a, bool on)
{
    if (m_visible.value(int(a), false) == on) return;
    m_visible.insert(int(a), on);
    emit changed();
}

QPen ProfileAttributeTrackOptions::penFor(PlotAttribute a) const
{
    return m_pens.value(int(a), QPen(seedColorFor(a), 1.6));
}

void ProfileAttributeTrackOptions::setPenFor(PlotAttribute a, const QPen &pen)
{
    if (m_pens.value(int(a)) == pen) return;
    m_pens.insert(int(a), pen);
    emit changed();
}

QVector<PlotAttribute> ProfileAttributeTrackOptions::visibleAttributes() const
{
    QVector<PlotAttribute> out;
    for (PlotAttribute a : nodePlotAttributes())
        if (isAttributeVisible(a)) out.push_back(a);
    for (PlotAttribute a : linkPlotAttributes())
        if (isAttributeVisible(a)) out.push_back(a);
    return out;
}

bool ProfileAttributeTrackOptions::anyAttributeVisible() const
{
    return std::any_of(m_visible.cbegin(), m_visible.cend(),
                       [](bool v) { return v; });
}

// ── Chrome setters ─────────────────────────────────────────────────────

void ProfileAttributeTrackOptions::setTrackHeightPx(int px)
{
    px = std::clamp(px, 60, 400);
    if (m_trackHeightPx == px) return;
    m_trackHeightPx = px;
    emit changed();
}

void ProfileAttributeTrackOptions::setShowTrackTitles(bool on)
{
    if (m_showTrackTitles == on) return;
    m_showTrackTitles = on;
    emit changed();
}

void ProfileAttributeTrackOptions::setEnvelopesVisible(bool on)
{
    if (m_envelopesVisible == on) return;
    m_envelopesVisible = on;
    emit changed();
}

void ProfileAttributeTrackOptions::setEnvelopeOpacity(double opacity01)
{
    opacity01 = std::clamp(opacity01, 0.0, 1.0);
    if (m_envelopeOpacity == opacity01) return;
    m_envelopeOpacity = opacity01;
    emit changed();
}

// ── Persistence ────────────────────────────────────────────────────────

void ProfileAttributeTrackOptions::writeTo(QSettings &s) const
{
    const QMetaObject *mo = metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        s.setValue(QLatin1String(p.name()), p.read(this));
    }
}

void ProfileAttributeTrackOptions::readFrom(QSettings &s)
{
    // Block per-setter emissions; one changed() at the end tells every view
    // to refresh once instead of 30 times.
    {
        const QSignalBlocker blocker(this);
        const QMetaObject *mo = metaObject();
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            const QString key = QLatin1String(p.name());
            if (s.contains(key))
                p.write(this, s.value(key));
        }
    }
    emit changed();
}

QString ProfileAttributeTrackOptions::displayLabelFor(
    const QString &propertyName) const
{
    using openswmmvis::plot::labelFor;
    struct Entry { const char *prop; PlotAttribute attr; };
    static const Entry entries[] = {
        {"nodeDepth",         PlotAttribute::NodeDepth},
        {"nodeHead",          PlotAttribute::NodeHead},
        {"nodeVolume",        PlotAttribute::NodeVolume},
        {"nodeLateralInflow", PlotAttribute::NodeLateralInflow},
        {"nodeTotalInflow",   PlotAttribute::NodeTotalInflow},
        {"nodeOverflow",      PlotAttribute::NodeOverflow},
        {"linkFlow",          PlotAttribute::LinkFlow},
        {"linkDepth",         PlotAttribute::LinkDepth},
        {"linkVelocity",      PlotAttribute::LinkVelocity},
        {"linkVolume",        PlotAttribute::LinkVolume},
        {"linkCapacity",      PlotAttribute::LinkCapacity},
    };
    for (const Entry &e : entries) {
        const QLatin1String prop(e.prop);
        const bool node = propertyName.startsWith(QLatin1String("node"));
        const QString kind = node ? tr("Node") : tr("Link");
        if (propertyName == prop + QLatin1String("Visible"))
            return tr("%1 %2 — visible").arg(kind, labelFor(e.attr));
        if (propertyName == prop + QLatin1String("Pen"))
            return tr("%1 %2 — line").arg(kind, labelFor(e.attr));
    }
    if (propertyName == QLatin1String("trackHeightPx"))    return tr("Track height (px)");
    if (propertyName == QLatin1String("showTrackTitles"))  return tr("Show track titles");
    if (propertyName == QLatin1String("envelopesVisible")) return tr("Show min/max envelopes");
    if (propertyName == QLatin1String("envelopeOpacity"))  return tr("Envelope opacity (0–1)");
    return propertyName;
}

// ── Longhand Q_PROPERTY accessors (see header note on why not macros) ──

bool ProfileAttributeTrackOptions::nodeDepthVisible() const
{ return isAttributeVisible(PlotAttribute::NodeDepth); }
void ProfileAttributeTrackOptions::setNodeDepthVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeDepth, on); }
bool ProfileAttributeTrackOptions::nodeHeadVisible() const
{ return isAttributeVisible(PlotAttribute::NodeHead); }
void ProfileAttributeTrackOptions::setNodeHeadVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeHead, on); }
bool ProfileAttributeTrackOptions::nodeVolumeVisible() const
{ return isAttributeVisible(PlotAttribute::NodeVolume); }
void ProfileAttributeTrackOptions::setNodeVolumeVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeVolume, on); }
bool ProfileAttributeTrackOptions::nodeLateralInflowVisible() const
{ return isAttributeVisible(PlotAttribute::NodeLateralInflow); }
void ProfileAttributeTrackOptions::setNodeLateralInflowVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeLateralInflow, on); }
bool ProfileAttributeTrackOptions::nodeTotalInflowVisible() const
{ return isAttributeVisible(PlotAttribute::NodeTotalInflow); }
void ProfileAttributeTrackOptions::setNodeTotalInflowVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeTotalInflow, on); }
bool ProfileAttributeTrackOptions::nodeOverflowVisible() const
{ return isAttributeVisible(PlotAttribute::NodeOverflow); }
void ProfileAttributeTrackOptions::setNodeOverflowVisible(bool on)
{ setAttributeVisible(PlotAttribute::NodeOverflow, on); }
bool ProfileAttributeTrackOptions::linkFlowVisible() const
{ return isAttributeVisible(PlotAttribute::LinkFlow); }
void ProfileAttributeTrackOptions::setLinkFlowVisible(bool on)
{ setAttributeVisible(PlotAttribute::LinkFlow, on); }
bool ProfileAttributeTrackOptions::linkDepthVisible() const
{ return isAttributeVisible(PlotAttribute::LinkDepth); }
void ProfileAttributeTrackOptions::setLinkDepthVisible(bool on)
{ setAttributeVisible(PlotAttribute::LinkDepth, on); }
bool ProfileAttributeTrackOptions::linkVelocityVisible() const
{ return isAttributeVisible(PlotAttribute::LinkVelocity); }
void ProfileAttributeTrackOptions::setLinkVelocityVisible(bool on)
{ setAttributeVisible(PlotAttribute::LinkVelocity, on); }
bool ProfileAttributeTrackOptions::linkVolumeVisible() const
{ return isAttributeVisible(PlotAttribute::LinkVolume); }
void ProfileAttributeTrackOptions::setLinkVolumeVisible(bool on)
{ setAttributeVisible(PlotAttribute::LinkVolume, on); }
bool ProfileAttributeTrackOptions::linkCapacityVisible() const
{ return isAttributeVisible(PlotAttribute::LinkCapacity); }
void ProfileAttributeTrackOptions::setLinkCapacityVisible(bool on)
{ setAttributeVisible(PlotAttribute::LinkCapacity, on); }

QPen ProfileAttributeTrackOptions::nodeDepthPen() const
{ return penFor(PlotAttribute::NodeDepth); }
void ProfileAttributeTrackOptions::setNodeDepthPen(const QPen &p)
{ setPenFor(PlotAttribute::NodeDepth, p); }
QPen ProfileAttributeTrackOptions::nodeHeadPen() const
{ return penFor(PlotAttribute::NodeHead); }
void ProfileAttributeTrackOptions::setNodeHeadPen(const QPen &p)
{ setPenFor(PlotAttribute::NodeHead, p); }
QPen ProfileAttributeTrackOptions::nodeVolumePen() const
{ return penFor(PlotAttribute::NodeVolume); }
void ProfileAttributeTrackOptions::setNodeVolumePen(const QPen &p)
{ setPenFor(PlotAttribute::NodeVolume, p); }
QPen ProfileAttributeTrackOptions::nodeLateralInflowPen() const
{ return penFor(PlotAttribute::NodeLateralInflow); }
void ProfileAttributeTrackOptions::setNodeLateralInflowPen(const QPen &p)
{ setPenFor(PlotAttribute::NodeLateralInflow, p); }
QPen ProfileAttributeTrackOptions::nodeTotalInflowPen() const
{ return penFor(PlotAttribute::NodeTotalInflow); }
void ProfileAttributeTrackOptions::setNodeTotalInflowPen(const QPen &p)
{ setPenFor(PlotAttribute::NodeTotalInflow, p); }
QPen ProfileAttributeTrackOptions::nodeOverflowPen() const
{ return penFor(PlotAttribute::NodeOverflow); }
void ProfileAttributeTrackOptions::setNodeOverflowPen(const QPen &p)
{ setPenFor(PlotAttribute::NodeOverflow, p); }
QPen ProfileAttributeTrackOptions::linkFlowPen() const
{ return penFor(PlotAttribute::LinkFlow); }
void ProfileAttributeTrackOptions::setLinkFlowPen(const QPen &p)
{ setPenFor(PlotAttribute::LinkFlow, p); }
QPen ProfileAttributeTrackOptions::linkDepthPen() const
{ return penFor(PlotAttribute::LinkDepth); }
void ProfileAttributeTrackOptions::setLinkDepthPen(const QPen &p)
{ setPenFor(PlotAttribute::LinkDepth, p); }
QPen ProfileAttributeTrackOptions::linkVelocityPen() const
{ return penFor(PlotAttribute::LinkVelocity); }
void ProfileAttributeTrackOptions::setLinkVelocityPen(const QPen &p)
{ setPenFor(PlotAttribute::LinkVelocity, p); }
QPen ProfileAttributeTrackOptions::linkVolumePen() const
{ return penFor(PlotAttribute::LinkVolume); }
void ProfileAttributeTrackOptions::setLinkVolumePen(const QPen &p)
{ setPenFor(PlotAttribute::LinkVolume, p); }
QPen ProfileAttributeTrackOptions::linkCapacityPen() const
{ return penFor(PlotAttribute::LinkCapacity); }
void ProfileAttributeTrackOptions::setLinkCapacityPen(const QPen &p)
{ setPenFor(PlotAttribute::LinkCapacity, p); }
