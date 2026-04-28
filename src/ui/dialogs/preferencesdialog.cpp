/*!
 * \file   preferencesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/preferencesdialog.h"

#include "core/preferencesmanager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
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
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildSelectionPage());
    m_pages->addWidget(buildCanvasPage());
    m_pages->addWidget(buildRenderingPage());
    m_pages->addWidget(buildSimulationPage());
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
    f->addRow(new QLabel(tr("General settings (more options ship in a "
                            "follow-up — recent files cap, welcome on "
                            "startup, and auto-save interval move here)."),
                         page));
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
    auto *f    = new QFormLayout(page);

    m_labelLodSpin = new QDoubleSpinBox(page);
    m_labelLodSpin->setRange(0.01, 100.0);
    m_labelLodSpin->setSingleStep(0.1);
    m_labelLodSpin->setDecimals(2);
    m_labelLodSpin->setToolTip(tr(
        "Minimum view-transform scale (m11) at which labels are drawn. "
        "Higher values hide labels sooner when zooming out; lower "
        "values draw labels even at coarse zoom (at a performance cost)."));
    f->addRow(tr("Label zoom-out threshold (m11)"), m_labelLodSpin);

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

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

void PreferencesDialog::readFromManager()
{
    auto *p = PreferencesManager::instance();

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

    m_progressTickMsSpin->setValue(p->progressTickMs());
}

void PreferencesDialog::writeToManager()
{
    auto *p = PreferencesManager::instance();

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

    p->setProgressTickMs(m_progressTickMsSpin->value());
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
    m_clickTolerancePxSpin->setValue(16);
    m_dragThresholdPxSpin->setValue(8);
    m_clearOnMissBox->setChecked(true);

    const int selIdx = m_defaultToolCombo->findData(QStringLiteral("Select"));
    m_defaultToolCombo->setCurrentIndex(selIdx >= 0 ? selIdx : 0);
    m_crsAuthorityEdit->setText(QStringLiteral("EPSG"));
    m_crsCodeSpin->setValue(4326);

    m_labelLodSpin->setValue(0.5);

    m_progressTickMsSpin->setValue(1000);
}
