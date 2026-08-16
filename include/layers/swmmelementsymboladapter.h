/*!
 * \file   swmmelementsymboladapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QObject wrapper exposing SWMMElementSymbol fields as Q_PROPERTYs.
 *
 *         Slice U-4. SWMMElementSymbol is a plain struct so it can't drive
 *         the QPropertyModel-backed editor directly. This adapter holds a
 *         live copy of the struct + a callback that writes the updated
 *         copy back to the parent SWMMModelLayer (via setJunctionSymbol,
 *         setConduitSymbol, …). Every Q_PROPERTY setter fires the writeback
 *         immediately so edits propagate live.
 *
 *         The adapter is owned by the parent SWMMModelLayer (QObject
 *         parent-child) so its lifetime tracks the layer.
 */
#ifndef OPENSWMMVIS_LAYERS_SWMMELEMENTSYMBOLADAPTER_H
#define OPENSWMMVIS_LAYERS_SWMMELEMENTSYMBOLADAPTER_H

#include "layers/swmmmodellayer.h"

#include <QColor>
#include <QFont>
#include <QObject>

#include <functional>

class SwmmElementSymbolAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor  fillColor    READ fillColor    WRITE setFillColor    NOTIFY symbolChanged)
    Q_PROPERTY(QColor  outlineColor READ outlineColor WRITE setOutlineColor NOTIFY symbolChanged)
    Q_PROPERTY(double  outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY symbolChanged)
    Q_PROPERTY(double  size         READ size         WRITE setSize         NOTIFY symbolChanged)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape
               READ markerShape WRITE setMarkerShape NOTIFY symbolChanged)
    Q_PROPERTY(bool    showLabel    READ showLabel    WRITE setShowLabel    NOTIFY symbolChanged)
    Q_PROPERTY(QFont   labelFont    READ labelFont    WRITE setLabelFont    NOTIFY symbolChanged)
    Q_PROPERTY(QColor  labelColor   READ labelColor   WRITE setLabelColor   NOTIFY symbolChanged)
    Q_PROPERTY(bool    showArrows   READ showArrows   WRITE setShowArrows   NOTIFY symbolChanged)
    Q_PROPERTY(double  arrowSize    READ arrowSize    WRITE setArrowSize    NOTIFY symbolChanged)
    Q_PROPERTY(QColor  arrowColor   READ arrowColor   WRITE setArrowColor   NOTIFY symbolChanged)
    Q_PROPERTY(bool    arrowOnlyWhenFlowPos READ arrowOnlyWhenFlowPos
               WRITE setArrowOnlyWhenFlowPos NOTIFY symbolChanged)

    Q_CLASSINFO("group:fillColor",            "Fill")
    Q_CLASSINFO("group:outlineColor",         "Outline")
    Q_CLASSINFO("group:outlineWidth",         "Outline")
    Q_CLASSINFO("group:size",                 "Symbology")
    Q_CLASSINFO("group:markerShape",          "Symbology")
    Q_CLASSINFO("group:showLabel",            "Labels")
    Q_CLASSINFO("group:labelFont",            "Labels")
    Q_CLASSINFO("group:labelColor",           "Labels")
    Q_CLASSINFO("group:showArrows",           "Flow arrows")
    Q_CLASSINFO("group:arrowSize",            "Flow arrows")
    Q_CLASSINFO("group:arrowColor",           "Flow arrows")
    Q_CLASSINFO("group:arrowOnlyWhenFlowPos", "Flow arrows")

public:
    using Writer = std::function<void(const SWMMElementSymbol &)>;

    explicit SwmmElementSymbolAdapter(SWMMElementSymbol initial,
                                       Writer writer,
                                       QObject *parent = nullptr)
        : QObject(parent), m_sym(std::move(initial)), m_writer(std::move(writer))
    {}

    [[nodiscard]] QColor fillColor()    const { return m_sym.fillColor; }
    [[nodiscard]] QColor outlineColor() const { return m_sym.outlineColor; }
    [[nodiscard]] double outlineWidth() const { return m_sym.outlineWidth; }
    [[nodiscard]] double size()         const { return m_sym.size; }
    [[nodiscard]] OpenSWMM::Render::MarkerShape markerShape() const { return m_sym.markerShape; }
    [[nodiscard]] bool   showLabel()    const { return m_sym.showLabel; }
    [[nodiscard]] QFont  labelFont()    const { return m_sym.labelFont; }
    [[nodiscard]] QColor labelColor()   const { return m_sym.labelColor; }
    [[nodiscard]] bool   showArrows()           const { return m_sym.showArrows; }
    [[nodiscard]] double arrowSize()            const { return m_sym.arrowSize; }
    [[nodiscard]] QColor arrowColor()           const { return m_sym.arrowColor; }
    [[nodiscard]] bool   arrowOnlyWhenFlowPos() const { return m_sym.arrowOnlyWhenFlowPos; }

    void setFillColor(const QColor &v)    { if (m_sym.fillColor    == v) return; m_sym.fillColor    = v; commit(); }
    void setOutlineColor(const QColor &v) { if (m_sym.outlineColor == v) return; m_sym.outlineColor = v; commit(); }
    void setOutlineWidth(double v)        { if (qFuzzyCompare(m_sym.outlineWidth, v)) return; m_sym.outlineWidth = v; commit(); }
    void setSize(double v)                { if (qFuzzyCompare(m_sym.size, v)) return; m_sym.size = v; commit(); }
    void setMarkerShape(OpenSWMM::Render::MarkerShape v)
                                          { if (m_sym.markerShape == v) return; m_sym.markerShape = v; commit(); }
    void setShowLabel(bool v)             { if (m_sym.showLabel == v) return; m_sym.showLabel = v; commit(); }
    void setLabelFont(const QFont &v)     { if (m_sym.labelFont == v) return; m_sym.labelFont = v; commit(); }
    void setLabelColor(const QColor &v)   { if (m_sym.labelColor == v) return; m_sym.labelColor = v; commit(); }
    void setShowArrows(bool v)            { if (m_sym.showArrows == v) return; m_sym.showArrows = v; commit(); }
    void setArrowSize(double v)           { if (qFuzzyCompare(m_sym.arrowSize, v)) return; m_sym.arrowSize = v; commit(); }
    void setArrowColor(const QColor &v)   { if (m_sym.arrowColor == v) return; m_sym.arrowColor = v; commit(); }
    void setArrowOnlyWhenFlowPos(bool v)  { if (m_sym.arrowOnlyWhenFlowPos == v) return; m_sym.arrowOnlyWhenFlowPos = v; commit(); }

    [[nodiscard]] const SWMMElementSymbol &snapshot() const { return m_sym; }
    void restore(const SWMMElementSymbol &s) { m_sym = s; commit(); }

    /*! Refresh the adapter's cached struct from the layer WITHOUT invoking
     *  the writer (no write-back loop). Emits symbolChanged so mounted
     *  editors re-read. The layer uses this to keep its persistent adapter
     *  set truthful when a symbol changes through another path
     *  (setKindRenderer back-write, style import, Cancel rollback). */
    void resyncFrom(const SWMMElementSymbol &s)
    {
        m_sym = s;
        emit symbolChanged();
    }

signals:
    void symbolChanged();

private:
    void commit()
    {
        if (m_writer) m_writer(m_sym);
        emit symbolChanged();
    }
    SWMMElementSymbol m_sym;
    Writer            m_writer;
};

#endif // OPENSWMMVIS_LAYERS_SWMMELEMENTSYMBOLADAPTER_H
