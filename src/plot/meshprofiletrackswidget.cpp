#include "plot/meshprofiletrackswidget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

using namespace ProfileSection;
using openswmmvis::io::Mesh2DValueStatus;
namespace {
QString statusText(Mesh2DValueStatus status)
{
    switch (status) {
    case Mesh2DValueStatus::Valid: return QObject::tr("Valid");
    case Mesh2DValueStatus::Missing: return QObject::tr("Missing");
    case Mesh2DValueStatus::Waterless: return QObject::tr("Waterless");
    case Mesh2DValueStatus::NotApplicable: return QObject::tr("Not applicable");
    }
    return QObject::tr("Missing");
}
QString units(const SampledSeries &s) { return s.unitsKnown ? s.units : QObject::tr("units unknown"); }
bool valid(const SeriesPoint &p) { return p.status == Mesh2DValueStatus::Valid && std::isfinite(p.value); }
}
void MeshProfileSamplesModel::setSection(const Section &section, const Definition &definition)
{
    beginResetModel(); m_section = section; m_definition = definition; m_rows.clear();
    m_section.series = builtInSeries(section,definition) + section.series;
    for (int s = 0; s < m_section.series.size(); ++s)
        for (int p = 0; p < m_section.series[s].points.size(); ++p) m_rows.append({s,p});
    endResetModel();
}
int MeshProfileSamplesModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }
int MeshProfileSamplesModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 10; }
QVariant MeshProfileSamplesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const auto row = m_rows[index.row()]; const auto &s = m_section.series[row.first];
    const auto &point = s.points[row.second];
    if (role == Qt::UserRole) return point.chainage;
    if (role == Qt::ToolTipRole) return s.error;
    if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) return {};
    switch (index.column()) {
    case 0: return s.definition.label.isEmpty() ? s.descriptor.label : s.definition.label;
    case 1: return QString::number(point.chainage, 'g', 12);
    case 2: return QString::number(point.scenePt.x(), 'g', 12);
    case 3: return QString::number(-point.scenePt.y(), 'g', 12);
    case 4: return valid(point) ? QString::number(point.value, 'g', 12) : QStringLiteral("—");
    case 5: return units(s);
    case 6: return statusText(point.status);
    case 7: return s.requestedTime.isValid() ? s.requestedTime.toString(Qt::ISODate) : tr("Not specified");
    case 8: return s.effectiveTime.isValid() ? s.effectiveTime.toString(Qt::ISODate) : tr("Static / unavailable");
    case 9: return s.sourcePath.isEmpty() ? s.definition.sourceId : s.sourcePath;
    }
    return {};
}
QVariant MeshProfileSamplesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    const QStringList headers{tr("Series"), tr("Distance [%1]").arg(m_definition.horizontalUnits),
        tr("Map X"), tr("Map Y"), tr("Value"), tr("Units"), tr("Status"), tr("Requested time"), tr("Effective time"), tr("Source")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}
MeshProfileTracksWidget::MeshProfileTracksWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus); setAccessibleName(tr("Scalar profile tracks"));
    setToolTip(tr("Each quantity retains its own units. Left and Right move the shared station cursor."));
    setMinimumHeight(150);
}
int MeshProfileTracksWidget::trackCount() const
{
    int count = 0;
    for (const auto &s : m_section.series)
        if (s.definition.visible && s.definition.role == SeriesRole::Scalar) ++count;
    return count;
}
void MeshProfileTracksWidget::setSection(const Section &section)
{
    m_section = section;
    if (!section.samples.isEmpty()) { m_xMin = section.samples.front().chainage; m_xMax = section.samples.back().chainage; }
    if (!(m_xMax > m_xMin)) m_xMax = m_xMin + 1;
    QStringList names;
    for (const auto &s : section.series)
        if (s.definition.visible && s.definition.role == SeriesRole::Scalar)
            names.append(QStringLiteral("%1 [%2]").arg(s.definition.label, units(s)));
    setAccessibleDescription(names.join(QStringLiteral("; ")));
    setMinimumHeight(std::max(1, trackCount()) * 150); update();
}
void MeshProfileTracksWidget::setViewRange(const QRectF &range)
{
    if (!std::isfinite(range.left()) || !std::isfinite(range.right()) || range.width() <= 0) return;
    m_xMin = range.left(); m_xMax = range.right(); update();
}
void MeshProfileTracksWidget::setCursorChainage(double chainage) { m_cursor = chainage; update(); }
void MeshProfileTracksWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    int track = 0;
    for (const auto &s : m_section.series) {
        if (!s.definition.visible || s.definition.role != SeriesRole::Scalar) continue;
        const int top = track++ * 150;
        const QRectF plot(64, top + 36, std::max(1, width()-80), 94);
        painter.setPen(palette().text().color());
        const QString timing = s.effectiveTime.isValid() ? s.effectiveTime.toString(Qt::ISODate) : tr("Static / unavailable");
        const QString title = QStringLiteral("%1 [%2] — %3").arg(s.definition.label, units(s), timing);
        painter.drawText(QRectF(8, top+3, width()-16, 28), Qt::AlignLeft|Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(title, Qt::ElideRight, width()-16));
        painter.drawRect(plot);
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto &point : s.points) if (valid(point)) { low = std::min(low,point.value); high = std::max(high,point.value); }
        if (s.definition.customRange) { low = s.definition.minimum; high = s.definition.maximum; }
        if (!s.error.isEmpty() || !std::isfinite(low) || !std::isfinite(high) || high < low) {
            painter.drawText(plot.adjusted(5,5,-5,-5), Qt::AlignCenter|Qt::TextWordWrap,
                             s.error.isEmpty() ? tr("No valid values") : s.error); continue;
        }
        if (high == low) { const double pad = std::max(1., std::abs(low)*0.05); low -= pad; high += pad; }
        painter.drawText(QRectF(0,plot.top()-7,59,20),Qt::AlignRight,QString::number(high,'g',5));
        painter.drawText(QRectF(0,plot.bottom()-12,59,20),Qt::AlignRight,QString::number(low,'g',5));
        const auto pixel = [&](const SeriesPoint &point) { return QPointF(plot.left()+(point.chainage-m_xMin)/(m_xMax-m_xMin)*plot.width(),plot.bottom()-(point.value-low)/(high-low)*plot.height()); };
        painter.save(); painter.setClipRect(plot); painter.setOpacity(s.definition.opacity); painter.setPen(s.definition.pen);
        QPainterPath path; bool active = false;
        for (const auto &point : s.points) {
            if (!valid(point)) { active = false; continue; }
            if (!active || point.breakBefore) path.moveTo(pixel(point)); else path.lineTo(pixel(point));
            active = true;
        }
        painter.drawPath(path); painter.setOpacity(1);
        if (m_cursor >= m_xMin && m_cursor <= m_xMax) {
            painter.setPen(QPen(palette().text().color(),1,Qt::DashLine));
            const double x = plot.left()+(m_cursor-m_xMin)/(m_xMax-m_xMin)*plot.width();
            painter.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
        }
        painter.restore();
    }
}
void MeshProfileTracksWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const double fraction = std::clamp((event->position().x()-64)/std::max(1,width()-80),0.,1.);
        m_cursor = m_xMin + fraction*(m_xMax-m_xMin); emit cursorChainageChanged(m_cursor); update(); event->accept();
    } else QWidget::mousePressEvent(event);
}
void MeshProfileTracksWidget::keyPressEvent(QKeyEvent *event)
{
    QVector<double> stations;
    for (const auto &s : m_section.series) for (const auto &point : s.points) stations.append(point.chainage);
    std::sort(stations.begin(),stations.end()); stations.erase(std::unique(stations.begin(),stations.end()),stations.end());
    if (stations.isEmpty()) { QWidget::keyPressEvent(event); return; }
    if (event->key() == Qt::Key_Home) m_cursor = stations.front();
    else if (event->key() == Qt::Key_End) m_cursor = stations.back();
    else if (event->key() == Qt::Key_Right) { auto it = std::upper_bound(stations.begin(),stations.end(),m_cursor); m_cursor = it == stations.end() ? stations.back() : *it; }
    else if (event->key() == Qt::Key_Left) { auto it = std::lower_bound(stations.begin(),stations.end(),m_cursor); m_cursor = it == stations.begin() ? stations.front() : *--it; }
    else { QWidget::keyPressEvent(event); return; }
    emit cursorChainageChanged(m_cursor); update(); event->accept();
}
