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
class OpenSWMMVisLayer;

namespace openswmmvis::ui {

class SymbologyDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SymbologyDialog(OpenSWMMVisLayer *layer, QWidget *parent = nullptr);
    ~SymbologyDialog() override;

private slots:
    void onColorClicked();
    void onApplyClicked();
    void accept() override;

private:
    void buildUi();
    void buildSingleTab(QTabWidget *tabs);
    void buildGraduatedTab(QTabWidget *tabs);
    void buildCategorizedTab(QTabWidget *tabs);
    void buildLabelsTab(QTabWidget *tabs);
    void buildArrowsTab(QTabWidget *tabs);

    void applyToLayer();

    QPointer<OpenSWMMVisLayer> m_layer;
    QTabWidget                *m_tabs = nullptr;

    // Single
    QPushButton *m_singleColorBtn = nullptr;
    QDoubleSpinBox *m_singleSize  = nullptr;
    QDoubleSpinBox *m_singleWidth = nullptr;
    QColor       m_singleColor;

    // Graduated
    QComboBox      *m_gradAttr  = nullptr;
    QComboBox      *m_gradRamp  = nullptr;
    QSpinBox       *m_gradClasses = nullptr;
    QDoubleSpinBox *m_gradMinSize = nullptr;
    QDoubleSpinBox *m_gradMaxSize = nullptr;

    // Categorized
    QComboBox      *m_catAttr = nullptr;
    QComboBox      *m_catScheme = nullptr;

    // Labels
    QCheckBox      *m_labelEnabled = nullptr;
    QLineEdit      *m_labelExpr   = nullptr;
    QDoubleSpinBox *m_labelSize   = nullptr;
    QCheckBox      *m_labelHalo   = nullptr;

    // Arrows
    QCheckBox      *m_arrowEnabled = nullptr;
    QDoubleSpinBox *m_arrowSize    = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYDIALOG_H
