/**
 * @file      swmmelementsymboladapter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QObject wrapper exposing SWMMElementSymbol fields as Q_PROPERTYs.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY symbolChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY symbolChanged)
    Q_PROPERTY(double outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY symbolChanged)
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY symbolChanged)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape READ markerShape WRITE setMarkerShape
                   NOTIFY symbolChanged)
    Q_PROPERTY(bool showLabel READ showLabel WRITE setShowLabel NOTIFY symbolChanged)
    Q_PROPERTY(QFont labelFont READ labelFont WRITE setLabelFont NOTIFY symbolChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY symbolChanged)
    Q_PROPERTY(bool showArrows READ showArrows WRITE setShowArrows NOTIFY symbolChanged)
    Q_PROPERTY(double arrowSize READ arrowSize WRITE setArrowSize NOTIFY symbolChanged)
    Q_PROPERTY(double arrowWidth READ arrowWidth WRITE setArrowWidth NOTIFY symbolChanged)
    Q_PROPERTY(QColor arrowColor READ arrowColor WRITE setArrowColor NOTIFY symbolChanged)
    Q_PROPERTY(bool arrowOnlyWhenFlowPos READ arrowOnlyWhenFlowPos WRITE setArrowOnlyWhenFlowPos
                   NOTIFY symbolChanged)

    Q_CLASSINFO("group:fillColor", "Fill")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidth", "Outline")
    Q_CLASSINFO("group:size", "Symbology")
    Q_CLASSINFO("group:markerShape", "Symbology")
    Q_CLASSINFO("group:showLabel", "Labels")
    Q_CLASSINFO("group:labelFont", "Labels")
    Q_CLASSINFO("group:labelColor", "Labels")
    Q_CLASSINFO("group:showArrows", "Flow arrows")
    Q_CLASSINFO("group:arrowSize", "Flow arrows")
    Q_CLASSINFO("group:arrowWidth", "Flow arrows")
    Q_CLASSINFO("group:arrowColor", "Flow arrows")
    Q_CLASSINFO("group:arrowOnlyWhenFlowPos", "Flow arrows")

public:
    using Writer = std::function<void(const SWMMElementSymbol &)>;

    /**
     * @brief Create a cached property-editing facade with synchronous write-back.
     * @param[in] initial Symbol copied/moved into local state; the source layer is not read automatically.
     * @param[in] writer Optional callback retained by value, invoked after each effective setter with the whole cached symbol. Captured objects must remain valid through callbacks.
     * @param[in] parent Optional QObject owner, normally the source layer.
     * @note Use on the owning GUI thread. No numeric clamping, renderer validation, source IO or error reporting is added.
     */
    explicit SwmmElementSymbolAdapter(SWMMElementSymbol initial, Writer writer,
                                      QObject *parent = nullptr)
        : QObject(parent), m_sym(std::move(initial)), m_writer(std::move(writer))
    {
    }

    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Element fill colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor fillColor() const { return m_sym.fillColor; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Element outline/line pen colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor outlineColor() const { return m_sym.outlineColor; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Element outline/line pen width in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double outlineWidth() const { return m_sym.outlineWidth; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point marker diameter or category-dependent line size in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double size() const { return m_sym.size; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point-marker glyph, ignored by line/polygon categories. Returned by value without consulting the source layer.
     */
    [[nodiscard]] OpenSWMM::Render::MarkerShape markerShape() const { return m_sym.markerShape; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Whether names are requested as element labels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] bool showLabel() const { return m_sym.showLabel; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Element label font with Qt point/pixel sizing. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QFont labelFont() const { return m_sym.labelFont; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Element label colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor labelColor() const { return m_sym.labelColor; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Whether link-direction arrows are requested; ignored for point/polygon categories. Returned by value without consulting the source layer.
     */
    [[nodiscard]] bool showArrows() const { return m_sym.showArrows; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Link arrowhead length along the link in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double arrowSize() const { return m_sym.arrowSize; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Link arrowhead width across the link in logical pixels, independent of length. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double arrowWidth() const { return m_sym.arrowWidth; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Link arrow colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor arrowColor() const { return m_sym.arrowColor; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Whether arrow display requires positive bound result flow; this does not itself bind/load results. Returned by value without consulting the source layer.
     */
    [[nodiscard]] bool arrowOnlyWhenFlowPos() const { return m_sym.arrowOnlyWhenFlowPos; }

    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Element fill colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setFillColor(const QColor &v)
    {
        if (m_sym.fillColor == v)
            return;
        m_sym.fillColor = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Element outline/line pen colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setOutlineColor(const QColor &v)
    {
        if (m_sym.outlineColor == v)
            return;
        m_sym.outlineColor = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Element outline/line pen width in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setOutlineWidth(double v)
    {
        if (qFuzzyCompare(m_sym.outlineWidth, v))
            return;
        m_sym.outlineWidth = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point marker diameter or category-dependent line size in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setSize(double v)
    {
        if (qFuzzyCompare(m_sym.size, v))
            return;
        m_sym.size = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point-marker glyph, ignored by line/polygon categories. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerShape(OpenSWMM::Render::MarkerShape v)
    {
        if (m_sym.markerShape == v)
            return;
        m_sym.markerShape = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Whether names are requested as element labels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setShowLabel(bool v)
    {
        if (m_sym.showLabel == v)
            return;
        m_sym.showLabel = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Element label font with Qt point/pixel sizing. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLabelFont(const QFont &v)
    {
        if (m_sym.labelFont == v)
            return;
        m_sym.labelFont = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Element label colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLabelColor(const QColor &v)
    {
        if (m_sym.labelColor == v)
            return;
        m_sym.labelColor = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Whether link-direction arrows are requested; ignored for point/polygon categories. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setShowArrows(bool v)
    {
        if (m_sym.showArrows == v)
            return;
        m_sym.showArrows = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Link arrowhead length along the link in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setArrowSize(double v)
    {
        if (qFuzzyCompare(m_sym.arrowSize, v))
            return;
        m_sym.arrowSize = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Link arrowhead width across the link in logical pixels, independent of length. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setArrowWidth(double v)
    {
        if (qFuzzyCompare(m_sym.arrowWidth, v))
            return;
        m_sym.arrowWidth = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Link arrow colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setArrowColor(const QColor &v)
    {
        if (m_sym.arrowColor == v)
            return;
        m_sym.arrowColor = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Whether arrow display requires positive bound result flow; this does not itself bind/load results. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setArrowOnlyWhenFlowPos(bool v)
    {
        if (m_sym.arrowOnlyWhenFlowPos == v)
            return;
        m_sym.arrowOnlyWhenFlowPos = v;
        commit();
    }

    /**
     * @return Borrowed reference to the cached whole symbol, valid only while the adapter lives and changed by future setters/restore/resync. Copy it to retain an undo snapshot.
     */
    [[nodiscard]] const SWMMElementSymbol &snapshot() const { return m_sym; }
    /**
     * @brief Restore a whole undo/cancel snapshot through the normal write-back path.
     * @param[in] s Complete symbol copied without validation/equality suppression.
     * @note Always invokes the optional writer and emits symbolChanged(); use resyncFrom() to refresh without writing back.
     */
    void restore(const SWMMElementSymbol &s)
    {
        m_sym = s;
        commit();
    }

    /*! Refresh the adapter's cached struct from the layer WITHOUT invoking
     *  the writer (no write-back loop). Emits symbolChanged so mounted
     *  editors re-read. The layer uses this to keep its persistent adapter
     *  set truthful when a symbol changes through another path
     *  (setKindRenderer back-write, style import, Cancel rollback).
     * @param[in] s Whole source symbol copied unconditionally into the cache; writer is not called.
     * @note Always emits symbolChanged(), even for equal state, so editors reread the complete bag. GUI-thread operation; does not validate or update the source layer.
     */
    void resyncFrom(const SWMMElementSymbol &s)
    {
        m_sym = s;
        emit symbolChanged();
    }

signals:
    /**
     * @brief Tell property editors to reread cached state after commit or resynchronization.
     * @note May be emitted more than once for one edit when the writer updates a layer that resynchronizes this adapter. No payload or equality guarantee is supplied.
     */
    void symbolChanged();

private:
    /**
     * @brief Invoke the retained writer with current cached symbology, then emit symbolChanged().
     * @note Empty writer still permits local edits/notification. Callback runs synchronously on the caller thread; no error handling, rollback, reentrancy guard or thread dispatch is supplied.
     */
    void commit()
    {
        if (m_writer)
            m_writer(m_sym);
        emit symbolChanged();
    }
    SWMMElementSymbol m_sym;
    Writer m_writer;
};

#endif // OPENSWMMVIS_LAYERS_SWMMELEMENTSYMBOLADAPTER_H
