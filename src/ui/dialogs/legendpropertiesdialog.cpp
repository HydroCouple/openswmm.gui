/**
 * @file      legendpropertiesdialog.cpp
 * @brief     Modeless Legend Properties dialog: the map legend's items, layout, fonts, symbols,
 *            color ramps, spacing and box, on tabs.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/legendpropertiesdialog.h"

#include "layers/openswmmvislayer.h"
#include "map/legendclasseditcommands.h"
#include "map/legendcontent.h"
#include "map/legendstyleundorecorder.h"
#include "map/mapcanvas.h"
#include "render/legenditemtree.h"
#include "render/legendoverlaystyle.h"
#include "ui/models/legenditemtreemodel.h"
#include "ui/widgets/colorbutton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>

namespace openswmmvis::ui
{

using OpenSWMM::Render::LegendNode;
using OpenSWMM::Render::LegendOverlayStyle;
using Style = LegendOverlayStyle;

namespace
{

/** A tab page: @p content in a scroll area, so tall tabs fit small screens. */
QWidget *scrolled(QWidget *content)
{
    auto *area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    return area;
}

QPushButton *button(const QString &text, const char *name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(name));
    b->setAutoDefault(false); // Enter in a field must not press it
    return b;
}

QList<QPair<QString, int>> alignmentChoices()
{
    return {{QObject::tr("Left"), int(Style::TextAlignment::Left)},
            {QObject::tr("Center"), int(Style::TextAlignment::Center)},
            {QObject::tr("Right"), int(Style::TextAlignment::Right)}};
}

} // namespace

LegendPropertiesDialog::LegendPropertiesDialog(LegendOverlayStyle *style, QWidget *parent,
                                               MapCanvas *canvas)
    : QDialog(parent), m_style(style), m_canvas(canvas)
{
    setWindowTitle(tr("Legend Properties"));
    // Naming wires the app-wide dialog layout persistence.
    setObjectName(QStringLiteral("LegendPropertiesDialog"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(660, 680);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("legendTabs"));
    if (m_style) {
        m_tabs->addTab(buildItemsTab(), tr("Items"));
        m_tabs->addTab(scrolled(buildLayoutTab()), tr("Layout"));
        m_tabs->addTab(scrolled(buildFontsTab()), tr("Fonts"));
        m_tabs->addTab(scrolled(buildSymbolsTab()), tr("Symbols"));
        m_tabs->addTab(scrolled(buildSpacingTab()), tr("Spacing"));
        m_tabs->addTab(scrolled(buildBoxTab()), tr("Box"));
    }
    root->addWidget(m_tabs, 1);

    // ── Footer: Presets ▾, Reset, Close ─────────────────────────────────
    auto *footer = new QHBoxLayout;
    auto *presets = new QToolButton(this);
    presets->setObjectName(QStringLiteral("presets"));
    presets->setText(tr("Presets"));
    presets->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(presets);
    const QString filter = tr("Legend style (*.oswlegend)");
    connect(menu->addAction(tr("Save style…")), &QAction::triggered, this, [this, filter] {
        const QString path =
            QFileDialog::getSaveFileName(this, tr("Save Legend Style"), {}, filter);
        QString error;
        if (!path.isEmpty() && !savePreset(path, &error))
            QMessageBox::warning(this, tr("Save Legend Style"), error);
    });
    connect(menu->addAction(tr("Load style…")), &QAction::triggered, this, [this, filter] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Load Legend Style"), {}, filter);
        QString error;
        if (!path.isEmpty() && !loadPreset(path, &error))
            QMessageBox::warning(this, tr("Load Legend Style"), error);
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Use as default for new legends")), &QAction::triggered, this,
            [this] {
                if (m_style)
                    Style::setDefaultPreset(m_style->presetJson());
            });
    connect(menu->addAction(tr("Restore built-in default for new legends")), &QAction::triggered,
            this, [] { Style::setDefaultPreset({}); });
    presets->setMenu(menu);
    footer->addWidget(presets);
    footer->addStretch(1);
    auto *reset = button(tr("Reset"), "reset", this);
    reset->setToolTip(tr("Restore every setting to its built-in default (Ctrl+Z undoes it)"));
    connect(reset, &QPushButton::clicked, this, [this] {
        if (m_style)
            m_style->resetToDefaults();
    });
    footer->addWidget(reset);
    auto *close = button(tr("Close"), "close", this);
    close->setToolTip(tr("Edits are already applied; Ctrl+Z on the map undoes them"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    root->addLayout(footer);

    if (m_style) {
        connect(m_style.data(), &QObject::destroyed, this, &QDialog::close);
        // Edits made elsewhere (the Properties panel, dragging the legend) show here too.
        connect(m_style.data(), &LegendOverlayStyle::changed, this,
                &LegendPropertiesDialog::scheduleRefresh);
    }
    refresh();
}

LegendPropertiesDialog::~LegendPropertiesDialog() = default;

// ── Bound controls ──────────────────────────────────────────────────────────

QCheckBox *LegendPropertiesDialog::check(const char *property, const QString &label)
{
    auto *box = new QCheckBox(label);
    box->setObjectName(QString::fromLatin1(property));
    connect(box, &QCheckBox::toggled, this, [this, property](bool on) {
        if (m_style)
            m_style->setProperty(property, on);
    });
    m_refreshers.append([this, box, property] {
        const QSignalBlocker block(box);
        box->setChecked(m_style->property(property).toBool());
    });
    return box;
}

QSpinBox *LegendPropertiesDialog::spin(const char *property, int min, int max,
                                       const QString &special)
{
    auto *box = new QSpinBox;
    box->setObjectName(QString::fromLatin1(property));
    box->setRange(min, max);
    if (!special.isEmpty())
        box->setSpecialValueText(special);
    connect(box, &QSpinBox::valueChanged, this, [this, property](int v) {
        if (m_style)
            m_style->setProperty(property, v);
    });
    m_refreshers.append([this, box, property] {
        const QSignalBlocker block(box);
        box->setValue(m_style->property(property).toInt());
    });
    return box;
}

QDoubleSpinBox *LegendPropertiesDialog::doubleSpin(const char *property, double min, double max,
                                                   double step, int decimals,
                                                   const QString &special)
{
    auto *box = new QDoubleSpinBox;
    box->setObjectName(QString::fromLatin1(property));
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    if (!special.isEmpty())
        box->setSpecialValueText(special);
    connect(box, &QDoubleSpinBox::valueChanged, this, [this, property](double v) {
        if (m_style)
            m_style->setProperty(property, v);
    });
    m_refreshers.append([this, box, property] {
        const QSignalBlocker block(box);
        box->setValue(m_style->property(property).toDouble());
    });
    return box;
}

QComboBox *LegendPropertiesDialog::combo(const char *property,
                                         const QList<QPair<QString, int>> &choices)
{
    auto *box = new QComboBox;
    box->setObjectName(QString::fromLatin1(property));
    for (const auto &[label, value] : choices)
        box->addItem(label, value);
    connect(box, &QComboBox::currentIndexChanged, this, [this, box, property](int) {
        if (m_style)
            m_style->setProperty(property, box->currentData().toInt());
    });
    m_refreshers.append([this, box, property] {
        const QSignalBlocker block(box);
        box->setCurrentIndex(box->findData(m_style->property(property).toInt()));
    });
    return box;
}

ColorButton *LegendPropertiesDialog::color(const char *property)
{
    auto *b = new ColorButton;
    b->setObjectName(QString::fromLatin1(property));
    b->setAutoDefault(false);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // a swatch, not a bar
    connect(b, &ColorButton::colorChanged, this, [this, property](const QColor &c) {
        if (m_style)
            m_style->setProperty(property, c);
    });
    m_refreshers.append([this, b, property] {
        const QSignalBlocker block(b);
        b->setColor(m_style->property(property).value<QColor>());
    });
    return b;
}

QLineEdit *LegendPropertiesDialog::text(const char *property)
{
    auto *edit = new QLineEdit;
    edit->setObjectName(QString::fromLatin1(property));
    connect(edit, &QLineEdit::textEdited, this, [this, property](const QString &t) {
        if (m_style)
            m_style->setProperty(property, t);
    });
    m_refreshers.append([this, edit, property] {
        const QString value = m_style->property(property).toString();
        if (edit->text() != value) {
            const QSignalBlocker block(edit);
            edit->setText(value);
        }
    });
    return edit;
}

void LegendPropertiesDialog::fontRow(QGridLayout *grid, int row, const QString &label,
                                     const char *fontProperty, const char *colorProperty,
                                     const char *alignmentProperty)
{
    const QString base = QString::fromLatin1(fontProperty);
    auto *family = new QFontComboBox;
    family->setObjectName(base + QStringLiteral(".family"));
    family->setEditable(false); // so the placeholder shows for the system font
    family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    family->setMinimumContentsLength(10);
    // The application font (the default) is often not in the list.
    family->setPlaceholderText(tr("System font"));
    auto *size = new QDoubleSpinBox;
    size->setObjectName(base + QStringLiteral(".size"));
    size->setRange(4, 96);
    size->setDecimals(1);
    size->setSuffix(tr(" pt"));
    auto *bold = new QCheckBox(tr("Bold"));
    bold->setObjectName(base + QStringLiteral(".bold"));
    auto *italic = new QCheckBox(tr("Italic"));
    italic->setObjectName(base + QStringLiteral(".italic"));
    // Each part edits the stored font, keeping the other parts.
    const auto editFont = [this, fontProperty](const std::function<void(QFont &)> &edit) {
        if (!m_style)
            return;
        QFont f = m_style->property(fontProperty).value<QFont>();
        edit(f);
        m_style->setProperty(fontProperty, f);
    };
    connect(family, &QFontComboBox::currentFontChanged, this, [editFont](const QFont &picked) {
        editFont([&picked](QFont &f) { f.setFamily(picked.family()); });
    });
    connect(size, &QDoubleSpinBox::valueChanged, this,
            [editFont](double pt) { editFont([pt](QFont &f) { f.setPointSizeF(pt); }); });
    connect(bold, &QCheckBox::toggled, this,
            [editFont](bool on) { editFont([on](QFont &f) { f.setBold(on); }); });
    connect(italic, &QCheckBox::toggled, this,
            [editFont](bool on) { editFont([on](QFont &f) { f.setItalic(on); }); });
    m_refreshers.append([this, family, size, bold, italic, fontProperty] {
        const QFont f = m_style->property(fontProperty).value<QFont>();
        const QSignalBlocker b1(family), b2(size), b3(bold), b4(italic);
        const QString name = QFontInfo(f).family();
        const int at = family->findText(name, Qt::MatchFixedString);
        if (at >= 0 && !f.family().isEmpty())
            family->setCurrentIndex(at);
        else
            family->setCurrentIndex(-1); // shows "System font"
        size->setValue(f.pointSizeF() > 0 ? f.pointSizeF() : QFont().pointSizeF());
        bold->setChecked(f.bold());
        italic->setChecked(f.italic());
    });
    grid->addWidget(new QLabel(label), row, 0);
    grid->addWidget(family, row, 1);
    grid->addWidget(size, row, 2);
    grid->addWidget(bold, row, 3);
    grid->addWidget(italic, row, 4);
    grid->addWidget(color(colorProperty), row, 5);
    grid->addWidget(combo(alignmentProperty, alignmentChoices()), row, 6);
}

void LegendPropertiesDialog::scheduleRefresh()
{
    // Queued: the change may come from an edit this dialog is still committing.
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, &LegendPropertiesDialog::refresh);
}

void LegendPropertiesDialog::refresh()
{
    m_refreshPending = false;
    if (!m_style)
        return;
    for (const auto &r : std::as_const(m_refreshers))
        r();
    updateItemControls();
}

// ── Items ───────────────────────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildItemsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    if (!m_canvas) {
        layout->addWidget(new QLabel(tr("Open Legend Properties from the legend on the map to "
                                        "choose its items.")));
        layout->addStretch(1);
        return page;
    }

    auto *follow = new QCheckBox(tr("Follow the map (layers in map order; new layers added)"));
    follow->setObjectName(QStringLiteral("autoUpdate"));
    follow->setToolTip(tr("Turn off to choose the order, add groups and text lines, and keep new "
                          "layers out. Turning it on again keeps each item's settings but drops "
                          "the order, groups and text (Ctrl+Z undoes it)."));
    connect(follow, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_style)
            return;
        if (on) {
            m_style->setAutoUpdate(true);
        } else {
            // Keep what the legend shows now, in its current order.
            const auto tree = openswmmvis::map::LegendContent::currentTree(m_canvas, m_style);
            m_style->setAutoUpdate(false);
            m_style->setItemTree(tree);
        }
    });
    m_refreshers.append([this, follow] {
        const QSignalBlocker block(follow);
        follow->setChecked(m_style->autoUpdate());
    });
    layout->addWidget(follow);

    m_items = new LegendItemTreeModel(m_canvas, m_style, this);
    m_itemsView = new QTreeView(page);
    m_itemsView->setObjectName(QStringLiteral("legendItemsTree"));
    m_itemsView->setModel(m_items);
    m_itemsView->setHeaderHidden(true);
    m_itemsView->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed);
    m_itemsView->expandAll();
    layout->addWidget(m_itemsView, 1);
    connect(m_itemsView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
                m_selectedKey = m_items->keyOf(current);
                updateItemControls();
            });
    connect(m_items, &LegendItemTreeModel::rebuilt, this, [this] {
        m_itemsView->expandAll();
        selectKey(m_selectedKey);
        updateItemControls();
    });

    // Structure: order, groups, text lines, layers.
    auto *buttons = new QHBoxLayout;
    const auto structure = [&](const QString &label, const char *name, const QString &tip,
                               const std::function<void()> &action) {
        auto *b = button(label, name, page);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, action);
        buttons->addWidget(b);
        m_structureButtons.append(b);
        return b;
    };
    const auto current = [this] { return m_items->indexOfKey(m_selectedKey); };
    structure(tr("Up"), "itemUp", tr("Move the item up"),
              [this, current] { m_items->moveBy(current(), -1); });
    structure(tr("Down"), "itemDown", tr("Move the item down"),
              [this, current] { m_items->moveBy(current(), 1); });
    structure(tr("Into group"), "itemIntoGroup", tr("Move the item into the group above it"),
              [this, current] { m_items->moveIntoGroup(current()); });
    structure(tr("Out of group"), "itemOutOfGroup", tr("Move the item out of its group"),
              [this, current] { m_items->moveOutOfGroup(current()); });
    structure(tr("Add group"), "addGroup", tr("Add a group after the selected item"),
              [this, current] {
                  m_selectedKey = m_items->addGroup(current(), tr("Group"));
                  QTimer::singleShot(
                      0, this, [this] { m_itemsView->edit(m_items->indexOfKey(m_selectedKey)); });
              });
    structure(tr("Add text"), "addText", tr("Add a line of text after the selected item"),
              [this, current] {
                  m_selectedKey = m_items->addText(current(), tr("Text"));
                  QTimer::singleShot(
                      0, this, [this] { m_itemsView->edit(m_items->indexOfKey(m_selectedKey)); });
              });
    QPushButton *addLayers = structure(tr("Add layers…"), "addLayers",
                                       tr("Add map layers the legend does not list"), [] {});
    connect(addLayers, &QPushButton::clicked, this, [this, addLayers] {
        const auto missing = m_items->missingLayers();
        QMenu menu(this);
        if (missing.isEmpty())
            menu.addAction(tr("Every map layer is listed"))->setEnabled(false);
        for (OpenSWMMVisLayer *layer : missing)
            connect(menu.addAction(layer->name()), &QAction::triggered, this,
                    [this, layer] { m_items->addLayers({layer}); });
        if (missing.size() > 1) {
            menu.addSeparator();
            connect(menu.addAction(tr("All of them")), &QAction::triggered, this,
                    [this, missing] { m_items->addLayers(missing); });
        }
        menu.exec(addLayers->mapToGlobal(QPoint(0, addLayers->height())));
    });
    structure(tr("Remove"), "removeItem", tr("Remove the layer, group or text from the legend"),
              [this, current] { m_items->removeItem(current()); });
    buttons->addStretch(1);
    layout->addLayout(buttons);

    // Settings of the selected item, in tabs.
    m_nodeBox = new QGroupBox(tr("Selected item"), page);
    auto *boxLayout = new QVBoxLayout(m_nodeBox);
    m_nodeTabs = new QTabWidget(m_nodeBox);
    m_nodeTabs->setObjectName(QStringLiteral("nodeTabs"));
    boxLayout->addWidget(m_nodeTabs);
    auto *general = new QWidget;
    auto *form = new QFormLayout(general);
    const auto editNode = [this](const std::function<void(LegendNode &)> &edit) {
        m_items->editSettings(m_items->indexOfKey(m_selectedKey), edit);
    };
    m_nodeLabel = new QLineEdit;
    m_nodeLabel->setObjectName(QStringLiteral("nodeLabel"));
    m_nodeLabel->setPlaceholderText(tr("Original name"));
    connect(m_nodeLabel, &QLineEdit::editingFinished, this, [this, editNode] {
        const QString t = m_nodeLabel->text();
        editNode([t](LegendNode &n) { n.userLabel = t; });
    });
    form->addRow(tr("Name in legend:"), m_nodeLabel);
    m_nodeHeading = new QComboBox;
    m_nodeHeading->setObjectName(QStringLiteral("nodeHeading"));
    m_nodeHeading->addItem(tr("Group heading"), int(LegendNode::HeadingStyle::Group));
    m_nodeHeading->addItem(tr("Sub-heading"), int(LegendNode::HeadingStyle::Subgroup));
    m_nodeHeading->addItem(tr("Hidden"), int(LegendNode::HeadingStyle::Hidden));
    connect(m_nodeHeading, &QComboBox::currentIndexChanged, this, [this, editNode](int) {
        const auto h = LegendNode::HeadingStyle(m_nodeHeading->currentData().toInt());
        editNode([h](LegendNode &n) { n.heading = h; });
    });
    form->addRow(tr("Heading:"), m_nodeHeading);
    m_nodeColumnBreak = new QCheckBox(tr("Start a new column here"));
    m_nodeColumnBreak->setObjectName(QStringLiteral("nodeColumnBreak"));
    connect(m_nodeColumnBreak, &QCheckBox::toggled, this,
            [editNode](bool on) { editNode([on](LegendNode &n) { n.columnBreakBefore = on; }); });
    form->addRow(QString(), m_nodeColumnBreak);
    m_nodeItemColumns = new QSpinBox;
    m_nodeItemColumns->setObjectName(QStringLiteral("nodeItemColumns"));
    m_nodeItemColumns->setRange(1, 8);
    connect(m_nodeItemColumns, &QSpinBox::valueChanged, this,
            [editNode](int v) { editNode([v](LegendNode &n) { n.itemColumns = v > 1 ? v : 0; }); });
    form->addRow(tr("Rows side by side:"), m_nodeItemColumns);
    m_nodeRamp = new QComboBox;
    m_nodeRamp->setObjectName(QStringLiteral("nodeRamp"));
    m_nodeRamp->addItem(tr("As the legend"), int(LegendNode::RampChoice::Inherit));
    m_nodeRamp->addItem(tr("Color bar"), int(LegendNode::RampChoice::Bar));
    m_nodeRamp->addItem(tr("Swatches"), int(LegendNode::RampChoice::Swatches));
    connect(m_nodeRamp, &QComboBox::currentIndexChanged, this, [this, editNode](int) {
        const auto r = LegendNode::RampChoice(m_nodeRamp->currentData().toInt());
        editNode([r](LegendNode &n) { n.ramp = r; });
    });
    form->addRow(tr("Color ramps:"), m_nodeRamp);
    m_nodeSymbolWidth = new QSpinBox;
    m_nodeSymbolWidth->setObjectName(QStringLiteral("nodeSymbolWidth"));
    m_nodeSymbolWidth->setRange(0, 200);
    m_nodeSymbolWidth->setSpecialValueText(tr("As the legend"));
    m_nodeSymbolWidth->setSuffix(tr(" px"));
    connect(m_nodeSymbolWidth, &QSpinBox::valueChanged, this,
            [editNode](int v) { editNode([v](LegendNode &n) { n.symbolWidth = v; }); });
    form->addRow(tr("Symbol width:"), m_nodeSymbolWidth);
    m_nodeSymbolHeight = new QSpinBox;
    m_nodeSymbolHeight->setObjectName(QStringLiteral("nodeSymbolHeight"));
    m_nodeSymbolHeight->setRange(0, 200);
    m_nodeSymbolHeight->setSpecialValueText(tr("As the legend"));
    m_nodeSymbolHeight->setSuffix(tr(" px"));
    connect(m_nodeSymbolHeight, &QSpinBox::valueChanged, this,
            [editNode](int v) { editNode([v](LegendNode &n) { n.symbolHeight = v; }); });
    form->addRow(tr("Symbol height:"), m_nodeSymbolHeight);
    m_nodeTabs->addTab(general, tr("General"));
    m_nodeTabs->addTab(buildNodeNumbersPage(), tr("Numbers"));
    m_nodeTabs->addTab(buildNodeColorBarPage(), tr("Color bar"));
    m_nodeTabs->addTab(buildNodeTextPage(), tr("Text"));
    m_nodeTabs->addTab(buildNodeCountsPage(), tr("Counts"));
    layout->addWidget(m_nodeBox);
    return page;
}

QWidget *LegendPropertiesDialog::buildNodeNumbersPage()
{
    using OpenSWMM::Render::NumberLabelFormat;
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    m_numbersNote = new QLabel(page);
    m_numbersNote->setWordWrap(true);
    form->addRow(m_numbersNote);
    m_numberMode = new QComboBox(page);
    m_numberMode->setObjectName(QStringLiteral("numberMode"));
    m_numberMode->addItem(tr("Decimal places"), int(NumberLabelFormat::Mode::Decimals));
    m_numberMode->addItem(tr("Significant figures"),
                          int(NumberLabelFormat::Mode::SignificantFigures));
    form->addRow(tr("Numbers:"), m_numberMode);
    m_numberDigits = new QSpinBox(page);
    m_numberDigits->setObjectName(QStringLiteral("numberDigits"));
    m_numberDigits->setRange(0, 12);
    form->addRow(tr("Digits:"), m_numberDigits);
    m_numberTrim = new QCheckBox(tr("Drop trailing zeros"), page);
    m_numberTrim->setObjectName(QStringLiteral("numberTrim"));
    form->addRow(QString(), m_numberTrim);
    m_numberThousands = new QCheckBox(tr("Thousands separator (1,234)"), page);
    m_numberThousands->setObjectName(QStringLiteral("numberThousands"));
    form->addRow(QString(), m_numberThousands);
    m_numberPrefix = new QLineEdit(page);
    m_numberPrefix->setObjectName(QStringLiteral("numberPrefix"));
    m_numberPrefix->setPlaceholderText(tr("None"));
    form->addRow(tr("Text before:"), m_numberPrefix);
    m_numberSuffix = new QLineEdit(page);
    m_numberSuffix->setObjectName(QStringLiteral("numberSuffix"));
    m_numberSuffix->setPlaceholderText(tr("None (for example \" m\")"));
    m_numberSuffix->setToolTip(tr("Units or other text after each value; on a color bar it "
                                  "replaces the layer's own units"));
    form->addRow(tr("Units after:"), m_numberSuffix);
    m_numberSeparator = new QLineEdit(page);
    m_numberSeparator->setObjectName(QStringLiteral("numberSeparator"));
    m_numberSeparator->setPlaceholderText(NumberLabelFormat().rangeSeparator);
    m_numberSeparator->setToolTip(tr("Text between the two ends of a range; empty uses \"%1\"")
                                      .arg(NumberLabelFormat().rangeSeparator));
    form->addRow(tr("Range separator:"), m_numberSeparator);
    m_numberControls = {m_numberMode,   m_numberDigits, m_numberTrim,     m_numberThousands,
                        m_numberPrefix, m_numberSuffix, m_numberSeparator};

    const auto apply = [this] { applyNumberFormat(); };
    connect(m_numberMode, &QComboBox::currentIndexChanged, this, apply);
    connect(m_numberDigits, &QSpinBox::valueChanged, this, apply);
    connect(m_numberTrim, &QCheckBox::toggled, this, apply);
    connect(m_numberThousands, &QCheckBox::toggled, this, apply);
    connect(m_numberPrefix, &QLineEdit::editingFinished, this, apply);
    connect(m_numberSuffix, &QLineEdit::editingFinished, this, apply);
    connect(m_numberSeparator, &QLineEdit::editingFinished, this, apply);
    return page;
}

namespace
{

/** "As the legend", then two choices stored as 1 and 0. */
QComboBox *followOrChoose(const char *name, const QString &one, const QString &zero)
{
    auto *box = new QComboBox;
    box->setObjectName(QString::fromLatin1(name));
    box->addItem(QObject::tr("As the legend"), -1);
    box->addItem(one, 1);
    box->addItem(zero, 0);
    return box;
}

} // namespace

void LegendPropertiesDialog::editSelected(const std::function<void(LegendNode &)> &edit)
{
    if (m_items)
        m_items->editSettings(m_items->indexOfKey(m_selectedKey), edit);
}

QWidget *LegendPropertiesDialog::buildNodeColorBarPage()
{
    using OpenSWMM::Render::LegendRampOverrides;
    using openswmmvis::map::LegendContent::LegendTreeItem;
    m_rampPage = new QWidget;
    auto *form = new QFormLayout(m_rampPage);
    auto *note = new QLabel(tr("This layer's color bars; \"As the legend\" follows the Symbols "
                               "tab."));
    note->setWordWrap(true);
    form->addRow(note);
    // A choice stored in one field of the layer's color bar settings.
    const auto choice = [&](QComboBox *box, const QString &label, int LegendRampOverrides::*field) {
        connect(box, &QComboBox::currentIndexChanged, this, [this, box, field](int) {
            const int v = box->currentData().toInt();
            editSelected([v, field](LegendNode &n) { n.rampSettings.*field = v; });
        });
        m_nodeRefreshers.append([box, field](const LegendTreeItem *item) {
            const QSignalBlocker b(box);
            box->setCurrentIndex(box->findData(item->node.rampSettings.*field));
        });
        form->addRow(label, box);
    };
    const auto number = [&](const char *name, const QString &label, int max,
                            int LegendRampOverrides::*field, const QString &suffix) {
        auto *s = new QSpinBox;
        s->setObjectName(QString::fromLatin1(name));
        s->setRange(0, max);
        s->setSpecialValueText(tr("As the legend"));
        s->setSuffix(suffix);
        connect(s, &QSpinBox::valueChanged, this, [this, field](int v) {
            editSelected([v, field](LegendNode &n) { n.rampSettings.*field = v; });
        });
        m_nodeRefreshers.append([s, field](const LegendTreeItem *item) {
            const QSignalBlocker b(s);
            s->setValue(item->node.rampSettings.*field);
        });
        form->addRow(label, s);
    };
    choice(followOrChoose("nodeRampOrientation", tr("Horizontal"), tr("Vertical")),
           tr("Direction:"), &LegendRampOverrides::orientation);
    number("nodeRampLength", tr("Length:"), 600, &LegendRampOverrides::length, tr(" px"));
    number("nodeRampThickness", tr("Thickness:"), 80, &LegendRampOverrides::thickness, tr(" px"));
    number("nodeRampTicks", tr("Values shown:"), 15, &LegendRampOverrides::tickCount, QString());
    choice(followOrChoose("nodeRampHighAtTop", tr("High value at the top"),
                          tr("Low value at the top")),
           tr("Vertical bars:"), &LegendRampOverrides::highAtTop);
    choice(followOrChoose("nodeRampEndCaps", tr("Show"), tr("Hide")), tr("Out-of-range caps:"),
           &LegendRampOverrides::endCaps);
    return m_rampPage;
}

void LegendPropertiesDialog::nodeTextRow(QGridLayout *grid, int row, const QString &label,
                                         bool heading)
{
    using openswmmvis::map::LegendContent::LegendTreeItem;
    const QString base = heading ? QStringLiteral("nodeHeading") : QStringLiteral("nodeItem");
    auto *own = new QCheckBox(tr("Own"));
    own->setObjectName(base + QStringLiteral(".own"));
    own->setToolTip(tr("Use this font and color instead of the legend's"));
    auto *family = new QFontComboBox;
    family->setObjectName(base + QStringLiteral(".family"));
    family->setEditable(false);
    family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    family->setMinimumContentsLength(10);
    family->setPlaceholderText(tr("System font"));
    auto *size = new QDoubleSpinBox;
    size->setObjectName(base + QStringLiteral(".size"));
    size->setRange(4, 96);
    size->setDecimals(1);
    size->setSuffix(tr(" pt"));
    auto *bold = new QCheckBox(tr("Bold"));
    bold->setObjectName(base + QStringLiteral(".bold"));
    auto *italic = new QCheckBox(tr("Italic"));
    italic->setObjectName(base + QStringLiteral(".italic"));
    auto *swatch = new ColorButton;
    swatch->setObjectName(base + QStringLiteral(".color"));
    swatch->setAutoDefault(false);
    swatch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // The node's font and color, and the legend's ones they start from.
    const auto fontOf = [heading](LegendNode &n) -> std::optional<QFont> & {
        return heading ? n.headingFont : n.itemFont;
    };
    const auto colorOf = [heading](LegendNode &n) -> QColor & {
        return heading ? n.headingColor : n.itemColor;
    };
    const auto legendFont = [this, heading] {
        return heading ? m_style->layerHeaderFont() : m_style->itemFont();
    };
    const auto legendColor = [this, heading] {
        return heading ? m_style->layerHeaderColor() : m_style->itemColor();
    };
    connect(own, &QCheckBox::toggled, this,
            [this, fontOf, colorOf, legendFont, legendColor](bool on) {
                const QFont f = legendFont();
                const QColor c = legendColor();
                editSelected([on, f, c, fontOf, colorOf](LegendNode &n) {
                    fontOf(n) = on ? std::optional<QFont>(f) : std::nullopt;
                    colorOf(n) = on ? c : QColor();
                });
            });
    const auto editFont = [this, fontOf, legendFont](const std::function<void(QFont &)> &change) {
        const QFont start = legendFont();
        editSelected([change, start, fontOf](LegendNode &n) {
            QFont f = fontOf(n).value_or(start);
            change(f);
            fontOf(n) = f;
        });
    };
    connect(family, &QFontComboBox::currentFontChanged, this, [editFont](const QFont &picked) {
        editFont([&picked](QFont &f) { f.setFamily(picked.family()); });
    });
    connect(size, &QDoubleSpinBox::valueChanged, this,
            [editFont](double pt) { editFont([pt](QFont &f) { f.setPointSizeF(pt); }); });
    connect(bold, &QCheckBox::toggled, this,
            [editFont](bool on) { editFont([on](QFont &f) { f.setBold(on); }); });
    connect(italic, &QCheckBox::toggled, this,
            [editFont](bool on) { editFont([on](QFont &f) { f.setItalic(on); }); });
    connect(swatch, &ColorButton::colorChanged, this, [this, colorOf](const QColor &c) {
        editSelected([c, colorOf](LegendNode &n) { colorOf(n) = c; });
    });
    m_nodeRefreshers.append([=](const LegendTreeItem *item) {
        using Type = LegendNode::Type;
        LegendNode n = item->node;
        const bool applies =
            heading ? (n.type == Type::Layer || n.type == Type::Group) : n.type == Type::Layer;
        const bool isOwn = fontOf(n).has_value() || colorOf(n).isValid();
        const QSignalBlocker b0(own), b1(family), b2(size), b3(bold), b4(italic), b5(swatch);
        own->setEnabled(applies);
        own->setChecked(applies && isOwn);
        for (QWidget *w : {static_cast<QWidget *>(family), static_cast<QWidget *>(size),
                           static_cast<QWidget *>(bold), static_cast<QWidget *>(italic),
                           static_cast<QWidget *>(swatch)})
            w->setEnabled(applies && isOwn);
        const QFont f = fontOf(n).value_or(legendFont());
        const int at = family->findText(QFontInfo(f).family(), Qt::MatchFixedString);
        family->setCurrentIndex(at >= 0 && !f.family().isEmpty() ? at : -1);
        size->setValue(f.pointSizeF() > 0 ? f.pointSizeF() : QFont().pointSizeF());
        bold->setChecked(f.bold());
        italic->setChecked(f.italic());
        swatch->setColor(colorOf(n).isValid() ? colorOf(n) : legendColor());
    });
    grid->addWidget(new QLabel(label), row, 0);
    grid->addWidget(own, row, 1);
    grid->addWidget(family, row, 2);
    grid->addWidget(size, row, 3);
    grid->addWidget(bold, row, 4);
    grid->addWidget(italic, row, 5);
    grid->addWidget(swatch, row, 6);
}

QWidget *LegendPropertiesDialog::buildNodeTextPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *note = new QLabel(tr("A layer's (or group's) own heading and row text; rows cover the "
                               "layer's symbols and color bar values. Sub-headings keep the "
                               "legend's."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *grid = new QGridLayout;
    nodeTextRow(grid, 0, tr("Heading"), true);
    nodeTextRow(grid, 1, tr("Rows"), false);
    grid->setColumnStretch(2, 1);
    layout->addLayout(grid);
    layout->addStretch(1);
    return page;
}

QWidget *LegendPropertiesDialog::buildNodeCountsPage()
{
    using openswmmvis::map::LegendContent::LegendTreeItem;
    m_countsPage = new QWidget;
    auto *form = new QFormLayout(m_countsPage);
    auto *note = new QLabel(tr("Vector layers can count their features; other layers always list "
                               "all their rows."));
    note->setWordWrap(true);
    form->addRow(note);
    auto *counts = followOrChoose("nodeCounts", tr("Show"), tr("Hide"));
    connect(counts, &QComboBox::currentIndexChanged, this, [this, counts](int) {
        const int v = counts->currentData().toInt();
        editSelected([v](LegendNode &n) { n.featureCounts = v; });
    });
    form->addRow(tr("Feature counts:"), counts);
    auto *filter =
        followOrChoose("nodeExtentFilter", tr("Only rows in the map view"), tr("All rows"));
    connect(filter, &QComboBox::currentIndexChanged, this, [this, filter](int) {
        const int v = filter->currentData().toInt();
        editSelected([v](LegendNode &n) { n.mapExtentFilter = v; });
    });
    form->addRow(tr("Rows listed:"), filter);
    m_nodeRefreshers.append([counts, filter](const LegendTreeItem *item) {
        const QSignalBlocker b1(counts), b2(filter);
        counts->setCurrentIndex(counts->findData(item->node.featureCounts));
        filter->setCurrentIndex(filter->findData(item->node.mapExtentFilter));
    });
    return m_countsPage;
}

OpenSWMMVisLayer *LegendPropertiesDialog::selectedLayer() const
{
    if (!m_items)
        return nullptr;
    const auto *item = m_items->itemAt(m_items->indexOfKey(m_selectedKey));
    if (!item ||
        (item->node.type != LegendNode::Type::Layer && item->node.type != LegendNode::Type::Item))
        return nullptr;
    return item->layer.data();
}

void LegendPropertiesDialog::applyNumberFormat()
{
    using OpenSWMM::Render::NumberLabelFormat;
    OpenSWMMVisLayer *layer = selectedLayer();
    NumberLabelFormat current;
    if (!layer || !layer->legendNumberFormat(&current))
        return;
    NumberLabelFormat f = current;
    f.mode = NumberLabelFormat::Mode(m_numberMode->currentData().toInt());
    f.precision = m_numberDigits->value();
    f.trimTrailingZeros = m_numberTrim->isChecked();
    f.thousandsSeparator = m_numberThousands->isChecked();
    f.prefix = m_numberPrefix->text();
    f.suffix = m_numberSuffix->text();
    f.rangeSeparator = m_numberSeparator->text().isEmpty() ? NumberLabelFormat().rangeSeparator
                                                           : m_numberSeparator->text();
    if (f == current)
        return;
    // One undo step on the map's stack (the legend's), so Ctrl+Z on the map undoes it.
    const auto *recorder = openswmmvis::map::LegendStyleUndoRecorder::forStyle(m_style);
    if (recorder && recorder->stack())
        recorder->stack()->push(new openswmmvis::map::SetLayerNumberFormatCommand(layer, f));
    else
        layer->setLegendNumberFormat(f);
    // The items now know this format, so undoing it (from any view) refreshes these controls.
    m_items->rebuild();
}

void LegendPropertiesDialog::selectKey(const QString &key)
{
    if (!m_itemsView)
        return;
    const QModelIndex index = m_items->indexOfKey(key);
    const QSignalBlocker block(m_itemsView->selectionModel());
    if (index.isValid())
        m_itemsView->setCurrentIndex(index);
    else
        m_itemsView->selectionModel()->clearCurrentIndex();
    m_selectedKey = m_items->keyOf(index);
}

void LegendPropertiesDialog::updateItemControls()
{
    if (!m_items || !m_style)
        return;
    const QModelIndex index = m_items->indexOfKey(m_selectedKey);
    const auto *item = m_items->itemAt(index);
    const bool manual = m_items->isManual();
    for (QWidget *b : std::as_const(m_structureButtons))
        b->setEnabled(manual);
    // A sublayer without a heading row only gathers its rows; it has no settings of its own.
    m_nodeBox->setEnabled(item != nullptr && !m_items->isSublayer(index));
    if (!item)
        return;
    using Type = LegendNode::Type;
    const Type type = item->node.type;
    const QSignalBlocker b1(m_nodeLabel), b2(m_nodeHeading), b3(m_nodeColumnBreak),
        b4(m_nodeItemColumns), b5(m_nodeRamp), b6(m_nodeSymbolWidth), b7(m_nodeSymbolHeight);
    if (!m_nodeLabel->hasFocus())
        m_nodeLabel->setText(item->node.userLabel);
    m_nodeLabel->setPlaceholderText(item->sourceLabel.isEmpty() ? tr("Original name")
                                                                : item->sourceLabel);
    m_nodeHeading->setEnabled(type == Type::Layer || type == Type::Group);
    m_nodeHeading->setCurrentIndex(m_nodeHeading->findData(int(item->node.heading)));
    m_nodeColumnBreak->setChecked(item->node.columnBreakBefore);
    m_nodeItemColumns->setEnabled(type == Type::Layer);
    m_nodeItemColumns->setValue(std::max(1, item->node.itemColumns));
    m_nodeRamp->setEnabled(type == Type::Layer);
    m_nodeRamp->setCurrentIndex(m_nodeRamp->findData(int(item->node.ramp)));
    m_nodeSymbolWidth->setEnabled(type == Type::Layer || type == Type::Item);
    m_nodeSymbolWidth->setValue(item->node.symbolWidth);
    m_nodeSymbolHeight->setEnabled(type == Type::Layer || type == Type::Item);
    m_nodeSymbolHeight->setValue(item->node.symbolHeight);
    // Color bar, Text and Counts: layer settings (the Text page also covers groups).
    m_rampPage->setEnabled(type == Type::Layer);
    m_countsPage->setEnabled(type == Type::Layer);
    for (const auto &r : std::as_const(m_nodeRefreshers))
        r(item);
    // Numbers: the layer's own format (a row edits its layer's).
    {
        OpenSWMM::Render::NumberLabelFormat numbers;
        OpenSWMMVisLayer *layer = selectedLayer();
        const bool hasNumbers = layer && layer->legendNumberFormat(&numbers);
        for (QWidget *w : std::as_const(m_numberControls))
            w->setEnabled(hasNumbers);
        m_numbersNote->setText(
            hasNumbers ? tr("The layer's own number format, for all its labels: the Layer Style "
                            "dialog and the Legend dock show the same.")
                       : tr("This item has no number labels to format."));
        if (hasNumbers) {
            const QSignalBlocker n1(m_numberMode), n2(m_numberDigits), n3(m_numberTrim),
                n4(m_numberThousands);
            m_numberMode->setCurrentIndex(m_numberMode->findData(int(numbers.mode)));
            m_numberDigits->setValue(numbers.precision);
            m_numberTrim->setChecked(numbers.trimTrailingZeros);
            m_numberThousands->setChecked(numbers.thousandsSeparator);
            for (const auto &[edit, value] : {std::pair{m_numberPrefix, numbers.prefix},
                                              std::pair{m_numberSuffix, numbers.suffix},
                                              std::pair{m_numberSeparator, numbers.rangeSeparator}})
                if (!edit->hasFocus()) {
                    const QSignalBlocker b(edit);
                    edit->setText(value);
                }
        }
    }
    const int row = index.row();
    const int siblings = m_items->rowCount(index.parent());
    const auto setEnabled = [this](const char *name, bool on) {
        if (auto *w = findChild<QPushButton *>(QString::fromLatin1(name)))
            w->setEnabled(on);
    };
    setEnabled("itemUp", manual && row > 0);
    setEnabled("itemDown", manual && row + 1 < siblings);
    const QModelIndex above = m_items->index(row - 1, 0, index.parent());
    const auto *aboveItem = m_items->itemAt(above);
    setEnabled("itemIntoGroup",
               manual && type != Type::Item && aboveItem && aboveItem->node.type == Type::Group);
    const auto *parentItem = m_items->itemAt(index.parent());
    setEnabled("itemOutOfGroup",
               manual && type != Type::Item && parentItem && parentItem->node.type == Type::Group);
    setEnabled("removeItem", manual && type != Type::Item);
}

// ── Layout ──────────────────────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildLayoutTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *title = new QGroupBox(tr("Title"));
    auto *titleForm = new QFormLayout(title);
    titleForm->addRow(check("showTitle", tr("Show the title")));
    titleForm->addRow(tr("Text:"), text("title"));
    layout->addWidget(title);

    auto *columns = new QGroupBox(tr("Columns"));
    auto *columnsForm = new QFormLayout(columns);
    columnsForm->addRow(tr("Columns:"), spin("columnCount", 1, 12));
    columnsForm->addRow(check("equalColumnWidths", tr("Equal column widths")));
    columnsForm->addRow(check("splitLayers", tr("Let a layer's rows continue in the next column")));
    columnsForm->addRow(tr("Space between columns:"), spin("columnSpacing", 0, 200));
    columnsForm->addRow(check("showColumnSeparators", tr("Lines between columns")));
    columnsForm->addRow(tr("Line color:"), color("columnSeparatorColor"));
    columnsForm->addRow(tr("Line width:"), doubleSpin("columnSeparatorWidth", 0, 10, 0.5, 1));
    layout->addWidget(columns);

    auto *labels = new QGroupBox(tr("Long labels"));
    auto *labelsForm = new QFormLayout(labels);
    labelsForm->addRow(
        tr("Long labels:"),
        combo("labelOverflow", {{tr("Shorten with …"), int(Style::LabelOverflow::Elide)},
                                {tr("Wrap onto more lines"), int(Style::LabelOverflow::Wrap)}}));
    labelsForm->addRow(tr("Widest label (automatic width):"), spin("maxLabelWidth", 60, 1200));
    auto *wrapOn = text("wrapString");
    wrapOn->setPlaceholderText(tr("e.g. |"));
    labelsForm->addRow(tr("Also break at:"), wrapOn);
    layout->addWidget(labels);

    auto *content = new QGroupBox(tr("Content"));
    auto *contentForm = new QFormLayout(content);
    auto *onlyInView = check("filterByMapExtent", tr("Only items with features in the map view"));
    onlyInView->setToolTip(tr("Vector layers; other layers list all their rows"));
    contentForm->addRow(onlyInView);
    auto *counts = check("showFeatureCounts", tr("Feature counts after the labels"));
    counts->setToolTip(tr("Vector layers; the count of each row's features, e.g. [12]"));
    contentForm->addRow(counts);
    layout->addWidget(content);

    auto *size = new QGroupBox(tr("Size"));
    auto *sizeForm = new QFormLayout(size);
    sizeForm->addRow(tr("Width:"), spin("explicitWidth", 0, 4000, tr("Fit the content")));
    sizeForm->addRow(tr("Height:"), spin("explicitHeight", 0, 4000, tr("Fit the content")));
    auto *fit = button(tr("Fit to content"), "fitToContent", size);
    connect(fit, &QPushButton::clicked, this, [this] {
        if (!m_style)
            return;
        m_style->setExplicitWidth(0);
        m_style->setExplicitHeight(0);
    });
    sizeForm->addRow(QString(), fit);
    layout->addWidget(size);
    layout->addStretch(1);
    return page;
}

// ── Fonts ───────────────────────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildFontsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *box = new QGroupBox(tr("Text"));
    auto *grid = new QGridLayout(box);
    grid->addWidget(new QLabel(tr("Font")), 0, 1);
    grid->addWidget(new QLabel(tr("Size")), 0, 2);
    grid->addWidget(new QLabel(tr("Color")), 0, 5);
    grid->addWidget(new QLabel(tr("Alignment")), 0, 6);
    fontRow(grid, 1, tr("Title"), "titleFont", "titleColor", "titleAlignment");
    fontRow(grid, 2, tr("Group heading"), "layerHeaderFont", "layerHeaderColor",
            "layerHeaderAlignment");
    fontRow(grid, 3, tr("Sub-heading"), "subheadingFont", "subheadingColor", "subheadingAlignment");
    fontRow(grid, 4, tr("Item"), "itemFont", "itemColor", "itemAlignment");
    grid->setColumnStretch(1, 1);
    layout->addWidget(box);
    layout->addStretch(1);
    return page;
}

// ── Symbols and color ramps ─────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildSymbolsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *symbols = new QGroupBox(tr("Symbols"));
    auto *form = new QFormLayout(symbols);
    form->addRow(tr("Swatch size:"), spin("swatchSize", 1, 200));
    form->addRow(tr("Width:"), spin("symbolWidth", 0, 200, tr("Swatch size")));
    form->addRow(tr("Height:"), spin("symbolHeight", 0, 200, tr("Swatch size")));
    form->addRow(tr("Size:"),
                 combo("symbolSizeMode",
                       {{tr("As on the map (space grows)"), int(Style::SymbolSizeMode::MapSize)},
                        {tr("Fit the width and height"), int(Style::SymbolSizeMode::Fit)}}));
    form->addRow(tr("Smallest:"), doubleSpin("minSymbolSize", 0, 200, 1, 1, tr("No limit")));
    form->addRow(tr("Largest:"), doubleSpin("maxSymbolSize", 0, 200, 1, 1, tr("No limit")));
    form->addRow(
        tr("Position:"),
        combo("symbolPosition", {{tr("Left of the label"), int(Style::SymbolPosition::Left)},
                                 {tr("Right of the label"), int(Style::SymbolPosition::Right)}}));
    form->addRow(tr("Space to the label:"), spin("symbolLabelSpacing", 0, 100));
    form->addRow(tr("Line shape:"),
                 combo("linePatchShape", {{tr("Straight"), int(Style::LinePatchShape::Horizontal)},
                                          {tr("Zigzag"), int(Style::LinePatchShape::ZigZag)},
                                          {tr("S-curve"), int(Style::LinePatchShape::SCurve)}}));
    form->addRow(tr("Fill shape:"),
                 combo("areaPatchShape",
                       {{tr("Rectangle"), int(Style::AreaPatchShape::Rectangle)},
                        {tr("Rounded rectangle"), int(Style::AreaPatchShape::RoundedRectangle)},
                        {tr("Ellipse"), int(Style::AreaPatchShape::Ellipse)}}));
    form->addRow(check("showPatchOutline", tr("Outline fills")));
    // Outline color: automatic (a darker shade of the fill) or chosen.
    auto *outlineRow = new QHBoxLayout;
    auto *autoOutline = new QCheckBox(tr("Darker shade of the fill"));
    autoOutline->setObjectName(QStringLiteral("patchOutlineAuto"));
    ColorButton *outline = color("patchOutlineColor");
    connect(autoOutline, &QCheckBox::toggled, this, [this, outline](bool on) {
        if (m_style)
            m_style->setPatchOutlineColor(
                on ? QColor()
                   : QColor(outline->color().isValid() ? outline->color() : QColor(60, 60, 60)));
    });
    m_refreshers.append([this, autoOutline, outline] {
        const bool automatic = !m_style->patchOutlineColor().isValid();
        const QSignalBlocker block(autoOutline);
        autoOutline->setChecked(automatic);
        outline->setEnabled(!automatic);
    });
    outlineRow->addWidget(autoOutline);
    outlineRow->addWidget(outline);
    outlineRow->addStretch(1);
    form->addRow(tr("Outline color:"), outlineRow);
    form->addRow(tr("Outline width:"), doubleSpin("patchOutlineWidth", 0, 10, 0.5, 1));
    layout->addWidget(symbols);

    auto *ramps = new QGroupBox(tr("Color ramps"));
    auto *rampForm = new QFormLayout(ramps);
    rampForm->addRow(tr("Show as:"),
                     combo("rampDisplay", {{tr("Color bar"), int(Style::RampDisplay::Bar)},
                                           {tr("Swatches"), int(Style::RampDisplay::Swatches)}}));
    rampForm->addRow(
        tr("Direction:"),
        combo("rampOrientation", {{tr("Vertical"), int(Style::RampOrientation::Vertical)},
                                  {tr("Horizontal"), int(Style::RampOrientation::Horizontal)}}));
    rampForm->addRow(tr("Length:"), spin("rampLength", 40, 600));
    rampForm->addRow(tr("Thickness:"), spin("rampThickness", 4, 80));
    rampForm->addRow(tr("Values:"), spin("rampTickCount", 2, 15));
    rampForm->addRow(check("rampHighAtTop", tr("High values at the top (vertical bars)")));
    rampForm->addRow(
        tr("Number format:"),
        combo("rampNumberFormat", {{tr("Automatic"), int(Style::NumberFormat::Auto)},
                                   {tr("Fixed decimals"), int(Style::NumberFormat::Fixed)},
                                   {tr("Scientific"), int(Style::NumberFormat::Scientific)}}));
    rampForm->addRow(tr("Decimals:"), spin("rampDecimals", 0, 10));
    rampForm->addRow(check("rampShowOutline", tr("Outline the bar")));
    rampForm->addRow(tr("Outline color:"), color("rampOutlineColor"));
    rampForm->addRow(check("rampShowTickMarks", tr("Tick marks")));
    rampForm->addRow(check("rampShowEndCaps", tr("End caps for values below and above the range")));
    rampForm->addRow(tr("Space to the values:"), spin("rampLabelSpacing", 0, 50));
    layout->addWidget(ramps);
    layout->addStretch(1);
    return page;
}

// ── Spacing ─────────────────────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildSpacingTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *margins = new QGroupBox(tr("Margins inside the box"));
    auto *marginGrid = new QGridLayout(margins);
    marginGrid->addWidget(new QLabel(tr("Left:")), 0, 0);
    marginGrid->addWidget(spin("marginLeft", 0, 200), 0, 1);
    marginGrid->addWidget(new QLabel(tr("Right:")), 0, 2);
    marginGrid->addWidget(spin("marginRight", 0, 200), 0, 3);
    marginGrid->addWidget(new QLabel(tr("Top:")), 1, 0);
    marginGrid->addWidget(spin("marginTop", 0, 200), 1, 1);
    marginGrid->addWidget(new QLabel(tr("Bottom:")), 1, 2);
    marginGrid->addWidget(spin("marginBottom", 0, 200), 1, 3);
    layout->addWidget(margins);

    auto *space = new QGroupBox(tr("Space"));
    auto *form = new QFormLayout(space);
    form->addRow(tr("Below the title:"), spin("titleSpacing", 0, 200));
    form->addRow(tr("Above groups:"), spin("groupSpacing", 0, 200));
    form->addRow(tr("Below group headings:"), spin("headerSpacing", 0, 200));
    form->addRow(tr("Above sub-headings:"), spin("subheadingSpacing", 0, 200));
    form->addRow(tr("Below rows:"), spin("rowSpacing", 0, 200));
    form->addRow(tr("Between wrapped lines:"), spin("lineSpacing", 0, 50));
    layout->addWidget(space);

    auto *indent = new QGroupBox(tr("Indents"));
    auto *indentForm = new QFormLayout(indent);
    indentForm->addRow(tr("Group headings (and each group level):"), spin("groupIndent", 0, 200));
    indentForm->addRow(tr("Sub-headings:"), spin("subheadingIndent", 0, 200));
    indentForm->addRow(tr("Rows:"), spin("itemIndent", 0, 200));
    layout->addWidget(indent);
    layout->addStretch(1);
    return page;
}

// ── Box ─────────────────────────────────────────────────────────────────────

QWidget *LegendPropertiesDialog::buildBoxTab()
{
    using Anchor = Style::Anchor;
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *place = new QGroupBox(tr("Placement"));
    auto *placeForm = new QFormLayout(place);
    placeForm->addRow(tr("Anchor:"),
                      combo("anchor", {{tr("Top left"), int(Anchor::TopLeft)},
                                       {tr("Top"), int(Anchor::Top)},
                                       {tr("Top right"), int(Anchor::TopRight)},
                                       {tr("Right"), int(Anchor::Right)},
                                       {tr("Bottom right"), int(Anchor::BottomRight)},
                                       {tr("Bottom"), int(Anchor::Bottom)},
                                       {tr("Bottom left"), int(Anchor::BottomLeft)},
                                       {tr("Left"), int(Anchor::Left)},
                                       {tr("Where dragged"), int(Anchor::Free)}}));
    placeForm->addRow(tr("Gap to the map's sides:"), spin("anchorMarginX", 0, 200));
    placeForm->addRow(tr("Gap to the map's top and bottom:"), spin("anchorMarginY", 0, 200));
    placeForm->addRow(tr("Opacity:"), doubleSpin("opacity", 0, 1, 0.05, 2));
    layout->addWidget(place);

    auto *frame = new QGroupBox(tr("Frame"));
    auto *frameForm = new QFormLayout(frame);
    frameForm->addRow(check("showFrame", tr("Show the frame")));
    frameForm->addRow(tr("Color:"), color("frameColor"));
    frameForm->addRow(tr("Width:"), doubleSpin("frameWidth", 0, 20, 0.5, 1));
    frameForm->addRow(tr("Line:"),
                      combo("frameStyle", {{tr("Solid"), int(Style::FrameStyle::Solid)},
                                           {tr("Dashed"), int(Style::FrameStyle::Dash)},
                                           {tr("Dotted"), int(Style::FrameStyle::Dot)},
                                           {tr("Dash-dot"), int(Style::FrameStyle::DashDot)}}));
    frameForm->addRow(tr("Corner radius:"), spin("cornerRadius", 0, 100));
    layout->addWidget(frame);

    auto *background = new QGroupBox(tr("Background"));
    auto *bgForm = new QFormLayout(background);
    bgForm->addRow(tr("Fill:"), combo("backgroundMode",
                                      {{tr("None"), int(Style::BackgroundMode::None)},
                                       {tr("Solid"), int(Style::BackgroundMode::Solid)},
                                       {tr("Gradient"), int(Style::BackgroundMode::Gradient)}}));
    bgForm->addRow(tr("Color:"), color("backgroundColor"));
    bgForm->addRow(tr("Gradient end color:"), color("gradientEndColor"));
    bgForm->addRow(tr("Gradient direction:"),
                   combo("gradientOrientation", {{tr("Top to bottom"), int(Qt::Vertical)},
                                                 {tr("Left to right"), int(Qt::Horizontal)}}));
    layout->addWidget(background);

    auto *shadow = new QGroupBox(tr("Shadow"));
    auto *shadowForm = new QFormLayout(shadow);
    shadowForm->addRow(check("showShadow", tr("Show a drop shadow")));
    shadowForm->addRow(tr("Color:"), color("shadowColor"));
    shadowForm->addRow(tr("Offset right:"), spin("shadowOffsetX", -20, 20));
    shadowForm->addRow(tr("Offset down:"), spin("shadowOffsetY", -20, 20));
    layout->addWidget(shadow);
    layout->addStretch(1);
    return page;
}

// ── Presets ─────────────────────────────────────────────────────────────────

bool LegendPropertiesDialog::savePreset(const QString &path, QString *error) const
{
    if (!m_style)
        return false;
    QJsonObject file;
    file[QStringLiteral("type")] = QString::fromLatin1(Style::kPresetFileType);
    file[QStringLiteral("version")] = 1;
    file[QStringLiteral("style")] = m_style->presetJson();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = tr("Could not write %1: %2").arg(path, out.errorString());
        return false;
    }
    out.write(QJsonDocument(file).toJson(QJsonDocument::Indented));
    return true;
}

bool LegendPropertiesDialog::loadPreset(const QString &path, QString *error)
{
    if (!m_style)
        return false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Could not read %1: %2").arg(path, in.errorString());
        return false;
    }
    const QJsonObject file = QJsonDocument::fromJson(in.readAll()).object();
    if (file.value(QStringLiteral("type")).toString() != QLatin1String(Style::kPresetFileType) ||
        !file.value(QStringLiteral("style")).isObject()) {
        if (error)
            *error = tr("%1 is not a legend style file.").arg(path);
        return false;
    }
    m_style->applyPreset(file.value(QStringLiteral("style")).toObject());
    return true;
}

} // namespace openswmmvis::ui
