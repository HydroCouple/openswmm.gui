/**
 * @file      legendpropertiesdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Modeless Legend Properties dialog: the map legend's items, layout, fonts, symbols,
 *            color ramps, spacing and box, on tabs.
 *
 *         The dialog does not own the legend style; it edits the style that the legend on the
 *         map, the Properties panel and the Legend dock all follow. Edits apply at once and are
 *         undo steps on the map's undo stack (Ctrl+Z), so Close keeps them. Each control's
 *         object name is the style property it edits.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QPointer>

#include <functional>

class MapCanvas;
class OpenSWMMVisLayer;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QTreeView;

namespace OpenSWMM::Render
{
class LegendOverlayStyle;
struct LegendNode;
} // namespace OpenSWMM::Render

namespace openswmmvis::map::LegendContent
{
struct LegendTreeItem;
}

namespace openswmmvis::ui
{

class ColorButton;
class LegendItemTreeModel;

/*!
 * \class LegendPropertiesDialog
 * \brief Tabs: Items, Layout, Fonts, Symbols, Spacing, Box; Presets, Reset and Close.
 */
class LegendPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build the dialog for a shared legend style.
     * @param[in,out] style Borrowed legend style; edits write to it at once. The dialog closes
     *   when the style is destroyed.
     * @param[in] parent Optional parent widget.
     * @param[in] canvas Map the legend belongs to; the Items tab needs it (without it the tab
     *   says so).
     */
    explicit LegendPropertiesDialog(OpenSWMM::Render::LegendOverlayStyle *style,
                                    QWidget *parent = nullptr, MapCanvas *canvas = nullptr);
    /** @brief Destroy the dialog; the style is left as edited. */
    ~LegendPropertiesDialog() override;

    /** @brief The Items tab's model, or null without a map. */
    [[nodiscard]] LegendItemTreeModel *itemsModel() const { return m_items; }

    /** @brief Save the legend's look (see LegendOverlayStyle::presetJson()) to a style file.
     * @param path File path (.oswlegend). @param[out] error Reason on failure; may be null.
     * @return Whether the file was written. */
    bool savePreset(const QString &path, QString *error = nullptr) const;
    /** @brief Apply a look from a style file saved by savePreset().
     * @param path File path. @param[out] error Reason on failure; may be null.
     * @return Whether the file was a legend style and was applied. */
    bool loadPreset(const QString &path, QString *error = nullptr);

private:
    QWidget *buildItemsTab();
    QWidget *buildLayoutTab();
    QWidget *buildFontsTab();
    QWidget *buildSymbolsTab();
    QWidget *buildSpacingTab();
    QWidget *buildBoxTab();

    // Controls bound to a style property (object name = property name).
    QCheckBox *check(const char *property, const QString &text);
    QSpinBox *spin(const char *property, int min, int max, const QString &special = {});
    QDoubleSpinBox *doubleSpin(const char *property, double min, double max, double step,
                               int decimals, const QString &special = {});
    QComboBox *combo(const char *property, const QList<QPair<QString, int>> &choices);
    ColorButton *color(const char *property);
    QLineEdit *text(const char *property);
    void fontRow(QGridLayout *grid, int row, const QString &label, const char *fontProperty,
                 const char *colorProperty, const char *alignmentProperty);

    void scheduleRefresh();
    void refresh();
    void selectKey(const QString &key);
    void updateItemControls();
    /** The selected item's map layer (a layer or one of its rows), or null. */
    OpenSWMMVisLayer *selectedLayer() const;
    QWidget *buildNodeNumbersPage();
    QWidget *buildNodeColorBarPage();
    QWidget *buildNodeTextPage();
    QWidget *buildNodeCountsPage();
    /** One row of the Text page: the heading's or the rows' own font and color. */
    void nodeTextRow(QGridLayout *grid, int row, const QString &label, bool heading);
    /** Change the selected item's legend settings (one undo step). */
    void editSelected(const std::function<void(OpenSWMM::Render::LegendNode &)> &edit);
    /** Write the Numbers controls to the selected layer, as one undo step. */
    void applyNumberFormat();

    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    QPointer<MapCanvas> m_canvas;
    QTabWidget *m_tabs = nullptr;
    QList<std::function<void()>> m_refreshers; ///< style → controls
    bool m_refreshPending = false;

    // Items tab.
    LegendItemTreeModel *m_items = nullptr;
    QTreeView *m_itemsView = nullptr;
    QString m_selectedKey;
    QGroupBox *m_nodeBox = nullptr;
    QLineEdit *m_nodeLabel = nullptr;
    QComboBox *m_nodeHeading = nullptr;
    QCheckBox *m_nodeColumnBreak = nullptr;
    QSpinBox *m_nodeItemColumns = nullptr;
    QComboBox *m_nodeRamp = nullptr;
    QSpinBox *m_nodeSymbolWidth = nullptr;
    QSpinBox *m_nodeSymbolHeight = nullptr;
    QTabWidget *m_nodeTabs = nullptr;
    // Numbers (the layer's own class label format).
    QLabel *m_numbersNote = nullptr;
    QComboBox *m_numberMode = nullptr;
    QSpinBox *m_numberDigits = nullptr;
    QCheckBox *m_numberTrim = nullptr;
    QCheckBox *m_numberThousands = nullptr;
    QLineEdit *m_numberPrefix = nullptr;
    QLineEdit *m_numberSuffix = nullptr;
    QLineEdit *m_numberSeparator = nullptr;
    QList<QWidget *> m_numberControls;
    // Color bar, Text and Counts pages; their controls follow the selected item.
    QWidget *m_rampPage = nullptr;
    QWidget *m_countsPage = nullptr;
    QList<std::function<void(const openswmmvis::map::LegendContent::LegendTreeItem *)>>
        m_nodeRefreshers;
    QList<QWidget *> m_structureButtons; ///< enabled only when the order is the user's
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H
