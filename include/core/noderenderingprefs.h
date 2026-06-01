/*!
 * \file   noderenderingprefs.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's per-node-type
 * outline pen, fill brush, and marker size (junction / outfall /
 * storage / divider). Drives the Preferences dialog's Rendering page
 * via QPropertyModel — getters call PreferencesManager::nodePen /
 * nodeBrush / nodeSize, setters call setNodePen / setNodeBrush /
 * setNodeSize, so any edit routes back through the singleton and
 * fires its preferenceChanged signal exactly like the link-pen
 * bridge does.
 */

#ifndef NODERENDERINGPREFS_H
#define NODERENDERINGPREFS_H

#include <QBrush>
#include <QObject>
#include <QPen>

class NodeRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPen   junctionPen  READ junctionPen  WRITE setJunctionPen  NOTIFY junctionPenChanged)
    Q_PROPERTY(QBrush junctionFill READ junctionFill WRITE setJunctionFill NOTIFY junctionFillChanged)
    Q_PROPERTY(double junctionSize READ junctionSize WRITE setJunctionSize NOTIFY junctionSizeChanged)

    Q_PROPERTY(QPen   outfallPen   READ outfallPen   WRITE setOutfallPen   NOTIFY outfallPenChanged)
    Q_PROPERTY(QBrush outfallFill  READ outfallFill  WRITE setOutfallFill  NOTIFY outfallFillChanged)
    Q_PROPERTY(double outfallSize  READ outfallSize  WRITE setOutfallSize  NOTIFY outfallSizeChanged)

    Q_PROPERTY(QPen   storagePen   READ storagePen   WRITE setStoragePen   NOTIFY storagePenChanged)
    Q_PROPERTY(QBrush storageFill  READ storageFill  WRITE setStorageFill  NOTIFY storageFillChanged)
    Q_PROPERTY(double storageSize  READ storageSize  WRITE setStorageSize  NOTIFY storageSizeChanged)

    Q_PROPERTY(QPen   dividerPen   READ dividerPen   WRITE setDividerPen   NOTIFY dividerPenChanged)
    Q_PROPERTY(QBrush dividerFill  READ dividerFill  WRITE setDividerFill  NOTIFY dividerFillChanged)
    Q_PROPERTY(double dividerSize  READ dividerSize  WRITE setDividerSize  NOTIFY dividerSizeChanged)

public:
    explicit NodeRenderingPrefs(QObject *parent = nullptr);

    [[nodiscard]] QPen   junctionPen()  const;
    [[nodiscard]] QBrush junctionFill() const;
    [[nodiscard]] double junctionSize() const;

    [[nodiscard]] QPen   outfallPen()   const;
    [[nodiscard]] QBrush outfallFill()  const;
    [[nodiscard]] double outfallSize()  const;

    [[nodiscard]] QPen   storagePen()   const;
    [[nodiscard]] QBrush storageFill()  const;
    [[nodiscard]] double storageSize()  const;

    [[nodiscard]] QPen   dividerPen()   const;
    [[nodiscard]] QBrush dividerFill()  const;
    [[nodiscard]] double dividerSize()  const;

    void setJunctionPen(const QPen &pen);
    void setJunctionFill(const QBrush &brush);
    void setJunctionSize(double sizePx);

    void setOutfallPen(const QPen &pen);
    void setOutfallFill(const QBrush &brush);
    void setOutfallSize(double sizePx);

    void setStoragePen(const QPen &pen);
    void setStorageFill(const QBrush &brush);
    void setStorageSize(double sizePx);

    void setDividerPen(const QPen &pen);
    void setDividerFill(const QBrush &brush);
    void setDividerSize(double sizePx);

signals:
    void junctionPenChanged(const QPen &pen);
    void junctionFillChanged(const QBrush &brush);
    void junctionSizeChanged(double sizePx);

    void outfallPenChanged(const QPen &pen);
    void outfallFillChanged(const QBrush &brush);
    void outfallSizeChanged(double sizePx);

    void storagePenChanged(const QPen &pen);
    void storageFillChanged(const QBrush &brush);
    void storageSizeChanged(double sizePx);

    void dividerPenChanged(const QPen &pen);
    void dividerFillChanged(const QBrush &brush);
    void dividerSizeChanged(double sizePx);
};

#endif // NODERENDERINGPREFS_H
