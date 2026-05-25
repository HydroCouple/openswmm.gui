/*!
 * \file   symbologydialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BI — Map symbology editor dialog.
 *
 * Per-layer symbology editor. Tabs:
 *   - Symbol      — SingleSymbolRenderer  (one stroke / fill / size for the whole layer)
 *   - Graduated   — GraduatedRenderer     (continuous attribute → ramp)
 *   - Categorized — CategorizedRenderer   (discrete attribute → colour map)
 *   - Rule-based  — RuleBasedRenderer     (if/then ladder)
 *   - Labels      — text expression + font + halo
 *   - Arrows      — direction arrows for links (size by flow magnitude)
 *
 * The full IFeatureRenderer interfaces ship in include/render/renderers/*.
 * This dialog is the editor surface that drives them.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYDIALOG_H

#include <QDialog>
#include <QPointer>

class QTabWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class ColorRampComboBox;   // Slice BB-β — gradient-swatch dropdown
class OpenSWMMVisLayer;

namespace openswmmvis::ui {

class SymbologyDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SymbologyDialog(OpenSWMMVisLayer *layer, QWidget *parent = nullptr);

    /*!
     * \brief Slice BI-MK.1.41 / BI-MK.LT — overload that pre-scopes the
     *        dialog to a specific kind on construction (for the layer-tree
     *        kind-row right-click → Style menu).
     * \param layer        Must be a SWMMModelLayer; ignored otherwise.
     * \param kindOrdinal  SWMMModelLayer::Category ordinal (0..10).
     * \param rendererId   "single" / "graduated" / "categorized" / ""
     *                     (empty → use the kind's current renderer class).
     */
    explicit SymbologyDialog(OpenSWMMVisLayer *layer,
                             int kindOrdinal,
                             const QString &rendererId,
                             QWidget *parent = nullptr);

    ~SymbologyDialog() override;

private slots:
    void onColorClicked();
    void onApplyClicked();
    void onApplyToAllKindsClicked();        // Slice BI-MK.1.42
    void onKindChanged(int row);            // Slice BI-MK.1.41
    void onCategorizedClassify();           // Slice BI.3-α
    void onCategorizedAddRow();             // Slice BI.3-α
    void onCategorizedRemoveRow();          // Slice BI.3-α
    void onCategorizedColorClicked();       // Slice BI.3-α — per-row color cell
    void accept() override;

private:
    void buildUi();
    void buildKindPicker();            // Slice BI-MK.1.41 — left-pane list
    void buildSingleTab(QTabWidget *tabs);
    void buildGraduatedTab(QTabWidget *tabs);
    void buildCategorizedTab(QTabWidget *tabs);
    void buildLabelsTab(QTabWidget *tabs);
    void buildArrowsTab(QTabWidget *tabs);

    void applyToLayer();
    void applyCurrentTabToKind(int kindOrdinal);  // Slice BI-MK.1.42

    /*!
     * \brief Populate the form fields from the layer's (or the active
     *        kind's, for multi-kind layers) current IFeatureRenderer
     *        state and select the tab matching its rendererId().
     *        Called from the ctor, the kind-picker selection slot, and
     *        whenever the per-kind renderer changes externally.
     */
    void readFromLayer();

    /*! True when this dialog is showing a SWMMModelLayer and the
     *  left-pane kind picker is visible. */
    [[nodiscard]] bool isKindScoped() const;

    /*!
     * \brief Slice CTX.1 — repopulate m_gradAttr + m_catAttr + m_singleSizeAttr
     *        based on what the layer + kind actually carries.
     *
     *        SWMMModelLayer: per-kind static numeric Q_PROPERTY-like
     *        candidates for Graduated/Single-size; per-kind string/enum
     *        candidates for Categorized.
     *
     *        SWMMResultsLayer: SWMMResultVariable enumerators in the
     *        scope (Nodes / Links / Subcatchments) of the kind ordinal;
     *        Categorized always empty (output is numeric).
     *
     *        Called from buildGraduatedTab/buildCategorizedTab (initial),
     *        the ctor after readFromLayer, and onKindChanged.
     */
    void populateAttributeCombos();

    QPointer<OpenSWMMVisLayer> m_layer;
    QListWidget               *m_kindList = nullptr;   // Slice BI-MK.1.41
    QPushButton               *m_applyAllBtn = nullptr;// Slice BI-MK.1.42
    int                        m_activeKind = -1;      // -1 = layer-scope
    QTabWidget                *m_tabs = nullptr;

    // Single
    QPushButton *m_singleColorBtn = nullptr;
    QDoubleSpinBox *m_singleSize  = nullptr;
    QDoubleSpinBox *m_singleWidth = nullptr;
    QColor       m_singleColor;
    // Slice BI Phase 8.13.43-α — Single tab data-defined size controls.
    QCheckBox      *m_singleSizeByAttr   = nullptr;
    QComboBox      *m_singleSizeAttr     = nullptr;
    QDoubleSpinBox *m_singleSizeValueMin = nullptr;
    QDoubleSpinBox *m_singleSizeValueMax = nullptr;
    QDoubleSpinBox *m_singleSizeOutMin   = nullptr;
    QDoubleSpinBox *m_singleSizeOutMax   = nullptr;
    QComboBox      *m_singleSizeCurve    = nullptr;

    // Graduated
    QComboBox          *m_gradAttr    = nullptr;
    ColorRampComboBox  *m_gradRamp    = nullptr;   // Slice BB-β — gradient-swatch dropdown
    QSpinBox           *m_gradClasses = nullptr;
    QDoubleSpinBox *m_gradMinSize = nullptr;
    QDoubleSpinBox *m_gradMaxSize = nullptr;
    // Slice BI Phase 8.13.43-α — Graduated tab output-axis selectors.
    QCheckBox      *m_gradOutputColor = nullptr;
    QCheckBox      *m_gradOutputSize  = nullptr;

    // Categorized — Slice BI.3-α per-value editor
    QComboBox      *m_catAttr        = nullptr;
    QComboBox      *m_catScheme      = nullptr;
    QTableWidget   *m_catTable       = nullptr;
    QPushButton    *m_catClassifyBtn = nullptr;
    QPushButton    *m_catAddBtn      = nullptr;
    QPushButton    *m_catRemoveBtn   = nullptr;

    // Labels
    QCheckBox      *m_labelEnabled = nullptr;
    QLineEdit      *m_labelExpr   = nullptr;
    QDoubleSpinBox *m_labelSize   = nullptr;
    QCheckBox      *m_labelHalo   = nullptr;

    // Arrows
    QCheckBox      *m_arrowEnabled        = nullptr;
    QDoubleSpinBox *m_arrowSize           = nullptr;
    QPushButton    *m_arrowColorBtn       = nullptr;  // Slice FX.1
    QColor          m_arrowColor          = QColor(34, 34, 34);
    QCheckBox      *m_arrowOnlyFlowPos    = nullptr;  // Slice FX.1 — opt-in flow-positive filter
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYDIALOG_H
