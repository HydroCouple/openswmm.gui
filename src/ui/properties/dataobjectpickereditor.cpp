/*!
 * \file   dataobjectpickereditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/dataobjectpickereditor.h"

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/panels/objectbrowserpanel.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include "ui/widgets/labeledcontrols.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QToolButton>

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

    SWMMModelLayer::DataCategory dc = SWMMModelLayer::DataTimeSeries;
    switch (m_ref.kind) {
    case DataObjectRef::TidalCurve:
    case DataObjectRef::AnyCurve:       dc = SWMMModelLayer::DataCurves;      break;
    case DataObjectRef::TimeSeries:     dc = SWMMModelLayer::DataTimeSeries;  break;
    case DataObjectRef::Pattern:        dc = SWMMModelLayer::DataPatterns;    break;
    case DataObjectRef::UnitHydrograph: dc = SWMMModelLayer::DataHydrographs; break;
    }

    // Slice BM.0-Add-New (2026-05-24) — gap categories (Curves / Patterns
    // / Transects / etc.) don't have a complex editor yet; surface the
    // gap tooltip directing the user to the future editor slice.
    if (!ObjectBrowserPanel::hasComplexEditor(dc)) {
        QMessageBox::information(this, tr("Create New"),
            ObjectBrowserPanel::gapTooltipFor(dc));
        return;
    }

    // DA.4-step2 — dispatch through the real editor for categories that
    // already ship one.
    if (dc == SWMMModelLayer::DataHydrographs) {
        // HydrographGroupEditor::pickGroup is purpose-built for this
        // exact entry point ("Use this from the RDII picker's browse
        // button" per its docstring). Synchronous, returns the chosen
        // name on accept (Create + Apply / OK), empty on Cancel. Supports
        // both create-new (currentName empty) and edit-existing.
        const QString chosen = HydrographGroupEditor::pickGroup(
            m_ref.layer, m_ref.currentName, this);
        if (chosen.isEmpty()) return;
        m_ref.currentName = chosen;
        repopulate();
        emit valueChanged();
        return;
    }

    // DataTimeSeries — TimeseriesEditorDialog::createNew is non-modal
    // and binds to a `TimeseriesRegistry` currently owned privately by
    // ObjectBrowserPanel. Exposing it from the layer is a larger
    // refactor (tracked separately). Until then, the picker uses an
    // inline name prompt and commits via createDataObject; the user
    // populates data points by opening the new Time Series in the
    // Object Browser (double-click → full TimeseriesEditorDialog).
    const QString suggested = m_ref.layer->suggestUniqueDataObjectName(dc);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        tr("Create New Time Series"),
        tr("Name:\n\nA new (empty) time series will be created and assigned\n"
           "to this outfall. To enter time/value rows, open the new series\n"
           "from the Object Browser (Data Objects → Time Series →\n"
           "double-click) for the full editor."),
        QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QString err;
    if (!m_ref.layer->createDataObject(dc, name, /*options=*/{}, &err))
        return;

    m_ref.currentName = name;
    repopulate();
    emit valueChanged();
}
