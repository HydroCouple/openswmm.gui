/**
 * @file      frozengroundrowmodel.h
 * @brief     Staged table of the frozen-ground rows of one section: [FROZEN_GROUND]
 *            (subcatchments) or [2D_FROZEN_GROUND] (mesh tags).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDROWMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDROWMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QStringList>

#include <array>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

/*! The engine holds the rows; this model stages edits and writes them back in one commit that
 *  is read back and, on failure, undone. Every view of a section shares one instance. */
class FrozenGroundRowModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /** Which section the model edits; the values are the engine's `target` codes. */
    enum class Target {
        Subcatchments = 0, //!< [FROZEN_GROUND]: rows named after subcatchments, or `*`
        Mesh = 1           //!< [2D_FROZEN_GROUND]: rows named after mesh tags, or `*`
    };
    /** QStringList of the names a combo editor offers for the name column. */
    static constexpr int ChoicesRole = Qt::UserRole + 1;
    /** Number of value columns after the name column. */
    static constexpr int ValueCount = 10;

    /** One row: its name and the ten SI values in the engine's column order. */
    struct Row {
        QString name;
        std::array<double, ValueCount> values{};
        bool operator==(const Row &o) const { return name == o.name && values == o.values; }
    };

    /**
     * @param[in] target Section this model edits; fixed for the model's lifetime.
     * @param[in] parent Optional QObject owner.
     */
    explicit FrozenGroundRowModel(Target target, QObject *parent = nullptr);

    [[nodiscard]] Target target() const { return m_target; }
    /** The section's bracketed name, for example "[FROZEN_GROUND]". */
    [[nodiscard]] QString sectionName() const;
    /** The engine's default values for a new row. */
    [[nodiscard]] static std::array<double, ValueCount> defaultValues();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Expose staged values. Display/Edit give text (numbers in their shortest exact
     * form), ChoicesRole the names the name column may take, ToolTip the column help.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Stage one edit. Numbers parse from C-locale text; a refused edit returns false and
     * changes nothing. No engine write.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Replace staged rows and baseline with the engine's and refresh the name choices.
     * @param[in] engine Borrowed engine; null clears the model.
     * @return Empty on success, otherwise the read error.
     */
    QString load(SWMM_Engine engine);
    /** True when the staged rows differ from the loaded baseline. */
    [[nodiscard]] bool isDirty() const { return m_rows != m_loaded; }
    /**
     * @brief Check that the engine is editable, still holds the loaded rows, and every staged
     * row has a known unique name and in-range values.
     * @return Empty when a commit may proceed, otherwise the first problem.
     */
    QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Rewrite the section from the staged rows and verify the read-back.
     * @param[in,out] engine Borrowed BUILDING/OPENED engine.
     * @param[in] advanceBaseline True adopts the staged rows as the baseline on success.
     * @return Empty for a clean model or success; otherwise the error, after an attempt to put
     * the loaded rows back.
     */
    QString commit(SWMM_Engine engine, bool advanceBaseline = true);
    /** Put the loaded rows back when the engine no longer holds them. Empty on success. */
    QString restoreLoaded(SWMM_Engine engine) const;
    /** Adopt the staged rows as the clean baseline. */
    void acceptChanges() { m_loaded = m_rows; }
    /**
     * @brief Append a row with the default values: `*` when the section has no default row
     * yet, otherwise the first name that has no row.
     * @return The new row index, or -1 when every name already has a row.
     */
    int appendRow();

    [[nodiscard]] const QList<Row> &rows() const { return m_rows; }
    [[nodiscard]] const QList<Row> &loadedRows() const { return m_loaded; }
    /** Names a row may take besides `*` (subcatchments or mesh tags) after load(). */
    [[nodiscard]] QStringList names() const { return m_names; }

private:
    /** Problem with one row's values, or empty. */
    static QString rowProblem(const Row &row);
    static QString read(SWMM_Engine engine, Target target, QList<Row> &rows);
    static QString write(SWMM_Engine engine, Target target, const QList<Row> &rows);

    Target m_target;
    QList<Row> m_rows, m_loaded;
    QStringList m_names;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDROWMODEL_H
