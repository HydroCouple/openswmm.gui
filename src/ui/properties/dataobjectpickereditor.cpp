/*!
 * \file   dataobjectpickereditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/dataobjectpickereditor.h"

#include "curve/curveregistry.h"
#include "layers/swmmmodellayer.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/widgets/labeledcontrols.h"

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QToolButton>

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_tables.h>

DataObjectPickerEditor::DataObjectPickerEditor(QWidget *parent)
    : QWidget(parent), m_combo(new LabeledPickerCombo(QString(), this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_combo);
    setFocusProxy(m_combo->combo());

    connect(m_combo, &LabeledPickerCombo::currentTextChanged,
            this, &DataObjectPickerEditor::onComboTextChanged);
    connect(m_combo, &LabeledPickerCombo::pickerClicked,
            this, &DataObjectPickerEditor::onPickerClicked);
}

void DataObjectPickerEditor::setValue(const DataObjectRef &ref)
{
    m_ref = ref;
    repopulate();
}

void DataObjectPickerEditor::repopulate()
{
    if (!m_combo) return;

    QStringList items;
    if (m_ref.layer && m_ref.engine) {
        switch (m_ref.kind) {
        case DataObjectRef::TidalCurve:
            // engine table type 6 = CURVE_TIDAL (per openswmm_tables.h comment)
            items = m_ref.layer->tableIdsOfType(6);
            break;
        case DataObjectRef::AnyCurve:
            items = m_ref.layer->tableIdsOfType(-1);
            break;
        case DataObjectRef::TimeSeries:
            items = m_ref.layer->tableIdsOfType(0);
            break;
        case DataObjectRef::Pattern: {
            // Patterns live in a separate engine array; filter by typeLock
            // when supplied (-1 means "any kind").
            const int n = swmm_pattern_count(m_ref.engine);
            for (int i = 0; i < n; ++i) {
                int t = -1;
                if (m_ref.typeLock >= 0 &&
                    swmm_pattern_get_type(m_ref.engine, i, &t) == SWMM_OK &&
                    t != m_ref.typeLock)
                    continue;
                if (const char *id = swmm_pattern_id(m_ref.engine, i))
                    if (*id) items << QString::fromUtf8(id);
            }
            break;
        }
        case DataObjectRef::UnitHydrograph: {
            // UH groups are surfaced through the DataHydrographs category.
            const int n = m_ref.layer->dataObjectCount(SWMMModelLayer::DataHydrographs);
            for (int i = 0; i < n; ++i) {
                const QString nm = m_ref.layer->dataObjectNameAt(
                    SWMMModelLayer::DataHydrographs, i);
                if (!nm.isEmpty()) items << nm;
            }
            break;
        }
        case DataObjectRef::Pollutant: {
            const int n = m_ref.layer->dataObjectCount(SWMMModelLayer::DataPollutants);
            for (int i = 0; i < n; ++i) {
                const QString nm = m_ref.layer->dataObjectNameAt(
                    SWMMModelLayer::DataPollutants, i);
                if (!nm.isEmpty()) items << nm;
            }
            break;
        }
        case DataObjectRef::RainGage: {
            // Rain gages are spatial objects (no DataCategory entry); ask
            // the layer directly. swmm_gage_count + _id round-trip through
            // SWMMModelLayer::rainGageNames() (added alongside this slice
            // if not present) — fall back to engine APIs inline if missing.
            const int n = swmm_gage_count(m_ref.engine);
            for (int i = 0; i < n; ++i) {
                if (const char *id = swmm_gage_id(m_ref.engine, i))
                    if (*id) items << QString::fromUtf8(id);
            }
            break;
        }
        }
    }

    QSignalBlocker block(m_combo->combo());
    m_suppressTextChange = true;
    m_combo->setItems(items, m_ref.currentName);
    m_suppressTextChange = false;
}

void DataObjectPickerEditor::onComboTextChanged(const QString &text)
{
    if (m_suppressTextChange) return;
    m_ref.currentName = text;
    emit valueChanged();
}

void DataObjectPickerEditor::onPickerClicked()
{
    if (!m_ref.layer) return;

    // Rain gages have no comprehensive editor and no DataCategory entry.
    // The combo still works for selection; the browse button surfaces a
    // brief note so the click isn't silent.
    if (m_ref.kind == DataObjectRef::RainGage) {
        QMessageBox::information(this, tr("Rain Gage"),
            tr("Rain gages are edited from the Object Browser "
               "(Rain Gages → select → Attribute panel). No dedicated "
               "editor dialog."));
        return;
    }

    SWMMModelLayer::DataCategory dc = SWMMModelLayer::DataTimeSeries;
    switch (m_ref.kind) {
    case DataObjectRef::TidalCurve:
    case DataObjectRef::AnyCurve:       dc = SWMMModelLayer::DataCurves;      break;
    case DataObjectRef::TimeSeries:     dc = SWMMModelLayer::DataTimeSeries;  break;
    case DataObjectRef::Pattern:        dc = SWMMModelLayer::DataPatterns;    break;
    case DataObjectRef::UnitHydrograph: dc = SWMMModelLayer::DataHydrographs; break;
    case DataObjectRef::Pollutant:      dc = SWMMModelLayer::DataPollutants;  break;
    case DataObjectRef::RainGage:       /* handled above */                   break;
    }

    // Slice BM.0-Add-New (2026-05-24) — gap categories (Transects / LID /
    // Pollutants / etc.) carry only a tooltip in the registry; surface it.
    if (!ObjectBrowserPanel::hasComplexEditor(dc)) {
        QMessageBox::information(this, tr("Create New"),
            ObjectBrowserPanel::gapTooltipFor(dc));
        return;
    }

    // DA.4-step2 — dispatch through the real editor for every shipped
    // category. Three of four expose synchronous `pickXxx` factories that
    // return the chosen name (mirroring `HydrographGroupEditor::pickGroup`);
    // CurveEditorDialog ships only `createNew` (non-modal) today, so the
    // curve flow opens the dialog non-modally and refreshes the combo on
    // close — the user picks the new entry from the combo manually until
    // a `pickCurve` is added (small follow-up).
    QString chosen;
    switch (dc) {
    case SWMMModelLayer::DataHydrographs:
        chosen = HydrographGroupEditor::pickGroup(
            m_ref.layer, m_ref.currentName, this);
        break;

    case SWMMModelLayer::DataPatterns: {
        using openswmmvis::pattern::PatternRegistry;
        using openswmmvis::ui::PatternEditorDialog;
        auto *reg = qobject_cast<PatternRegistry *>(m_ref.layer->ensurePatternRegistry());
        if (!reg) return;
        chosen = PatternEditorDialog::pickPattern(
            reg, /*undoStack=*/nullptr, m_ref.currentName, this);
        break;
    }

    case SWMMModelLayer::DataTimeSeries: {
        using openswmmvis::timeseries::TimeseriesRegistry;
        using openswmmvis::ui::TimeseriesEditorDialog;
        auto *reg = qobject_cast<TimeseriesRegistry *>(m_ref.layer->ensureTimeseriesRegistry());
        if (!reg) return;
        chosen = TimeseriesEditorDialog::pickTimeseries(
            reg, /*undoStack=*/nullptr, m_ref.currentName, this);
        // Flush any newly-created provider out to the engine so the
        // adapter's setter (which looks up by engine table index) can
        // resolve the name.
        if (!chosen.isEmpty()) reg->saveToEngine();
        break;
    }

    case SWMMModelLayer::DataCurves: {
        using openswmmvis::curve::CurveRegistry;
        using openswmmvis::ui::CurveEditorDialog;
        auto *reg = qobject_cast<CurveRegistry *>(m_ref.layer->ensureCurveRegistry());
        if (!reg) return;
        chosen = CurveEditorDialog::pickCurve(
            reg, /*undoStack=*/nullptr, m_ref.currentName, this);
        break;
    }

    default:
        return;
    }

    if (chosen.isEmpty()) return;
    m_ref.currentName = chosen;
    repopulate();
    emit valueChanged();
}
