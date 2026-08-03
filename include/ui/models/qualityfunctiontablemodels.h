/*!
 * \file   qualityfunctiontablemodels.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Table models for the unified Land Use editor's Buildup and
 *         Washoff tabs (iteration 4).
 *
 * Rows are the engine's pollutants; columns are the [BUILDUP]/[WASHOFF]
 * function parameters for one selected land use. Cells read and write the
 * engine directly (swmm_buildup_get/set, swmm_washoff_get/set) —
 * apply-as-you-go, matching the compound-edit dialogs. Call
 * refresh() after the pollutant set or the bound land use changes;
 * rows re-dimension automatically ("sections dynamically added/removed").
 *
 * EXT-buildup guard: when the function is EXT, coefficient C3 holds a
 * time-series TABLE INDEX, not a number (see QualityHandler.cpp) — the
 * cell is read-only with an explanatory tooltip so a numeric edit can
 * never corrupt it.
 */
#ifndef OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H
#define OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H

#include <QAbstractTableModel>
#include <QStringList>
#include <QStyledItemDelegate>

namespace openswmmvis::ui {

/// Shared base: pollutant rows + one bound (engine, land use) target.
class QualityFunctionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit QualityFunctionTableModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent) {}

    /*! Bind the engine handle and land use index this table edits.
     *  luIndex < 0 unbinds (empty table). */
    void bind(void *engineHandle, int luIndex);
    void refresh();   ///< re-query pollutant rows (after add/remove/rename)

    int rowCount(const QModelIndex &parent = {}) const override;

    [[nodiscard]] void *engineHandle() const { return m_engine; }
    [[nodiscard]] int   landUseIndex() const { return m_luIndex; }

protected:
    QString pollutantName(int row) const;

    void *m_engine  = nullptr;
    int   m_luIndex = -1;
    int   m_rows    = 0;
};

class BuildupTableModel : public QualityFunctionTableModel
{
    Q_OBJECT

public:
    enum Column { ColPollutant, ColFunction, ColC1, ColC2, ColC3,
                  ColNormalizer, ColCount };

    using QualityFunctionTableModel::QualityFunctionTableModel;

    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    static QStringList functionNames();    ///< NONE POW EXP SAT EXT
    static QStringList normalizerNames();  ///< AREA CURB
};

class WashoffTableModel : public QualityFunctionTableModel
{
    Q_OBJECT

public:
    enum Column { ColPollutant, ColFunction, ColCoeff, ColExponent,
                  ColSweepEffic, ColBmpEffic, ColCount };

    using QualityFunctionTableModel::QualityFunctionTableModel;

    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    static QStringList functionNames();    ///< NONE EXP RC EMC
};

/*! Combo editor for the enum columns (Function / Normalizer): the model
 *  hands the option list through OptionsRole. */
class EnumComboDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int OptionsRole = Qt::UserRole + 41;

    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H
