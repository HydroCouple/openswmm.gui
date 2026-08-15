/*!
 * \file   linkrenderingprefs.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-link-type pens
 * (conduit / pump / orifice / weir / outlet). Drives the Preferences
 * dialog's Rendering page via QPropertyModel — getters call
 * PreferencesManager::linkPen(), setters call setLinkPen(), so any edit
 * routes back through the singleton and fires its preferenceChanged
 * signal exactly like a hand-coded color picker would.
 */

#ifndef LINKRENDERINGPREFS_H
#define LINKRENDERINGPREFS_H

#include <QObject>
#include <QPen>

class LinkRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen conduit READ conduitPen WRITE setConduitPen NOTIFY conduitPenChanged)
    Q_PROPERTY(QPen pump    READ pumpPen    WRITE setPumpPen    NOTIFY pumpPenChanged)
    Q_PROPERTY(QPen orifice READ orificePen WRITE setOrificePen NOTIFY orificePenChanged)
    Q_PROPERTY(QPen weir    READ weirPen    WRITE setWeirPen    NOTIFY weirPenChanged)
    Q_PROPERTY(QPen outlet  READ outletPen  WRITE setOutletPen  NOTIFY outletPenChanged)

public:
    explicit LinkRenderingPrefs(QObject *parent = nullptr);

    [[nodiscard]] QPen conduitPen() const;
    [[nodiscard]] QPen pumpPen()    const;
    [[nodiscard]] QPen orificePen() const;
    [[nodiscard]] QPen weirPen()    const;
    [[nodiscard]] QPen outletPen()  const;

    void setConduitPen(const QPen &pen);
    void setPumpPen(const QPen &pen);
    void setOrificePen(const QPen &pen);
    void setWeirPen(const QPen &pen);
    void setOutletPen(const QPen &pen);

signals:
    void conduitPenChanged(const QPen &pen);
    void pumpPenChanged(const QPen &pen);
    void orificePenChanged(const QPen &pen);
    void weirPenChanged(const QPen &pen);
    void outletPenChanged(const QPen &pen);
};

#endif // LINKRENDERINGPREFS_H
