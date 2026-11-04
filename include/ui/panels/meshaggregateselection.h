/**
 * @file      meshaggregateselection.h
 * @brief     Several mesh elements of one kind edited together: common values, Mixed and
 *            validated shared field edits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Process-aware editing plan, Phase 3 (docs/development/process-aware-spatial-editing.md §7.1,
 * §9). The model side of the Properties aggregate tree. It owns an unfocused
 * MeshAttributeTableModel over the targets' layer and kind, so every read, validation rule and
 * write is the attribute table's own; setValue() commits a field through a staged transaction.
 *
 * Contract:
 *  - Targets are typed MeshObjectRefs captured at construction and resolved to rows at use.
 *  - Mixed is a display/edit state, never a value: opening an editor does not write anything.
 *  - setValue() validates and commits one field immediately as one undo step.
 *  - apply() is one undo step. A value the model refuses for any target rolls the whole step
 *    back and leaves no undo entry.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_MESHAGGREGATESELECTION_H
#define OPENSWMMVIS_UI_PANELS_MESHAGGREGATESELECTION_H

#include "selection/selectionmanager.h" // SWMMObjectRef
#include "ui/panels/meshattributetablemodel.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QVector>

class MapCanvas;
class SWMM2DMeshLayer;
class SWMMModelLayer;

class MeshAggregateSelection : public QObject
{
    Q_OBJECT
public:
    using Kind = MeshAttributeTableModel::Kind;

    /*! Summary of one field across the targets. */
    struct Field {
        bool mixed = false;  ///< Targets hold different values.
        QVariant display;    ///< Common DisplayRole value (invalid when mixed).
        QVariant edit;       ///< Common EditRole value (invalid when mixed).
        int editable = 0;    ///< Targets on which the field is editable.
    };

    /**
     * @param[in] layer Borrowed mesh holding every target; must outlive staging (the source
     *   model guards it with a QPointer).
     * @param[in] kind Element family shared by every target.
     * @param[in] targets Typed refs of that kind on that layer; duplicates are ignored.
     * @param[in] canvas Optional borrowed canvas whose undo stack receives apply().
     * @param[in] model Optional borrowed SWMM model behind the reference pickers.
     * @param[in] parent Optional QObject owner.
     */
    MeshAggregateSelection(SWMM2DMeshLayer *layer, Kind kind, const QVector<SWMMObjectRef> &targets,
                           MapCanvas *canvas, SWMMModelLayer *model, QObject *parent = nullptr);

    /** @return Owned field model shared by every read and write. */
    [[nodiscard]] MeshAttributeTableModel *source() const { return m_source; }
    /** @return Element family of the targets. */
    [[nodiscard]] Kind kind() const { return m_source->kind(); }
    /** @return Number of captured targets (resolved or not). */
    [[nodiscard]] int targetCount() const { return int(m_targets.size()); }
    /** @return Current rows of the targets that still resolve, in capture order. */
    [[nodiscard]] QList<int> rows() const;
    /** @return Captured targets that no longer resolve (deleted or remeshed). */
    [[nodiscard]] int unresolvedCount() const;

    /** @return Summary of \p column across the resolved targets (cached). */
    [[nodiscard]] Field field(int column) const;

    /*! Commit one field to all selected elements as one validated undo step. */
    bool setValue(int column, const QVariant &value);

    /** @return True when \p column holds a staged value. */
    [[nodiscard]] bool isStaged(int column) const { return m_staged.contains(column); }
    /** @return Staged value of \p column, or invalid. */
    [[nodiscard]] QVariant stagedValue(int column) const { return m_staged.value(column); }
    /** @return Staged columns in column order. */
    [[nodiscard]] QList<int> stagedColumns() const { return m_staged.keys(); }
    QString stagingNotice() const { return m_stagingNotice; }

    /*! Stage \p value for \p column. A value equal to a non-Mixed common value unstages it; an
     *  invalid value or a field not editable on every target is refused.
     * @return True when the staged set changed or already held the value. */
    bool stage(int column, const QVariant &value);
    /*! Drop \p column's staged value. */
    void unstage(int column);
    /*! Drop every staged value. */
    void discard();

    /*! Validate the staged values against the field schema and every target, then write them as
     *  one undo step in column order and confirm each target took each value.
     * @param[out] error Optional reason when nothing was written.
     * @return True when every staged value was written (staging is then cleared). False leaves the
     *   mesh, the staging and the undo history as they were. */
    bool apply(QString *error = nullptr);

    /*! Value as compared across targets: picker refs by name, NaN equal to NaN. */
    [[nodiscard]] static QVariant comparable(const QVariant &value);

signals:
    /*! Empty on a successful edit; otherwise the reason the edit was refused. */
    void editFinished(const QString &error);
    /*! The staged set changed. */
    void stagedChanged();

private:
    void invalidate() { m_cache.clear(); }
    void pruneInapplicableFields();
    [[nodiscard]] QString validate(int column, const QVariant &value,
                                   const QList<int> &rows) const;

    MeshAttributeTableModel *m_source;
    QVector<SWMMObjectRef> m_targets;
    QMap<int, QVariant> m_staged; ///< Column order is apply order.
    QString m_stagingNotice;
    quint64 m_draftRevision = 0;
    mutable QHash<int, Field> m_cache;
    mutable QList<int> m_rows; ///< Resolved target rows, rebuilt after a reset or layout change.
    mutable int m_unresolved = 0;
    mutable bool m_rowsValid = false;
    QPointer<MapCanvas> m_canvas;
};

#endif // OPENSWMMVIS_UI_PANELS_MESHAGGREGATESELECTION_H
