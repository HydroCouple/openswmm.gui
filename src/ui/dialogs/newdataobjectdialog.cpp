/*!
 * \file   newdataobjectdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/newdataobjectdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

namespace {

QString titleFor(SWMMModelLayer::DataCategory dc)
{
    using L = SWMMModelLayer;
    switch (dc) {
    case L::DataCurves:      return QObject::tr("New Curve");
    case L::DataTimeSeries:  return QObject::tr("New Time Series");
    case L::DataPatterns:    return QObject::tr("New Time Pattern");
    case L::DataLIDControls: return QObject::tr("New LID Control");
    case L::DataPollutants:  return QObject::tr("New Pollutant");
    case L::DataLandUses:    return QObject::tr("New Land Use");
    case L::DataAquifers:    return QObject::tr("New Aquifer");
    case L::DataSnowpacks:   return QObject::tr("New Snowpack");
    case L::DataControls:    return QObject::tr("New Control Rule");
    case L::DataTransects:   return QObject::tr("New Transect");
    case L::DataHydrographs: return QObject::tr("New Unit Hydrograph");
    case L::DataStreets:     return QObject::tr("New Street");
    case L::DataInlets:      return QObject::tr("New Inlet");
    default:                 return QObject::tr("New Data Object");
    }
}

} // namespace

NewDataObjectDialog::NewDataObjectDialog(SWMMModelLayer::DataCategory dc,
                                          SWMMModelLayer *layer,
                                          QWidget *parent)
    : QDialog(parent), m_category(dc), m_layer(layer)
{
    setWindowTitle(titleFor(dc));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    root->addLayout(form);

    // Name row — auto-suggested unique default that the user may overwrite.
    m_nameEdit = new QLineEdit(this);
    if (m_layer) m_nameEdit->setText(m_layer->suggestUniqueDataObjectName(dc));
    m_nameEdit->selectAll();
    form->addRow(tr("Name:"), m_nameEdit);

    using L = SWMMModelLayer;
    switch (dc) {
    case L::DataCurves: {
        m_curveTypeCombo = new QComboBox(this);
        // Engine canonical encoding (openswmm_tables.h):
        //  0 STORAGE | 1 DIVERSION | 2 TIDAL | 3 RATING | 4 CONTROL |
        //  5 SHAPE  | 6..11 PUMP1..PUMP5  → 11 entries.
        m_curveTypeCombo->addItem(tr("Storage"),    0);
        m_curveTypeCombo->addItem(tr("Diversion"),  1);
        m_curveTypeCombo->addItem(tr("Tidal"),      2);
        m_curveTypeCombo->addItem(tr("Rating"),     3);
        m_curveTypeCombo->addItem(tr("Control"),    4);
        m_curveTypeCombo->addItem(tr("Shape"),      5);
        m_curveTypeCombo->addItem(tr("Pump 1"),     6);
        m_curveTypeCombo->addItem(tr("Pump 2"),     7);
        m_curveTypeCombo->addItem(tr("Pump 3"),     8);
        m_curveTypeCombo->addItem(tr("Pump 4"),     9);
        m_curveTypeCombo->addItem(tr("Pump 5"),    10);
        form->addRow(tr("Curve Type:"), m_curveTypeCombo);
        break;
    }
    case L::DataTimeSeries: {
        m_tsSourceCombo = new QComboBox(this);
        m_tsSourceCombo->addItem(tr("Inline Table"), QStringLiteral("table"));
        m_tsSourceCombo->addItem(tr("External File"), QStringLiteral("file"));
        form->addRow(tr("Source:"), m_tsSourceCombo);
        break;
    }
    case L::DataPatterns: {
        m_patternTypeCombo = new QComboBox(this);
        // Engine canonical: 0 MONTHLY | 1 DAILY | 2 HOURLY | 3 WEEKEND
        m_patternTypeCombo->addItem(tr("Monthly"), 0);
        m_patternTypeCombo->addItem(tr("Daily"),   1);
        m_patternTypeCombo->addItem(tr("Hourly"),  2);
        m_patternTypeCombo->addItem(tr("Weekend"), 3);
        form->addRow(tr("Pattern Type:"), m_patternTypeCombo);
        break;
    }
    case L::DataLIDControls: {
        m_lidTypeCombo = new QComboBox(this);
        // Engine canonical: 0 BC | 1 RG | 2 GR | 3 IT | 4 PP | 5 RB | 6 RD | 7 VS
        m_lidTypeCombo->addItem(tr("Bio-Retention Cell"), 0);
        m_lidTypeCombo->addItem(tr("Rain Garden"),        1);
        m_lidTypeCombo->addItem(tr("Green Roof"),         2);
        m_lidTypeCombo->addItem(tr("Infiltration Trench"),3);
        m_lidTypeCombo->addItem(tr("Permeable Pavement"), 4);
        m_lidTypeCombo->addItem(tr("Rain Barrel"),        5);
        m_lidTypeCombo->addItem(tr("Roof Drain"),         6);
        m_lidTypeCombo->addItem(tr("Vegetative Swale"),   7);
        form->addRow(tr("LID Type:"), m_lidTypeCombo);
        break;
    }
    case L::DataPollutants: {
        m_pollutantUnitsCombo = new QComboBox(this);
        m_pollutantUnitsCombo->addItem(tr("mg/L"),   0);
        m_pollutantUnitsCombo->addItem(tr("µg/L"),   1);
        m_pollutantUnitsCombo->addItem(tr("Count/L"),2);
        form->addRow(tr("Units:"), m_pollutantUnitsCombo);
        break;
    }
    case L::DataInlets: {
        m_inletTypeCombo = new QComboBox(this);
        m_inletTypeCombo->addItem(tr("Grate"),   QStringLiteral("GRATE"));
        m_inletTypeCombo->addItem(tr("Curb"),    QStringLiteral("CURB"));
        m_inletTypeCombo->addItem(tr("Slotted"), QStringLiteral("SLOTTED"));
        m_inletTypeCombo->addItem(tr("Custom"),  QStringLiteral("CUSTOM"));
        form->addRow(tr("Inlet Type:"), m_inletTypeCombo);
        break;
    }
    case L::DataControls: {
        m_ruleSkeletonCombo = new QComboBox(this);
        m_ruleSkeletonCombo->addItem(tr("Empty (write your own)"),
                                       QStringLiteral("empty"));
        m_ruleSkeletonCombo->addItem(tr("Pump on/off (depth)"),
                                       QStringLiteral("pump"));
        m_ruleSkeletonCombo->addItem(tr("Orifice setting (depth)"),
                                       QStringLiteral("orifice"));
        m_ruleSkeletonCombo->addItem(tr("Weir bypass (flow)"),
                                       QStringLiteral("weir"));
        form->addRow(tr("Skeleton:"), m_ruleSkeletonCombo);
        break;
    }
    case L::DataHydrographs: {
        m_uhRainGageCombo = new QComboBox(this);
        // Populate from existing rain gages in the project so the
        // common case (most users have 1 gage) is one click.
        if (m_layer && m_layer->engine()) {
            SWMM_Engine eng = m_layer->engine();
            const int n = swmm_gage_count(eng);
            for (int i = 0; i < n; ++i) {
                const char *id = swmm_gage_id(eng, i);
                if (id) m_uhRainGageCombo->addItem(QString::fromUtf8(id));
            }
        }
        if (m_uhRainGageCombo->count() == 0)
            m_uhRainGageCombo->addItem(tr("(no rain gages defined)"));
        form->addRow(tr("Rain Gage:"), m_uhRainGageCombo);

        m_uhResponseCombo = new QComboBox(this);
        m_uhResponseCombo->addItem(tr("Short"),  0);
        m_uhResponseCombo->addItem(tr("Medium"), 1);
        m_uhResponseCombo->addItem(tr("Long"),   2);
        form->addRow(tr("Initial Response:"), m_uhResponseCombo);
        break;
    }
    case L::DataLandUses:
    case L::DataAquifers:
    case L::DataSnowpacks:
    case L::DataTransects:
    case L::DataStreets:
        // Name-only categories — no per-creation options.
        break;
    default:
        break;
    }

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Disable OK when the name field is empty (prevents the engine's
    // empty-id rejection from biting the user at save time).
    auto refreshOk = [this]() {
        m_buttonBox->button(QDialogButtonBox::Ok)
            ->setEnabled(!m_nameEdit->text().trimmed().isEmpty());
    };
    connect(m_nameEdit, &QLineEdit::textChanged, this, refreshOk);
    refreshOk();

    root->addWidget(m_buttonBox);
    resize(360, sizeHint().height());
}

QVariantMap NewDataObjectDialog::collectOptions() const
{
    QVariantMap out;
    using L = SWMMModelLayer;
    switch (m_category) {
    case L::DataCurves:
        if (m_curveTypeCombo)
            out.insert(QStringLiteral("curveType"),
                       m_curveTypeCombo->currentData());
        break;
    case L::DataTimeSeries:
        if (m_tsSourceCombo)
            out.insert(QStringLiteral("source"),
                       m_tsSourceCombo->currentData());
        break;
    case L::DataPatterns:
        if (m_patternTypeCombo)
            out.insert(QStringLiteral("patternType"),
                       m_patternTypeCombo->currentData());
        break;
    case L::DataLIDControls:
        if (m_lidTypeCombo)
            out.insert(QStringLiteral("lidType"),
                       m_lidTypeCombo->currentData());
        break;
    case L::DataPollutants:
        if (m_pollutantUnitsCombo)
            out.insert(QStringLiteral("units"),
                       m_pollutantUnitsCombo->currentData());
        break;
    case L::DataInlets:
        if (m_inletTypeCombo)
            out.insert(QStringLiteral("inletType"),
                       m_inletTypeCombo->currentData());
        break;
    case L::DataControls:
        if (m_ruleSkeletonCombo)
            out.insert(QStringLiteral("skeleton"),
                       m_ruleSkeletonCombo->currentData());
        break;
    case L::DataHydrographs:
        if (m_uhRainGageCombo)
            out.insert(QStringLiteral("rainGage"),
                       m_uhRainGageCombo->currentText());
        if (m_uhResponseCombo)
            out.insert(QStringLiteral("response"),
                       m_uhResponseCombo->currentData());
        break;
    default:
        break;
    }
    return out;
}

std::optional<NewObjectSpec>
NewDataObjectDialog::getNew(SWMMModelLayer::DataCategory dc,
                              SWMMModelLayer *layer, QWidget *parent)
{
    NewDataObjectDialog dlg(dc, layer, parent);
    if (dlg.exec() != QDialog::Accepted) return std::nullopt;
    const QString name = dlg.m_nameEdit->text().trimmed();
    if (name.isEmpty()) return std::nullopt;
    return NewObjectSpec{dc, name, dlg.collectOptions()};
}
