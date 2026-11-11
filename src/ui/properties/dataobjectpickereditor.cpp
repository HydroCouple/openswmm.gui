/**
 * @file      dataobjectpickereditor.cpp
 * @brief     Custom cell editor widget for `DataObjectRef`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/dataobjectpickereditor.h"

#include "aquifer/aquiferregistry.h"
#include "curve/curveregistry.h"
#include "layers/swmmmodellayer.h"
#include "pattern/patternregistry.h"
#include "snowpack/snowpackregistry.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/aquifereditordialog.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/dialogs/streeteditordialog.h"
#include "transect/transectregistry.h"
#include "street/streetregistry.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "inlet/inletregistry.h"
#include "ui/dialogs/inleteditordialog.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/dialogs/pollutanteditordialog.h"
#include "ui/dialogs/lidcontroleditordialog.h"
#include "pollutant/pollutantregistry.h"
#include "lid/lidcontrolregistry.h"
#include "ui/widgets/labeledcontrols.h"
#include "map/mapcanvas.h"
#include "map/nodepicksession.h"

#include <QAbstractItemView>
#include <QMenu>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QToolButton>

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_infrastructure.h> // Inlet designs
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_nodes.h>         // SubcatchOutlet combined list
#include <openswmm/engine/openswmm_subcatchments.h> // SubcatchOutlet combined list

DataObjectPickerEditor::DataObjectPickerEditor(QWidget *parent)
    : QWidget(parent), m_combo(new LabeledPickerCombo(QString(), this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_combo);
    setFocusProxy(m_combo->combo());

    connect(m_combo, &LabeledPickerCombo::currentTextChanged, this,
            &DataObjectPickerEditor::onComboTextChanged);
    connect(m_combo, &LabeledPickerCombo::pickerClicked, this,
            &DataObjectPickerEditor::onPickerClicked);
}

void DataObjectPickerEditor::setValue(const DataObjectRef &ref)
{
    // Same re-entrancy as LinkEndpointComboBox::setValue: the view re-applies
    // the value from inside this editor's own currentTextChanged commit (the
    // engine write emits attributeChanged, the Properties dock refreshes, and
    // the view calls setEditorData on the open editor). When the list scope is
    // unchanged and the name is already offered, move the selection instead of
    // clearing and refilling the combo while its signal is still being emitted.
    // Outside that commit, rebuild so external additions/removals are visible.
    const bool sameScope = m_combo && m_ref.engine && ref.engine == m_ref.engine &&
                           ref.layer == m_ref.layer && ref.kind == m_ref.kind &&
                           ref.typeLock == m_ref.typeLock && m_combo->combo()->count() > 0;
    m_ref = ref;
    if (sameScope && m_committing) {
        const QString selected = selectedLabel();
        if (selected.isEmpty() || m_combo->combo()->findText(selected) >= 0) {
            const QSignalBlocker block(m_combo->combo());
            m_suppressTextChange = true;
            m_combo->setCurrentText(selected);
            m_suppressTextChange = false;
            return;
        }
    }
    repopulate();
}

QString DataObjectPickerEditor::selectedLabel() const
{
    QString selected = m_ref.currentName;
    if (m_ref.kind == DataObjectRef::SubcatchOutlet && !selected.isEmpty()) {
        const bool sub = m_ref.outletTarget == DataObjectRef::SubcatchmentOutlet ||
            (m_ref.outletTarget == DataObjectRef::Automatic &&
             swmm_node_index(m_ref.engine, selected.toUtf8().constData()) < 0);
        selected = (sub ? tr("Subcatchment: %1") : tr("Node: %1")).arg(selected);
    }
    return selected;
}

namespace
{
QStringList referenceChoices(const DataObjectRef &ref)
{
    QStringList items;
    if (ref.layer && ref.engine) {
        switch (ref.kind) {
        case DataObjectRef::Transect:
            for (int i = 0; i < swmm_transect_count(ref.engine); ++i)
                if (const char *name = swmm_transect_id(ref.engine, i))
                    items << QString::fromUtf8(name);
            break;
        case DataObjectRef::Street:
            for (int i = 0; i < swmm_street_count(ref.engine); ++i)
                if (const char *name = swmm_street_id(ref.engine, i))
                    items << QString::fromUtf8(name);
            break;
        case DataObjectRef::TidalCurve:
            // engine table type 6 = CURVE_TIDAL (per openswmm_tables.h comment)
            items = ref.layer->tableIdsOfType(6);
            break;
        case DataObjectRef::DiversionCurve:
            items = ref.layer->tableIdsOfType(2);
            break;
        case DataObjectRef::AnyCurve:
            items = ref.layer->tableIdsOfType(-1);
            break;
        case DataObjectRef::StorageCurve:
            // engine table type 1 = CURVE_STORAGE (per openswmm_tables.h comment)
            items = ref.layer->tableIdsOfType(1);
            break;
        case DataObjectRef::PumpCurve:
            // PUMP1..PUMP5. A storage or tidal curve on a pump is misread by
            // the engine, so only pump curves are offered.
            items = ref.layer->tableIdsOfTypes({7, 8, 9, 10, 11});
            break;
        case DataObjectRef::RatingCurve:
            items = ref.layer->tableIdsOfType(3); // CURVE_RATING
            break;
        case DataObjectRef::TimeSeries:
            items = ref.layer->tableIdsOfType(0);
            break;
        case DataObjectRef::Pattern: {
            // Patterns live in a separate engine array; filter by typeLock
            // when supplied (-1 means "any kind").
            const int n = swmm_pattern_count(ref.engine);
            for (int i = 0; i < n; ++i) {
                int t = -1;
                if (ref.typeLock >= 0 && swmm_pattern_get_type(ref.engine, i, &t) == SWMM_OK &&
                    t != ref.typeLock)
                    continue;
                if (const char *id = swmm_pattern_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            }
            break;
        }
        case DataObjectRef::UnitHydrograph: {
            // UH groups are surfaced through the DataHydrographs category.
            const int n = ref.layer->dataObjectCount(SWMMModelLayer::DataHydrographs);
            for (int i = 0; i < n; ++i) {
                const QString nm = ref.layer->dataObjectNameAt(SWMMModelLayer::DataHydrographs, i);
                if (!nm.isEmpty())
                    items << nm;
            }
            break;
        }
        case DataObjectRef::Pollutant: {
            const int n = ref.layer->dataObjectCount(SWMMModelLayer::DataPollutants);
            for (int i = 0; i < n; ++i) {
                const QString nm = ref.layer->dataObjectNameAt(SWMMModelLayer::DataPollutants, i);
                if (!nm.isEmpty())
                    items << nm;
            }
            break;
        }
        case DataObjectRef::RainGage: {
            // Rain gages are spatial objects (no DataCategory entry); ask
            // the layer directly. swmm_gage_count + _id round-trip through
            // SWMMModelLayer::rainGageNames() (added alongside this slice
            // if not present) — fall back to engine APIs inline if missing.
            const int n = swmm_gage_count(ref.engine);
            for (int i = 0; i < n; ++i) {
                if (const char *id = swmm_gage_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            }
            break;
        }
        case DataObjectRef::Node: {
            // Every node is a legal 1D↔2D coupling target. Sorted, matching
            // the mesh toolbar's coupled-node dropdown so the two surfaces
            // offer the same list.
            const int n = swmm_node_count(ref.engine);
            for (int i = 0; i < n; ++i)
                if (const char *id = swmm_node_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            items.sort(Qt::CaseInsensitive);
            break;
        }
        case DataObjectRef::Subcatchment: {
            // [OUTFALLS] RouteTo target. Sorted so the dropdown reads the
            // same way as the Subcatchments attribute table.
            const int n = swmm_subcatch_count(ref.engine);
            for (int i = 0; i < n; ++i)
                if (const char *id = swmm_subcatch_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            items.sort(Qt::CaseInsensitive);
            break;
        }
        case DataObjectRef::SubcatchOutlet: {
            // Combined outlet target list: every node, then every subcatchment.
            // The owning adapter resolves the picked name back to a node-outlet
            // vs. cascade-outlet engine write.
            // Virtual junctions are legal outlet targets too: a point
            // lateral is integrated at the zero-storage node (engine plan
            // VJ_LATERAL_INFLOW_PLAN_2026-09-04).
            const int nn = swmm_node_count(ref.engine);
            for (int i = 0; i < nn; ++i)
                if (const char *id = swmm_node_id(ref.engine, i))
                    if (*id)
                        items << DataObjectPickerEditor::tr("Node: %1").arg(QString::fromUtf8(id));
            const int ns = swmm_subcatch_count(ref.engine);
            for (int i = 0; i < ns; ++i)
                if (const char *id = swmm_subcatch_id(ref.engine, i))
                    if (*id)
                        items << DataObjectPickerEditor::tr("Subcatchment: %1")
                                     .arg(QString::fromUtf8(id));
            break;
        }
        case DataObjectRef::LidControl: {
            for (int i = 0; i < swmm_lid_count(ref.engine); ++i) {
                int type = -1;
                swmm_lid_get_type(ref.engine, i, &type);
                if (type == 8 || type == 0 || type == 1 || type == 3 || type == 4)
                    if (const char *id = swmm_lid_id(ref.engine, i))
                        items << QString::fromUtf8(id);
            }
            break;
        }
        case DataObjectRef::Snowpack: {
            for (int i = 0; i < swmm_snowpack_count(ref.engine); ++i)
                if (const char *id = swmm_snowpack_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            break;
        }
        case DataObjectRef::Aquifer: {
            // [AQUIFERS] live in their own engine array (no table type).
            const int n = swmm_aquifer_count(ref.engine);
            for (int i = 0; i < n; ++i)
                if (const char *id = swmm_aquifer_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            break;
        }
        case DataObjectRef::Inlet: {
            // [INLETS] designs. Shape compatibility (STREET vs drop) is
            // enforced by the engine on swmm_inlet_usage_set (rule 635); the
            // combo lists every design so a mis-set one is still visible.
            const int n = swmm_inlet_count(ref.engine);
            for (int i = 0; i < n; ++i)
                if (const char *id = swmm_inlet_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            break;
        }
        case DataObjectRef::CaptureNode: {
            // Engine rule 627: the capture node must not be a virtual or
            // inlet junction (they have no storage to receive the capture).
            const int n = swmm_node_count(ref.engine);
            for (int i = 0; i < n; ++i) {
                int isVirtual = 0;
                swmm_node_is_virtual(ref.engine, i, &isVirtual);
                if (isVirtual)
                    continue;
                if (const char *id = swmm_node_id(ref.engine, i))
                    if (*id)
                        items << QString::fromUtf8(id);
            }
            items.sort(Qt::CaseInsensitive);
            break;
        }
        }
    }

    return items;
}

using DC = SWMMModelLayer;
DC::DataCategory referenceCategory(DataObjectRef::Kind kind)
{
    using R = DataObjectRef;
    switch (kind) {
    case R::TidalCurve:
    case R::DiversionCurve:
    case R::AnyCurve:
    case R::PumpCurve:
    case R::RatingCurve:
    case R::StorageCurve:
        return DC::DataCurves;
    case R::TimeSeries:
        return DC::DataTimeSeries;
    case R::UnitHydrograph:
        return DC::DataHydrographs;
    case R::Pattern:
        return DC::DataPatterns;
    case R::Transect:
        return DC::DataTransects;
    case R::Street:
        return DC::DataStreets;
    case R::Pollutant:
        return DC::DataPollutants;
    case R::LidControl:
        return DC::DataLIDControls;
    case R::Aquifer:
        return DC::DataAquifers;
    case R::Snowpack:
        return DC::DataSnowpacks;
    case R::Inlet:
        return DC::DataInlets;
    case R::RainGage:
    case R::Node:
    case R::Subcatchment:
    case R::SubcatchOutlet:
    case R::CaptureNode:
        return DC::NumDataCategories;
    }
    return DC::NumDataCategories;
}

QString pickReference(const DataObjectRef &ref, QWidget *parent)
{
    using namespace openswmmvis;
    using namespace openswmmvis::ui;
    QPointer<SWMMModelLayer> layer(ref.layer);
    if (!layer || !ref.engine || ref.engine != layer->engine())
        return {};
    const auto cat = referenceCategory(ref.kind);
    const QString initial =
        ComprehensiveEditorRegistry::existingObjectName(cat, layer, ref.currentName, parent);
    if (!ref.currentName.isEmpty() && initial.isEmpty())
        return {};
    auto *stack = layer->editCanvas() ? layer->editCanvas()->undoStack() : nullptr;
    QString chosen;
    switch (cat) {
    case DC::DataCurves:
        chosen = CurveEditorDialog::pickCurve(
            qobject_cast<curve::CurveRegistry *>(layer->ensureCurveRegistry()), stack, initial,
            parent);
        break;
    case DC::DataTimeSeries:
        chosen = TimeseriesEditorDialog::pickTimeseries(
            qobject_cast<timeseries::TimeseriesRegistry *>(layer->ensureTimeseriesRegistry()),
            stack, initial, parent);
        break;
    case DC::DataPatterns:
        chosen = PatternEditorDialog::pickPattern(
            qobject_cast<pattern::PatternRegistry *>(layer->ensurePatternRegistry()), stack,
            initial, parent);
        break;
    case DC::DataHydrographs:
        chosen = HydrographGroupEditor::pickGroup(layer, initial, parent);
        break;
    case DC::DataTransects:
        chosen = TransectEditorDialog::pickTransect(
            qobject_cast<transect::TransectRegistry *>(layer->ensureTransectRegistry()), layer,
            stack, initial, parent);
        break;
    case DC::DataStreets:
        chosen = StreetEditorDialog::pickStreet(
            qobject_cast<street::StreetRegistry *>(layer->ensureStreetRegistry()), layer, initial,
            parent);
        break;
    case DC::DataInlets:
        chosen = InletEditorDialog::pickInlet(
            qobject_cast<inlet::InletRegistry *>(layer->ensureInletRegistry()), layer, stack,
            parent, ref.typeLock, initial);
        break;
    case DC::DataPollutants:
        chosen = PollutantEditorDialog::pickPollutant(
            qobject_cast<pollutant::PollutantRegistry *>(layer->ensurePollutantRegistry()), layer,
            initial, parent);
        break;
    case DC::DataSnowpacks:
        chosen = SnowpackEditorDialog::pickSnowpack(
            qobject_cast<snowpack::SnowpackRegistry *>(layer->ensureSnowpackRegistry()), layer,
            initial, parent);
        break;
    case DC::DataAquifers:
        chosen = AquiferEditorDialog::pickAquifer(
            qobject_cast<aquifer::AquiferRegistry *>(layer->ensureAquiferRegistry()), layer,
            initial, parent);
        break;
    case DC::DataLIDControls:
        chosen = LidControlEditorDialog::pickLidControl(
            qobject_cast<lid::LidControlRegistry *>(layer->ensureLidControlRegistry()), layer,
            initial, parent);
        break;
    default:
        return {};
    }
    if (!layer || layer->engine() != ref.engine || chosen.isEmpty() || chosen == ref.currentName)
        return {};
    if (!referenceChoices(ref).contains(chosen, Qt::CaseInsensitive)) {
        QMessageBox::information(parent, QObject::tr("Assign data object"),
                                 QObject::tr("“%1” is incompatible with this field.").arg(chosen));
        return {};
    }
    return chosen;
}
} // namespace

bool addDataObjectReferenceAction(QMenu &menu, QAbstractItemView *view, const QModelIndex &index)
{
    const auto ref = index.data(Qt::EditRole).value<DataObjectRef>();
    const QString title =
        ComprehensiveEditorRegistry::instance().editorTitle(referenceCategory(ref.kind));
    if (!ref.layer || title.isEmpty())
        return false;
    auto *action = menu.addAction(
        ref.currentName.isEmpty() ? QObject::tr("Open %1…").arg(title)
                                  : QObject::tr("Edit \"%1\" in %2…").arg(ref.currentName, title));
    QObject::connect(
        action, &QAction::triggered, view,
        [view = QPointer<QAbstractItemView>(view), target = QPersistentModelIndex(index), ref,
         layer = QPointer<SWMMModelLayer>(ref.layer)] {
            const auto stillCurrent = [&] {
                return view && layer && layer->engine() == ref.engine && target.isValid() &&
                       target.model() == view->model() &&
                       target.data(Qt::EditRole).value<DataObjectRef>() == ref;
            };
            if (!stillCurrent())
                return;
            const QString chosen = pickReference(ref, view->window());
            if (!chosen.isEmpty() && stillCurrent()) {
                auto updated = ref;
                updated.currentName = chosen;
                view->model()->setData(target, QVariant::fromValue(updated), Qt::EditRole);
            }
        });
    return true;
}

void DataObjectPickerEditor::repopulate()
{
    if (!m_combo)
        return;
    QStringList items = referenceChoices(m_ref);
    // A filtered curve list must still show the curve already assigned (an
    // older model may give a pump a curve of another type); without it the
    // combo falls back to "(none)" and the next commit would clear it.
    if ((m_ref.kind == DataObjectRef::PumpCurve || m_ref.kind == DataObjectRef::RatingCurve ||
         m_ref.kind == DataObjectRef::Node || m_ref.kind == DataObjectRef::TimeSeries ||
         m_ref.kind == DataObjectRef::AnyCurve) &&
        !m_ref.currentName.isEmpty() && !items.contains(m_ref.currentName))
        items << m_ref.currentName;

    QSignalBlocker block(m_combo->combo());
    m_suppressTextChange = true;
    m_combo->setItems(items, selectedLabel());
    m_suppressTextChange = false;
}

void DataObjectPickerEditor::applyPickedName(const QString &name)
{
    {
        const QSignalBlocker block(this);
        onComboTextChanged(name);
    }
    repopulate();
    emit valueChanged();
}

void DataObjectPickerEditor::onComboTextChanged(const QString &text)
{
    if (m_suppressTextChange)
        return;
    m_ref.currentName = text;
    if (m_ref.kind == DataObjectRef::SubcatchOutlet) {
        m_ref.outletTarget = DataObjectRef::Automatic;
        if (text.startsWith(tr("Node: "))) {
            m_ref.outletTarget = DataObjectRef::NodeOutlet;
            m_ref.currentName = text.mid(tr("Node: ").size());
        } else if (text.startsWith(tr("Subcatchment: "))) {
            m_ref.outletTarget = DataObjectRef::SubcatchmentOutlet;
            m_ref.currentName = text.mid(tr("Subcatchment: ").size());
        }
    }
    const QScopedValueRollback<bool> committing(m_committing, true);
    emit valueChanged();
}

void DataObjectPickerEditor::onPickerClicked()
{
    if (!m_ref.layer)
        return;

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
    // Nodes are created on the map, not from a data-object dialog — the combo
    // is the whole picker, so the browse button explains itself.
    if (m_ref.kind == DataObjectRef::Node) {
        QMessageBox::information(this, tr("Coupled Node"),
                                 tr("Pick an existing SWMM node from the dropdown, or leave it "
                                    "blank to remove the coupling. Nodes are added on the map."));
        return;
    }
    // Subcatchments are drawn on the map, so the combo is the whole picker.
    if (m_ref.kind == DataObjectRef::Subcatchment) {
        QMessageBox::information(this, tr("Subcatchment"),
                                 tr("Pick an existing subcatchment from the dropdown, or leave it "
                                    "blank to send the discharge out of the system. Subcatchments "
                                    "are drawn on the map."));
        return;
    }
    // Capture node: pick it on the map. The property grid closes this cell
    // editor the moment the canvas takes focus, so the session is parented
    // to the LAYER and writes the pick through the layer's inlet-usage edit;
    // the editor is only updated if it still exists.
    if (m_ref.kind == DataObjectRef::CaptureNode) {
        MapCanvas *canvas = m_ref.layer ? m_ref.layer->editCanvas() : nullptr;
        if (!canvas || m_ref.hostNodeIdx < 0) {
            QMessageBox::information(this, tr("Capture Node"),
                                     tr("Pick the node the inlet discharges to. Virtual and inlet "
                                        "junctions cannot receive an inlet's capture."));
            return;
        }
        auto *session = new NodePickSession(canvas, m_ref.layer);
        QPointer<DataObjectPickerEditor> self(this);
        QPointer<SWMMModelLayer> layer(m_ref.layer);
        const int host = m_ref.hostNodeIdx;
        connect(session, &NodePickSession::nodePicked, session,
                [session, self, layer, host](SWMMModelLayer *l, const QString &name, int idx) {
                    if (!layer || l != layer)
                        return;
                    // Engine rule 627: virtual / inlet junctions cannot
                    // receive the capture; nor can the inlet itself.
                    int isVirtual = 0;
                    swmm_node_is_virtual(layer->engine(), idx, &isVirtual);
                    if (isVirtual || idx == host)
                        return; // keep picking
                    SWMM_InletUsage u{};
                    if (layer->inletUsageFor(SWMM_INLET_HOST_NODE, host, &u) &&
                        u.capture_node_idx != idx) {
                        u.capture_node_idx = idx;
                        layer->pushInletUsageEdit(u);
                    }
                    if (self)
                        self->applyPickedName(name);
                    session->finish();
                    session->deleteLater();
                });
        connect(session, &NodePickSession::cancelled, session, &QObject::deleteLater);
        return;
    }
    // The outlet picker is pure selection over existing nodes/subcatchments —
    // no "create new" target, so the browse button is a no-op note.
    if (m_ref.kind == DataObjectRef::SubcatchOutlet) {
        QMessageBox::information(this, tr("Subcatchment Outlet"),
                                 tr("Pick an existing node or subcatchment as this "
                                    "subcatchment's outlet from the dropdown."));
        return;
    }

    const auto ref = m_ref;
    QPointer<DataObjectPickerEditor> self(this);
    const QString chosen = pickReference(ref, window());
    if (self && self->m_ref == ref && !chosen.isEmpty())
        self->applyPickedName(chosen);
}
