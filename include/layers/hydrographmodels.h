/*!
 * \file   hydrographmodels.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BS Phase 6.9.2 — hydrograph MVC layer.
 *
 * Four QAbstractItem/Table/ListModel subclasses owned by SWMMModelLayer.
 * Each is a thin Qt view onto the engine's [HYDROGRAPHS] / [RDII_DECAY]
 * state and listens to SWMMModelLayer::hydrographChanged(name) so every
 * subscribed view (HydrographGroupEditor, SWMMHydrographPropertyAdapter,
 * Object Browser, NewDataObjectDialog, NodeCompoundEditDialog) stays in
 * sync without polling.
 *
 * Mutation is one-way: views call layer->applyHydrograph*() /
 * applyRdiiDecay*(); the layer wraps the BS-02 C API and emits
 * hydrographChanged(). The models themselves are read-only with respect
 * to engine state — their setData() implementations forward to those
 * apply* helpers and rely on the resulting signal for the refresh.
 *
 * Per-(name, month) rebinding uses setContext() rather than per-key
 * cached instances; only the editor needs (name, month)-specific views
 * and only one at a time per tab.
 *
 * See: docs/GUI_IMPLEMENTATION_PLAN.md Slice BS Phase 6.9.2.
 */

#ifndef HYDROGRAPHMODELS_H
#define HYDROGRAPHMODELS_H

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QObject>
#include <QString>

class SWMMModelLayer;

// ---------------------------------------------------------------------------
// Group list model — one row per UH group name, in engine first-occurrence
// order. Editable in place: setData on column 0 routes through
// SWMMModelLayer::applyHydrographRenameGroup.
// ---------------------------------------------------------------------------

class HydrographGroupListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit HydrographGroupListModel(SWMMModelLayer *layer);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data    (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData (const QModelIndex &index, const QVariant &value,
                       int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

    /*! Look up the row index of a group by name, or -1 if not present. */
    int indexOf(const QString &name) const;
    /*! Read the group name at a given row, or empty if out of bounds. */
    QString nameAt(int row) const;

private slots:
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
};

// ---------------------------------------------------------------------------
// RTK table model — 3 rows × 4 cols for one (group, month). Column 0 is the
// non-editable response label (Short-Term / Medium-Term / Long-Term). Columns
// 1..3 are R, T, K. Blank cells (no engine entry yet for that response)
// render as empty QVariant — distinct from explicit 0 — so the user can
// tell which responses have data.
// ---------------------------------------------------------------------------

class HydrographRtkTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColResponse = 0, ColR = 1, ColT = 2, ColK = 3, ColCount = 4 };

    explicit HydrographRtkTableModel(SWMMModelLayer *layer);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data    (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData (const QModelIndex &index, const QVariant &value,
                       int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

    /*! Rebind to a different (group, month). month=-1 represents the engine's
     *  "ALL" encoding. Emits modelReset(). */
    void setContext(const QString &name, int month);

    QString currentName()  const { return m_name; }
    int     currentMonth() const { return m_month; }

private slots:
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString         m_name;
    int             m_month = -1;
};

// ---------------------------------------------------------------------------
// Linear IA table model — 3 rows × 4 cols. Same shape as the RTK model but
// the editable columns are Dmax / Drec / Do.
// ---------------------------------------------------------------------------

class HydrographIaTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColResponse = 0, ColDmax = 1, ColDrec = 2, ColDo = 3, ColCount = 4 };

    explicit HydrographIaTableModel(SWMMModelLayer *layer);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data    (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData (const QModelIndex &index, const QVariant &value,
                       int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

    /*! Rebind to a different (group, month). month=-1 = ALL. */
    void setContext(const QString &name, int month);

    QString currentName()  const { return m_name; }
    int     currentMonth() const { return m_month; }

private slots:
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString         m_name;
    int             m_month = -1;
};

// ---------------------------------------------------------------------------
// Exponential IA decay table model — 3 rows × 11 cols. Season-agnostic, so
// setContext() takes only a group name. Active is a Qt::CheckStateRole on
// column 1; existence of a [RDII_DECAY] row IS the "active" flag in the
// engine model. Unchecking Active calls applyRdiiDecayRemove(); checking
// a blank row calls applyRdiiDecaySet() with sane defaults (T_ref=10).
// Snow (column 8) is a second checkbox enabling the row's degree-day snow
// model; its two numeric columns (snow_T, snow_ddf) grey out until both
// Active and Snow are checked.
// ---------------------------------------------------------------------------

class HydrographDecayTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColResponse = 0,
        ColActive   = 1,
        ColKdep     = 2,
        ColK0       = 3,
        ColKT       = 4,
        ColTref     = 5,
        ColTheta    = 6,
        ColTfreeze  = 7,
        ColSnow     = 8,
        ColSnowT    = 9,
        ColSnowDdf  = 10,
        ColCount    = 11
    };

    explicit HydrographDecayTableModel(SWMMModelLayer *layer);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data    (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData (const QModelIndex &index, const QVariant &value,
                       int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

    /*! Rebind to a different group. */
    void setContext(const QString &name);

    QString currentName() const { return m_name; }

private slots:
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString         m_name;
};

#endif // HYDROGRAPHMODELS_H
