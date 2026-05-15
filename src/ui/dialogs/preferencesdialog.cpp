/*!
 * \file   preferencesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/preferencesdialog.h"

#include "core/preferencesmanager.h"
#include "ui/dialogs/licenseagreementdialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(560, 420);
    buildUi();
    readFromManager();
}

void PreferencesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Left-list / right-stack layout.
    auto *split = new QHBoxLayout();
    split->setSpacing(8);
    root->addLayout(split, 1);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(160);
    m_categoryList->addItem(tr("General"));
    m_categoryList->addItem(tr("Selection"));
    m_categoryList->addItem(tr("Canvas && CRS"));
    m_categoryList->addItem(tr("Rendering"));
    m_categoryList->addItem(tr("Simulation"));
    m_categoryList->addItem(tr("Map Display"));
    m_categoryList->addItem(tr("Measure Tool"));
    m_categoryList->addItem(tr("Naming"));
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildSelectionPage());
    m_pages->addWidget(buildCanvasPage());
    m_pages->addWidget(buildRenderingPage());
    m_pages->addWidget(buildSimulationPage());
    m_pages->addWidget(buildMapDisplayPage());
    m_pages->addWidget(buildMeasureToolPage());
    m_pages->addWidget(buildNamingPage());
    split->addWidget(m_pages, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_categoryList->setCurrentRow(1);   // start on Selection — the most
                                         // frequently-tuned page today.

    // Buttons: Reset | Apply / Cancel / OK.
    auto *btnRow = new QHBoxLayout();
    root->addLayout(btnRow);
    auto *resetBtn = new QPushButton(tr("Reset to defaults"), this);
    connect(resetBtn, &QPushButton::clicked,
            this, &PreferencesDialog::onResetToDefaults);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
        this);
    btnRow->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PreferencesDialog::onApply);
}

QWidget *PreferencesDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_showLicenseOnStartupBox = new QCheckBox(
        tr("Show license agreement on startup"), page);
    m_showLicenseOnStartupBox->setToolTip(tr(
        "When checked, the MIT license agreement dialog is shown each time "
        "the application starts. Uncheck to suppress it after you have "
        "accepted the terms."));
    f->addRow(m_showLicenseOnStartupBox);

    return page;
}

QWidget *PreferencesDialog::buildSelectionPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_clickTolerancePxSpin = new QSpinBox(page);
    m_clickTolerancePxSpin->setRange(1, 200);
    m_clickTolerancePxSpin->setSuffix(tr(" px"));
    m_clickTolerancePxSpin->setToolTip(tr(
        "Minimum pick radius for click-selection. The effective radius "
        "at pick time is max(this value, largest-rendered-glyph half-"
        "bound + 4 px halo), so clicks inside any visible marker always "
        "hit regardless of this number."));
    f->addRow(tr("Click tolerance"),           m_clickTolerancePxSpin);

    m_dragThresholdPxSpin = new QSpinBox(page);
    m_dragThresholdPxSpin->setRange(1, 200);
    m_dragThresholdPxSpin->setSuffix(tr(" px"));
    m_dragThresholdPxSpin->setToolTip(tr(
        "Cursor distance required before a click turns into a rubber-band "
        "select. Higher values forgive trackpad jitter on single clicks."));
    f->addRow(tr("Drag threshold"),             m_dragThresholdPxSpin);

    m_clearOnMissBox = new QCheckBox(tr("Clear selection when clicking "
                                         "empty space"), page);
    f->addRow(m_clearOnMissBox);

    // Per-vector-class selection highlight colors. Each row is a flat
    // button whose background previews the current color; clicking pops
    // the standard color picker. The chosen color is held on the
    // m_pendingSelColor* members until Apply / OK propagates to
    // PreferencesManager.
    auto makeColorButton = [page](QPushButton *&btn, QColor &held) {
        btn = new QPushButton(page);
        btn->setMinimumWidth(120);
        btn->setAutoFillBackground(true);
        QObject::connect(btn, &QPushButton::clicked, btn, [btn, &held]() {
            const QColor c = QColorDialog::getColor(
                held.isValid() ? held : QColor(Qt::yellow),
                btn,
                QObject::tr("Selection color"),
                QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            held = c;
            QString css = QStringLiteral(
                "QPushButton { background-color: %1; color: %2; "
                "border: 1px solid #777; padding: 3px 8px; }")
                .arg(c.name(QColor::HexRgb),
                     c.lightness() > 128 ? QStringLiteral("black")
                                         : QStringLiteral("white"));
            btn->setStyleSheet(css);
            btn->setText(c.name(QColor::HexRgb).toUpper());
        });
    };

    makeColorButton(m_selColorLink,     m_pendingSelColorLink);
    makeColorButton(m_selColorNode,     m_pendingSelColorNode);
    makeColorButton(m_selColorSubcatch, m_pendingSelColorSubcatch);
    makeColorButton(m_selColorGage,     m_pendingSelColorGage);

    f->addRow(tr("Selection color — links"),         m_selColorLink);
    f->addRow(tr("Selection color — nodes"),         m_selColorNode);
    f->addRow(tr("Selection color — subcatchments"), m_selColorSubcatch);
    f->addRow(tr("Selection color — rain gages"),    m_selColorGage);

    return page;
}

QWidget *PreferencesDialog::buildCanvasPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_defaultToolCombo = new QComboBox(page);
    m_defaultToolCombo->addItem(tr("Select"), QStringLiteral("Select"));
    m_defaultToolCombo->addItem(tr("Pan"),    QStringLiteral("Pan"));
    m_defaultToolCombo->addItem(tr("Zoom"),   QStringLiteral("Zoom"));
    m_defaultToolCombo->setToolTip(tr(
        "Tool automatically activated when a project window opens."));
    f->addRow(tr("Default tool on project open"), m_defaultToolCombo);

    m_crsAuthorityEdit = new QLineEdit(page);
    m_crsAuthorityEdit->setToolTip(tr(
        "CRS authority used when the loaded .inp has no CRS "
        "(e.g. \"EPSG\")."));
    f->addRow(tr("Default CRS authority"),       m_crsAuthorityEdit);

    m_crsCodeSpin = new QSpinBox(page);
    m_crsCodeSpin->setRange(1, 999999);
    m_crsCodeSpin->setToolTip(tr(
        "CRS code (e.g. 4326 for WGS 84)."));
    f->addRow(tr("Default CRS code"),            m_crsCodeSpin);

    return page;
}

QWidget *PreferencesDialog::buildRenderingPage()
{
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *f    = new QFormLayout();

    m_labelLodSpin = new QDoubleSpinBox(page);
    m_labelLodSpin->setRange(0.01, 100.0);
    m_labelLodSpin->setSingleStep(0.1);
    m_labelLodSpin->setDecimals(2);
    m_labelLodSpin->setToolTip(tr(
        "Minimum view-transform scale (m11) at which labels are drawn. "
        "Higher values hide labels sooner when zooming out; lower "
        "values draw labels even at coarse zoom (at a performance cost)."));
    f->addRow(tr("Label zoom-out threshold (m11)"), m_labelLodSpin);

    auto *lodGroup = new QGroupBox(tr("Label Rendering"), page);
    lodGroup->setLayout(f);
    outer->addWidget(lodGroup);

    auto makeColorButton = [page](QPushButton *&btn, QColor &held) {
        btn = new QPushButton(page);
        btn->setMinimumWidth(120);
        btn->setAutoFillBackground(true);
        QObject::connect(btn, &QPushButton::clicked, btn, [btn, &held]() {
            const QColor c = QColorDialog::getColor(
                held.isValid() ? held : QColor(Qt::yellow),
                btn,
                QObject::tr("Link color"),
                QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            held = c;
            const QString css = QStringLiteral(
                "QPushButton { background-color: %1; color: %2; "
                "border: 1px solid #777; padding: 3px 8px; }")
                .arg(c.name(QColor::HexRgb),
                     c.lightness() > 128 ? QStringLiteral("black")
                                         : QStringLiteral("white"));
            btn->setStyleSheet(css);
            btn->setText(c.name(QColor::HexRgb).toUpper());
        });
    };

    auto *linkGroup = new QGroupBox(tr("Link Colors"), page);
    auto *lf = new QFormLayout(linkGroup);
    makeColorButton(m_linkColorConduit, m_pendingLinkColorConduit);
    makeColorButton(m_linkColorPump,    m_pendingLinkColorPump);
    makeColorButton(m_linkColorOrifice, m_pendingLinkColorOrifice);
    makeColorButton(m_linkColorWeir,    m_pendingLinkColorWeir);
    makeColorButton(m_linkColorOutlet,  m_pendingLinkColorOutlet);
    lf->addRow(tr("Conduits"), m_linkColorConduit);
    lf->addRow(tr("Pumps"),    m_linkColorPump);
    lf->addRow(tr("Orifices"), m_linkColorOrifice);
    lf->addRow(tr("Weirs"),    m_linkColorWeir);
    lf->addRow(tr("Outlets"),  m_linkColorOutlet);
    outer->addWidget(linkGroup);

    outer->addStretch(1);

    return page;
}

QWidget *PreferencesDialog::buildSimulationPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_progressTickMsSpin = new QSpinBox(page);
    m_progressTickMsSpin->setRange(50, 10000);
    m_progressTickMsSpin->setSingleStep(100);
    m_progressTickMsSpin->setSuffix(tr(" ms"));
    m_progressTickMsSpin->setToolTip(tr(
        "Rate at which the running simulation pushes progress / current-"
        "time updates to the Simulation Status dock. Lower = more live, "
        "more overhead. Default 1000 ms (1 Hz)."));
    f->addRow(tr("Progress-tick interval"),       m_progressTickMsSpin);

    return page;
}

QWidget *PreferencesDialog::buildMapDisplayPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *group = new QGroupBox(tr("Scale Bar"), page);
    auto *f     = new QFormLayout(group);

    // Color button (same pattern as selection colors)
    m_scaleBarColorBtn = new QPushButton(group);
    m_scaleBarColorBtn->setMinimumWidth(120);
    m_scaleBarColorBtn->setAutoFillBackground(true);
    connect(m_scaleBarColorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(
            m_pendingScaleBarColor.isValid() ? m_pendingScaleBarColor : Qt::black,
            this,
            tr("Scale bar color"),
            QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        m_pendingScaleBarColor = c;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        m_scaleBarColorBtn->setStyleSheet(css);
        m_scaleBarColorBtn->setText(c.name(QColor::HexRgb).toUpper());
    });
    f->addRow(tr("Color"), m_scaleBarColorBtn);

    m_scaleBarPenWidthSpin = new QSpinBox(group);
    m_scaleBarPenWidthSpin->setRange(1, 20);
    m_scaleBarPenWidthSpin->setSuffix(tr(" px"));
    f->addRow(tr("Line width"), m_scaleBarPenWidthSpin);

    m_scaleBarPenStyleCombo = new QComboBox(group);
    m_scaleBarPenStyleCombo->addItem(tr("Solid"),       static_cast<int>(Qt::SolidLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash"),        static_cast<int>(Qt::DashLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dot"),         static_cast<int>(Qt::DotLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash-Dot"),    static_cast<int>(Qt::DashDotLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash-Dot-Dot"), static_cast<int>(Qt::DashDotDotLine));
    f->addRow(tr("Line style"), m_scaleBarPenStyleCombo);

    m_scaleBarFontBtn = new QPushButton(group);
    connect(m_scaleBarFontBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, m_pendingScaleBarFont, this, tr("Scale bar font"));
        if (!ok) return;
        m_pendingScaleBarFont = f;
        m_scaleBarFontBtn->setText(QStringLiteral("%1, %2pt")
            .arg(f.family()).arg(f.pointSize()));
    });
    f->addRow(tr("Font"), m_scaleBarFontBtn);

    m_scaleBarUnitsCombo = new QComboBox(group);
    m_scaleBarUnitsCombo->addItem(tr("Auto"),       0);
    m_scaleBarUnitsCombo->addItem(tr("Meters"),     1);
    m_scaleBarUnitsCombo->addItem(tr("Feet"),        2);
    m_scaleBarUnitsCombo->addItem(tr("Kilometers"), 3);
    m_scaleBarUnitsCombo->addItem(tr("Miles"),      4);
    f->addRow(tr("Units"), m_scaleBarUnitsCombo);

    m_scaleBarPositionCombo = new QComboBox(group);
    m_scaleBarPositionCombo->addItem(tr("Bottom Left"),  0);
    m_scaleBarPositionCombo->addItem(tr("Bottom Right"), 1);
    m_scaleBarPositionCombo->addItem(tr("Top Left"),     2);
    m_scaleBarPositionCombo->addItem(tr("Top Right"),    3);
    f->addRow(tr("Position"), m_scaleBarPositionCombo);

    m_scaleBarMaxBarLengthSpin = new QSpinBox(group);
    m_scaleBarMaxBarLengthSpin->setRange(20, 500);
    m_scaleBarMaxBarLengthSpin->setSuffix(tr(" px"));
    m_scaleBarMaxBarLengthSpin->setToolTip(tr(
        "Reference pixel length used to calculate the nearest round-number "
        "distance. Larger values produce a longer scale bar."));
    f->addRow(tr("Max bar length"), m_scaleBarMaxBarLengthSpin);

    m_scaleBarLabelDecimalsSpin = new QSpinBox(group);
    m_scaleBarLabelDecimalsSpin->setRange(-1, 4);
    m_scaleBarLabelDecimalsSpin->setSpecialValueText(tr("Auto"));
    m_scaleBarLabelDecimalsSpin->setToolTip(tr(
        "Number of decimal places shown in the scale bar label.\n"
        "-1 (Auto) removes trailing zeros (e.g. \"1 km\", \"2.5 km\").\n"
        "0 rounds to a whole number (e.g. \"1 km\").\n"
        "1–4 shows that many fixed decimal places."));
    f->addRow(tr("Label decimals"), m_scaleBarLabelDecimalsSpin);

    m_scaleBarCompactNotationBox = new QCheckBox(tr("Compact notation (\"1k\" / \"500m\")"), group);
    m_scaleBarCompactNotationBox->setToolTip(tr(
        "When checked, omits the space and shortens units:\n"
        "\"1 km\" → \"1k\",  \"500 m\" → \"500m\"."));
    f->addRow(QString(), m_scaleBarCompactNotationBox);

    outer->addWidget(group);
    outer->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildMeasureToolPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    // Helper: create a color button with live background preview.
    auto makeColorBtn = [this](QGroupBox *parent) -> QPushButton * {
        auto *btn = new QPushButton(parent);
        btn->setMinimumWidth(120);
        btn->setAutoFillBackground(true);
        return btn;
    };
    auto applyColorBtn = [](QPushButton *btn, const QColor &c) {
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };

    // ── Line & vertex group ───────────────────────────────────────────────
    auto *lineGroup = new QGroupBox(tr("Lines && Vertices"), page);
    auto *lf        = new QFormLayout(lineGroup);

    m_measureLineColorBtn = makeColorBtn(lineGroup);
    connect(m_measureLineColorBtn, &QPushButton::clicked, this, [this, applyColorBtn]() {
        const QColor c = QColorDialog::getColor(
            m_pendingMeasureLineColor.isValid() ? m_pendingMeasureLineColor : Qt::red,
            this, tr("Measure line color"));
        if (!c.isValid()) return;
        m_pendingMeasureLineColor = c;
        applyColorBtn(m_measureLineColorBtn, c);
    });
    lf->addRow(tr("Line && vertex color"), m_measureLineColorBtn);
    outer->addWidget(lineGroup);

    // ── Labels group ─────────────────────────────────────────────────────
    auto *labelGroup = new QGroupBox(tr("Labels"), page);
    auto *labf       = new QFormLayout(labelGroup);

    m_measureLabelFontBtn = new QPushButton(labelGroup);
    connect(m_measureLabelFontBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, m_pendingMeasureLabelFont,
                                              this, tr("Measure label font"));
        if (!ok) return;
        m_pendingMeasureLabelFont = f;
        m_measureLabelFontBtn->setText(QStringLiteral("%1, %2pt")
            .arg(f.family()).arg(f.pointSize()));
    });
    labf->addRow(tr("Font"), m_measureLabelFontBtn);

    m_measureLabelDecimalsSpin = new QSpinBox(labelGroup);
    m_measureLabelDecimalsSpin->setRange(0, 6);
    m_measureLabelDecimalsSpin->setToolTip(tr("Number of decimal places shown in distance and area labels."));
    labf->addRow(tr("Decimal places"), m_measureLabelDecimalsSpin);
    outer->addWidget(labelGroup);

    // ── Area fill group ───────────────────────────────────────────────────
    auto *fillGroup = new QGroupBox(tr("Area Fill"), page);
    auto *ff        = new QFormLayout(fillGroup);

    m_measureFillColorBtn = makeColorBtn(fillGroup);
    connect(m_measureFillColorBtn, &QPushButton::clicked, this, [this, applyColorBtn]() {
        const QColor c = QColorDialog::getColor(
            m_pendingMeasureFillColor.isValid() ? m_pendingMeasureFillColor
                                                : QColor(100, 149, 237),
            this, tr("Area fill color"));
        if (!c.isValid()) return;
        m_pendingMeasureFillColor = c;
        applyColorBtn(m_measureFillColorBtn, c);
    });
    ff->addRow(tr("Fill color"), m_measureFillColorBtn);

    m_measureFillOpacitySpin = new QSpinBox(fillGroup);
    m_measureFillOpacitySpin->setRange(0, 100);
    m_measureFillOpacitySpin->setSuffix(tr(" %"));
    m_measureFillOpacitySpin->setToolTip(tr("Opacity of the area polygon fill (0 = transparent, 100 = opaque)."));
    ff->addRow(tr("Fill opacity"), m_measureFillOpacitySpin);
    outer->addWidget(fillGroup);

    outer->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

void PreferencesDialog::readFromManager()
{
    auto *p = PreferencesManager::instance();

    m_showLicenseOnStartupBox->setChecked(LicenseAgreementDialog::shouldShowOnStartup());

    m_clickTolerancePxSpin->setValue(p->clickTolerancePx());
    m_dragThresholdPxSpin->setValue(p->dragThresholdPx());
    m_clearOnMissBox->setChecked(p->clearSelectionOnMiss());

    auto applyColor = [](QPushButton *btn, QColor &held, const QColor &c) {
        held = c;
        QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };
    applyColor(m_selColorLink,     m_pendingSelColorLink,
               p->selectionColor(QStringLiteral("link")));
    applyColor(m_selColorNode,     m_pendingSelColorNode,
               p->selectionColor(QStringLiteral("node")));
    applyColor(m_selColorSubcatch, m_pendingSelColorSubcatch,
               p->selectionColor(QStringLiteral("subcatchment")));
    applyColor(m_selColorGage,     m_pendingSelColorGage,
               p->selectionColor(QStringLiteral("gage")));

    const int toolIdx = m_defaultToolCombo->findData(p->defaultTool());
    m_defaultToolCombo->setCurrentIndex(toolIdx >= 0 ? toolIdx : 0);
    m_crsAuthorityEdit->setText(p->defaultCrsAuthority());
    m_crsCodeSpin->setValue(p->defaultCrsCode());

    m_labelLodSpin->setValue(p->labelLodM11Min());
    applyColor(m_linkColorConduit, m_pendingLinkColorConduit,
               p->linkColor(QStringLiteral("conduit")));
    applyColor(m_linkColorPump, m_pendingLinkColorPump,
               p->linkColor(QStringLiteral("pump")));
    applyColor(m_linkColorOrifice, m_pendingLinkColorOrifice,
               p->linkColor(QStringLiteral("orifice")));
    applyColor(m_linkColorWeir, m_pendingLinkColorWeir,
               p->linkColor(QStringLiteral("weir")));
    applyColor(m_linkColorOutlet, m_pendingLinkColorOutlet,
               p->linkColor(QStringLiteral("outlet")));

    m_progressTickMsSpin->setValue(p->progressTickMs());

    // Scale Bar
    applyColor(m_scaleBarColorBtn, m_pendingScaleBarColor, p->scaleBarPenColor());
    m_scaleBarPenWidthSpin->setValue(p->scaleBarPenWidth());
    {
        const int idx = m_scaleBarPenStyleCombo->findData(p->scaleBarPenStyle());
        m_scaleBarPenStyleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_pendingScaleBarFont = QFont(p->scaleBarFontFamily(), p->scaleBarFontSize());
    m_scaleBarFontBtn->setText(QStringLiteral("%1, %2pt")
        .arg(m_pendingScaleBarFont.family()).arg(m_pendingScaleBarFont.pointSize()));
    {
        const int idx = m_scaleBarUnitsCombo->findData(p->scaleBarUnits());
        m_scaleBarUnitsCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    {
        const int idx = m_scaleBarPositionCombo->findData(p->scaleBarPosition());
        m_scaleBarPositionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_scaleBarMaxBarLengthSpin->setValue(p->scaleBarMaxBarLength());
    m_scaleBarLabelDecimalsSpin->setValue(p->scaleBarLabelDecimals());
    m_scaleBarCompactNotationBox->setChecked(p->scaleBarCompactNotation());

    // Measure Tool
    applyColor(m_measureLineColorBtn, m_pendingMeasureLineColor, p->measureLineColor());
    m_pendingMeasureLabelFont = QFont(p->measureLabelFontFamily(), p->measureLabelFontSize());
    m_measureLabelFontBtn->setText(QStringLiteral("%1, %2pt")
        .arg(m_pendingMeasureLabelFont.family()).arg(m_pendingMeasureLabelFont.pointSize()));
    m_measureLabelDecimalsSpin->setValue(p->measureLabelDecimals());
    applyColor(m_measureFillColorBtn, m_pendingMeasureFillColor, p->measureFillColor());
    m_measureFillOpacitySpin->setValue(p->measureFillOpacity());

    // Naming prefixes
    m_prefixJunction    ->setText(p->elementNamePrefix(QStringLiteral("junction")));
    m_prefixOutfall     ->setText(p->elementNamePrefix(QStringLiteral("outfall")));
    m_prefixStorage     ->setText(p->elementNamePrefix(QStringLiteral("storage")));
    m_prefixDivider     ->setText(p->elementNamePrefix(QStringLiteral("divider")));
    m_prefixConduit     ->setText(p->elementNamePrefix(QStringLiteral("conduit")));
    m_prefixPump        ->setText(p->elementNamePrefix(QStringLiteral("pump")));
    m_prefixOrifice     ->setText(p->elementNamePrefix(QStringLiteral("orifice")));
    m_prefixWeir        ->setText(p->elementNamePrefix(QStringLiteral("weir")));
    m_prefixOutlet      ->setText(p->elementNamePrefix(QStringLiteral("outlet")));
    m_prefixRaingage    ->setText(p->elementNamePrefix(QStringLiteral("raingage")));
    m_prefixSubcatchment->setText(p->elementNamePrefix(QStringLiteral("subcatchment")));
}

void PreferencesDialog::writeToManager()
{
    auto *p = PreferencesManager::instance();

    LicenseAgreementDialog::setShowOnStartup(m_showLicenseOnStartupBox->isChecked());

    p->setClickTolerancePx(m_clickTolerancePxSpin->value());
    p->setDragThresholdPx(m_dragThresholdPxSpin->value());
    p->setClearSelectionOnMiss(m_clearOnMissBox->isChecked());

    if (m_pendingSelColorLink.isValid())
        p->setSelectionColor(QStringLiteral("link"),         m_pendingSelColorLink);
    if (m_pendingSelColorNode.isValid())
        p->setSelectionColor(QStringLiteral("node"),         m_pendingSelColorNode);
    if (m_pendingSelColorSubcatch.isValid())
        p->setSelectionColor(QStringLiteral("subcatchment"), m_pendingSelColorSubcatch);
    if (m_pendingSelColorGage.isValid())
        p->setSelectionColor(QStringLiteral("gage"),         m_pendingSelColorGage);

    p->setDefaultTool(m_defaultToolCombo->currentData().toString());
    p->setDefaultCrsAuthority(m_crsAuthorityEdit->text().trimmed());
    p->setDefaultCrsCode(m_crsCodeSpin->value());

    p->setLabelLodM11Min(m_labelLodSpin->value());
    if (m_pendingLinkColorConduit.isValid())
        p->setLinkColor(QStringLiteral("conduit"), m_pendingLinkColorConduit);
    if (m_pendingLinkColorPump.isValid())
        p->setLinkColor(QStringLiteral("pump"), m_pendingLinkColorPump);
    if (m_pendingLinkColorOrifice.isValid())
        p->setLinkColor(QStringLiteral("orifice"), m_pendingLinkColorOrifice);
    if (m_pendingLinkColorWeir.isValid())
        p->setLinkColor(QStringLiteral("weir"), m_pendingLinkColorWeir);
    if (m_pendingLinkColorOutlet.isValid())
        p->setLinkColor(QStringLiteral("outlet"), m_pendingLinkColorOutlet);

    p->setProgressTickMs(m_progressTickMsSpin->value());

    // Scale Bar
    if (m_pendingScaleBarColor.isValid())
        p->setScaleBarPenColor(m_pendingScaleBarColor);
    p->setScaleBarPenWidth(m_scaleBarPenWidthSpin->value());
    p->setScaleBarPenStyle(m_scaleBarPenStyleCombo->currentData().toInt());
    p->setScaleBarFontFamily(m_pendingScaleBarFont.family());
    p->setScaleBarFontSize(m_pendingScaleBarFont.pointSize());
    p->setScaleBarUnits(m_scaleBarUnitsCombo->currentData().toInt());
    p->setScaleBarPosition(m_scaleBarPositionCombo->currentData().toInt());
    p->setScaleBarMaxBarLength(m_scaleBarMaxBarLengthSpin->value());
    p->setScaleBarLabelDecimals(m_scaleBarLabelDecimalsSpin->value());
    p->setScaleBarCompactNotation(m_scaleBarCompactNotationBox->isChecked());

    // Measure Tool
    if (m_pendingMeasureLineColor.isValid())
        p->setMeasureLineColor(m_pendingMeasureLineColor);
    p->setMeasureLabelFontFamily(m_pendingMeasureLabelFont.family());
    p->setMeasureLabelFontSize(m_pendingMeasureLabelFont.pointSize());
    p->setMeasureLabelDecimals(m_measureLabelDecimalsSpin->value());
    if (m_pendingMeasureFillColor.isValid())
        p->setMeasureFillColor(m_pendingMeasureFillColor);
    p->setMeasureFillOpacity(m_measureFillOpacitySpin->value());

    // Naming prefixes
    auto savePrefix = [&](QLineEdit *ed, const QString &kind) {
        const QString t = ed->text().trimmed();
        if (!t.isEmpty()) p->setElementNamePrefix(kind, t);
    };
    savePrefix(m_prefixJunction,     QStringLiteral("junction"));
    savePrefix(m_prefixOutfall,      QStringLiteral("outfall"));
    savePrefix(m_prefixStorage,      QStringLiteral("storage"));
    savePrefix(m_prefixDivider,      QStringLiteral("divider"));
    savePrefix(m_prefixConduit,      QStringLiteral("conduit"));
    savePrefix(m_prefixPump,         QStringLiteral("pump"));
    savePrefix(m_prefixOrifice,      QStringLiteral("orifice"));
    savePrefix(m_prefixWeir,         QStringLiteral("weir"));
    savePrefix(m_prefixOutlet,       QStringLiteral("outlet"));
    savePrefix(m_prefixRaingage,     QStringLiteral("raingage"));
    savePrefix(m_prefixSubcatchment, QStringLiteral("subcatchment"));
}

void PreferencesDialog::onApply()
{
    writeToManager();
}

void PreferencesDialog::onAccept()
{
    writeToManager();
    accept();
}

void PreferencesDialog::onResetToDefaults()
{
    // Defaults mirror the `k…` constants in preferencesmanager.cpp — not
    // shared via a header because the dialog also wants to show them as
    // live values after a reset-click. Keep in sync if you change the
    // compiled-in defaults on the manager side.
    m_showLicenseOnStartupBox->setChecked(true);

    m_clickTolerancePxSpin->setValue(16);
    m_dragThresholdPxSpin->setValue(8);
    m_clearOnMissBox->setChecked(true);

    const int selIdx = m_defaultToolCombo->findData(QStringLiteral("Select"));
    m_defaultToolCombo->setCurrentIndex(selIdx >= 0 ? selIdx : 0);
    m_crsAuthorityEdit->setText(QStringLiteral("EPSG"));
    m_crsCodeSpin->setValue(4326);

    m_labelLodSpin->setValue(0.5);

    auto resetColorButton = [](QPushButton *btn, QColor &held, const QColor &c) {
        held = c;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };
    resetColorButton(m_linkColorConduit, m_pendingLinkColorConduit, QColor(50, 50, 200));
    resetColorButton(m_linkColorPump,    m_pendingLinkColorPump,    QColor(Qt::red));
    resetColorButton(m_linkColorOrifice, m_pendingLinkColorOrifice, QColor(200, 150, 0));
    resetColorButton(m_linkColorWeir,    m_pendingLinkColorWeir,    QColor(0, 180, 100));
    resetColorButton(m_linkColorOutlet,  m_pendingLinkColorOutlet,  QColor(120, 90, 40));

    m_progressTickMsSpin->setValue(1000);

    // Scale bar defaults
    {
        const QColor black = Qt::black;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: white; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(black.name(QColor::HexRgb));
        m_scaleBarColorBtn->setStyleSheet(css);
        m_scaleBarColorBtn->setText(black.name(QColor::HexRgb).toUpper());
        m_pendingScaleBarColor = black;
    }
    m_scaleBarPenWidthSpin->setValue(2);
    m_scaleBarPenStyleCombo->setCurrentIndex(0);    // Solid
    m_pendingScaleBarFont = QFont(QStringLiteral("sans-serif"), 8);
    m_scaleBarFontBtn->setText(QStringLiteral("sans-serif, 8pt"));
    m_scaleBarUnitsCombo->setCurrentIndex(0);       // Auto
    m_scaleBarPositionCombo->setCurrentIndex(0);    // Bottom Left
    m_scaleBarMaxBarLengthSpin->setValue(100);
    m_scaleBarLabelDecimalsSpin->setValue(-1);
    m_scaleBarCompactNotationBox->setChecked(false);

    // Measure Tool defaults
    resetColorButton(m_measureLineColorBtn,  m_pendingMeasureLineColor,  Qt::red);
    resetColorButton(m_measureFillColorBtn,  m_pendingMeasureFillColor,  QColor(100, 149, 237));
    m_pendingMeasureLabelFont = QFont(QStringLiteral("sans-serif"), 8);
    m_measureLabelFontBtn->setText(QStringLiteral("sans-serif, 8pt"));
    m_measureLabelDecimalsSpin->setValue(2);
    m_measureFillOpacitySpin->setValue(30);

    // Naming prefix defaults
    m_prefixJunction    ->setText(QStringLiteral("J"));
    m_prefixOutfall     ->setText(QStringLiteral("O"));
    m_prefixStorage     ->setText(QStringLiteral("S"));
    m_prefixDivider     ->setText(QStringLiteral("D"));
    m_prefixConduit     ->setText(QStringLiteral("C"));
    m_prefixPump        ->setText(QStringLiteral("Pu"));
    m_prefixOrifice     ->setText(QStringLiteral("Or"));
    m_prefixWeir        ->setText(QStringLiteral("W"));
    m_prefixOutlet      ->setText(QStringLiteral("Ou"));
    m_prefixRaingage    ->setText(QStringLiteral("RG"));
    m_prefixSubcatchment->setText(QStringLiteral("Sub"));
}

// ---------------------------------------------------------------------------
// Naming page
// ---------------------------------------------------------------------------

QWidget *PreferencesDialog::buildNamingPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto mkEdit = [&](const QString &defaultText) -> QLineEdit * {
        auto *ed = new QLineEdit(page);
        ed->setPlaceholderText(defaultText);
        ed->setMaximumWidth(120);
        return ed;
    };

    f->addRow(new QLabel(tr(
        "<b>Element name prefixes</b><br>"
        "Auto-generated names use these prefixes followed by a sequential "
        "number (e.g. J1, J2, …). Changing the prefix here takes effect "
        "immediately for the next placed element."
        ), page));

    m_prefixJunction     = mkEdit(QStringLiteral("J"));
    m_prefixOutfall      = mkEdit(QStringLiteral("O"));
    m_prefixStorage      = mkEdit(QStringLiteral("S"));
    m_prefixDivider      = mkEdit(QStringLiteral("D"));
    m_prefixConduit      = mkEdit(QStringLiteral("C"));
    m_prefixPump         = mkEdit(QStringLiteral("Pu"));
    m_prefixOrifice      = mkEdit(QStringLiteral("Or"));
    m_prefixWeir         = mkEdit(QStringLiteral("W"));
    m_prefixOutlet       = mkEdit(QStringLiteral("Ou"));
    m_prefixRaingage     = mkEdit(QStringLiteral("RG"));
    m_prefixSubcatchment = mkEdit(QStringLiteral("Sub"));

    f->addRow(tr("Junction:"),      m_prefixJunction);
    f->addRow(tr("Outfall:"),       m_prefixOutfall);
    f->addRow(tr("Storage:"),       m_prefixStorage);
    f->addRow(tr("Divider:"),       m_prefixDivider);
    f->addRow(tr("Conduit:"),       m_prefixConduit);
    f->addRow(tr("Pump:"),          m_prefixPump);
    f->addRow(tr("Orifice:"),       m_prefixOrifice);
    f->addRow(tr("Weir:"),          m_prefixWeir);
    f->addRow(tr("Outlet:"),        m_prefixOutlet);
    f->addRow(tr("Rain Gage:"),     m_prefixRaingage);
    f->addRow(tr("Subcatchment:"),  m_prefixSubcatchment);

    return page;
}
