/*!
 * \file reactionsystemeditordialog.h
 * \brief Editor for the reaction system (.rxn / [REACTION_*]) — GUI plan
 *        §3.2, rounds G-B2/G-C1.
 *
 * \details Tabs: Options / Species / Coefficients / Terms / Expressions /
 *          Initial Quality / File (raw text) / Sources (disabled — the
 *          engine rejects [REACTION_SOURCES] until phase R-sources).
 *
 *          The engine handle IS the model (GUI plan §2): structured tabs
 *          apply as-you-go through the swmm_reaction_* CRUD (each mutation
 *          eagerly validated engine-side and rolled back on failure, so the
 *          model can never go uncompilable), and the File tab serializes
 *          from / applies to the same state — which is the whole sync
 *          mechanism between the views (CLAUDE.md §5.1). FILE persistence
 *          happens only on Save (swmm_reactions_save) to the bound
 *          [PROCESS_COMPONENTS] config, with a one-step "create component +
 *          config file" flow when none is bound (G-D1).
 *
 *          Dependency-light on purpose — Qt Widgets + the engine ABI — so
 *          tests construct and drive it (the WaterAgeSourcesDialog
 *          contract).
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H
#define OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the model's reaction system.
 */
class ReactionSystemEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReactionSystemEditorDialog(SWMM_Engine engine,
                                        QWidget *parent = nullptr);
    ~ReactionSystemEditorDialog() override = default;

    /*! \brief Successful engine writes performed so far (test-facing; the
     *         caller marks the project dirty when > 0 on close). */
    [[nodiscard]] int writeCount() const { return m_writes; }
    [[nodiscard]] bool wroteAnyChanges() const { return m_writes > 0; }

    /*! \brief Reload every tab from the engine (after external CRUD). */
    void reloadAll();

private slots:
    void onAddSpecies();
    void onRemoveSpecies();
    void onAddCoefficient();
    void onRemoveCoefficient();
    void onAddTerm();
    void onRemoveTerm();
    void onAddInitOverride();
    void onRemoveInitOverride();
    void onSave();
    void onTabChanged(int index);

private:
    void buildUi();
    QWidget *buildOptionsTab();
    QWidget *buildSpeciesTab();
    QWidget *buildCoefficientsTab();
    QWidget *buildTermsTab();
    QWidget *buildExpressionsTab();
    QWidget *buildInitialQualityTab();
    QWidget *buildFileTab();
    QWidget *buildSourcesPlaceholderTab();

    void loadOptions();
    void loadSpecies();
    void loadCoefficients();
    void loadTerms();
    void loadExpressions();
    void loadInitialQuality();
    void loadFileTab();
    bool applyFileTab();           ///< returns false when the text is bad

    void refreshBinding();         ///< title bar + Save enablement (G-D1)
    void setStatus(const QString &msg, bool error);
    void bumpWrites() { ++m_writes; }

    QStringList speciesNames() const;

    SWMM_Engine     m_engine        = nullptr;
    QTabWidget     *m_tabs          = nullptr;
    QLabel         *m_statusLabel   = nullptr;
    QPushButton    *m_saveBtn       = nullptr;

    // Options
    QComboBox      *m_solverCombo   = nullptr;
    QComboBox      *m_couplingCombo = nullptr;
    QComboBox      *m_rateUnitsCombo = nullptr;
    QComboBox      *m_areaUnitsCombo = nullptr;
    QDoubleSpinBox *m_timestepSpin  = nullptr;
    QDoubleSpinBox *m_atolSpin      = nullptr;
    QDoubleSpinBox *m_rtolSpin      = nullptr;

    // Species
    QTableWidget   *m_speciesTable  = nullptr;
    QLineEdit      *m_newSpeciesName  = nullptr;
    QComboBox      *m_newSpeciesKind  = nullptr;
    QLineEdit      *m_newSpeciesUnits = nullptr;

    // Coefficients
    QTableWidget   *m_coeffTable    = nullptr;
    QLineEdit      *m_newCoeffName  = nullptr;
    QComboBox      *m_newCoeffKind  = nullptr;
    QDoubleSpinBox *m_newCoeffValue = nullptr;

    // Terms
    QTableWidget   *m_termTable     = nullptr;
    QLineEdit      *m_newTermName   = nullptr;

    // Expressions
    QTableWidget   *m_exprTable     = nullptr;

    // Initial quality
    QTableWidget   *m_initGlobalTable   = nullptr;
    QTableWidget   *m_initOverrideTable = nullptr;

    // File tab
    QPlainTextEdit *m_fileEdit      = nullptr;
    QLabel         *m_fileStatus    = nullptr;
    QString         m_fileBaseline;      ///< last serialize (dirty check)
    int             m_fileTabIndex  = -1;
    int             m_lastTabIndex  = 0;

    bool            m_loading       = false;  ///< suppress apply during load
    bool            m_gatingBack    = false;  ///< bouncing to the File tab
    int             m_writes        = 0;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H
