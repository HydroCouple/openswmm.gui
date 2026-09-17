/*!
 * \file   mesh2daquifermodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * GG2 / GG3 (2026-09-07) — MVC models for the two-zone groundwater kernel's
 * authoring rows: `[2D_AQUIFER]` and `[2D_AQUIFER_NODE]`.
 *
 * The engine is the single source of truth. `load()` reads the rows through
 * `swmm_gw2d_row_*` / `swmm_gw2d_node_*`, edits are staged here, and
 * `commit()` writes the difference back. Staging rather than writing through
 * matters for one reason worth stating: these rows seed the kernel at
 * initialize, so the C API refuses them mid-run. A model that wrote through
 * would fail silently on half a table; a model that stages can tell the user
 * up front that the run has to be reset.
 *
 * Values are in the PROJECT's units throughout — the same numbers the `.inp`
 * carries and the same numbers the API takes. The dialog labels the columns
 * from `UnitSystem`; nothing converts, so what the user types is what the
 * file holds. (Converting in the GUI is how a saved model stops matching
 * what was on screen.)
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui {

/*! \brief The `[2D_AQUIFER]` rows: scope, the five positional columns, and
 *         the law/closure selections. */
class Mesh2DAquiferModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColScope = 0,   //!< "*", "TAG <t>" or "CELL <n>"
        ColTarget,      //!< the tag name or the 1-based cell index
        ColKs,
        ColZs,
        ColThetaS,
        ColThetaR,
        ColAlpha,
        ColSoil,
        ColClosure,
        ColHg0,
        ColCLoss,
        ColCount
    };

    explicit Mesh2DAquiferModel(QObject *parent = nullptr);

    //! Soil-law tokens in the engine's enum order, so the row index IS the
    //! SWMM_GW2D_SOIL_* code. Do not reorder.
    [[nodiscard]] static QStringList soilTokens();
    //! Closure tokens. Index 0 is AUTO (code −1); the rest are 0..2 — see
    //! closureCodeAt().
    [[nodiscard]] static QStringList closureTokens();
    [[nodiscard]] static int closureCodeAt(int index);
    [[nodiscard]] static int closureIndexOf(int code);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;

    //! Replace the staged rows with the engine's.
    void load(SWMM_Engine engine);
    //! Apply the staged rows to the engine. Every row is rewritten (the C API
    //! has no "set row"), which is also what makes a reorder work.
    //! \returns an empty string on success, else the first engine complaint.
    QString commit(SWMM_Engine engine);
    [[nodiscard]] bool isDirty() const;

    //! Append a row seeded with the defaults the parser uses.
    int appendRow();

    //! Column labels want the project's length/rate words.
    void setUnitLabels(const QString &lengthLabel, const QString &rateLabel);

private:
    struct Row {
        int     scope   = 0;      // SWMM_GW2D_SCOPE_*
        QString tag;
        int     cell    = -1;     // 0-based
        double  ks      = 0.5;
        double  zs      = 5.0;
        double  thetaS  = 0.45;
        double  thetaR  = 0.10;
        double  alpha   = 2.0;
        int     soil    = 0;      // SWMM_GW2D_SOIL_*
        bool    soilSet = false;
        int     closure = -1;     // SWMM_GW2D_CLOSURE_*
        bool    closureSet = false;
        double  hg0     = -1.0;   // < 0 = "use the option default"
        double  cLoss   = 0.0;
        double  psiB    = 0.20;
        double  lambda  = 0.40;
        double  vgN     = 1.6;
        double  vgL     = 0.5;
        int     mLayers = -1;
        bool operator==(const Row &o) const;
    };

    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString    m_lengthLabel = QStringLiteral("m");
    QString    m_rateLabel   = QStringLiteral("mm/hr");
};

/*! \brief The `[2D_AQUIFER_NODE]` beds: which 1D node exchanges with which
 *         cell, through what conductance. */
class Mesh2DAquiferNodeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColNode = 0, ColCell, ColKc, ColDc, ColArea, ColCount };

    explicit Mesh2DAquiferNodeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;

    void load(SWMM_Engine engine);
    QString commit(SWMM_Engine engine);
    [[nodiscard]] bool isDirty() const;
    int appendRow(const QString &node, int cell);

    void setUnitLabels(const QString &lengthLabel, const QString &rateLabel,
                       const QString &areaLabel);

private:
    struct Row {
        QString node;
        int     cell = 0;    // 0-based
        double  kc   = 0.0;  // 0 = direct Darcy, no semi-confining bed
        double  dc   = 0.0;
        double  area = 0.0;  // 0 = the cell's own area
        bool operator==(const Row &o) const;
    };

    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString    m_lengthLabel = QStringLiteral("m");
    QString    m_rateLabel   = QStringLiteral("mm/hr");
    QString    m_areaLabel   = QStringLiteral("m²");
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H
