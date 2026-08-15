/*!
 * \file   selectionrenderingprefs.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-class selection
 * pens and brushes (link / subcatchment / node / gage). Drives the
 * Preferences dialog's Selection page via QPropertyModel — getters
 * call PreferencesManager::selectionPen / selectionBrush, setters call
 * setSelectionPen / setSelectionBrush, so any edit routes back through
 * the singleton and fires its preferenceChanged signal exactly like a
 * hand-coded picker would.
 *
 * Note: links are stroked only, so no `linkBrush` property is exposed.
 * The link pen's width is interpreted ADDITIVELY over the base link
 * pen — see PreferencesManager::selectionPen() documentation.
 */

#ifndef SELECTIONRENDERINGPREFS_H
#define SELECTIONRENDERINGPREFS_H

#include <QBrush>
#include <QObject>
#include <QPen>

class SelectionRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen   linkPen           READ linkPen           WRITE setLinkPen           NOTIFY linkPenChanged)
    Q_PROPERTY(QPen   subcatchmentPen   READ subcatchmentPen   WRITE setSubcatchmentPen   NOTIFY subcatchmentPenChanged)
    Q_PROPERTY(QBrush subcatchmentFill  READ subcatchmentFill  WRITE setSubcatchmentFill  NOTIFY subcatchmentFillChanged)
    Q_PROPERTY(QPen   nodePen           READ nodePen           WRITE setNodePen           NOTIFY nodePenChanged)
    Q_PROPERTY(QBrush nodeFill          READ nodeFill          WRITE setNodeFill          NOTIFY nodeFillChanged)
    Q_PROPERTY(QPen   gagePen           READ gagePen           WRITE setGagePen           NOTIFY gagePenChanged)
    Q_PROPERTY(QBrush gageFill          READ gageFill          WRITE setGageFill          NOTIFY gageFillChanged)

public:
    explicit SelectionRenderingPrefs(QObject *parent = nullptr);

    [[nodiscard]] QPen   linkPen()          const;
    [[nodiscard]] QPen   subcatchmentPen()  const;
    [[nodiscard]] QBrush subcatchmentFill() const;
    [[nodiscard]] QPen   nodePen()          const;
    [[nodiscard]] QBrush nodeFill()         const;
    [[nodiscard]] QPen   gagePen()          const;
    [[nodiscard]] QBrush gageFill()         const;

    void setLinkPen(const QPen &pen);
    void setSubcatchmentPen(const QPen &pen);
    void setSubcatchmentFill(const QBrush &brush);
    void setNodePen(const QPen &pen);
    void setNodeFill(const QBrush &brush);
    void setGagePen(const QPen &pen);
    void setGageFill(const QBrush &brush);

signals:
    void linkPenChanged(const QPen &pen);
    void subcatchmentPenChanged(const QPen &pen);
    void subcatchmentFillChanged(const QBrush &brush);
    void nodePenChanged(const QPen &pen);
    void nodeFillChanged(const QBrush &brush);
    void gagePenChanged(const QPen &pen);
    void gageFillChanged(const QBrush &brush);
};

#endif // SELECTIONRENDERINGPREFS_H
