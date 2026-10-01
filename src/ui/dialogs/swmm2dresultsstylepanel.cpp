/*!
 * \file   swmm2dresultsstylepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  O2-1 / VS.8 — multi-tab styling panel for SWMM2DResultsLayer.
 *
 *         Pattern: each control is initialised from the style bag BEFORE
 *         its change-signal is connected, so construction never fires
 *         spurious writes; afterwards every edit applies live (the same
 *         apply-on-edit model the rest of the styling UI uses).
 */
#include "ui/dialogs/swmm2dresultsstylepanel.h"

#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/resultscalarsublayer.h"
#include "io/mesh2dvariableexport.h"
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QFileDialog>
#include <algorithm>
#include <cmath>
#include <limits>
#include "render/sublayers/scalarfillsublayer.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/dialogs/sublayertabhelpers.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/colorrampcombobox.h"
#include "ui/widgets/dashstylecombo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <functional>

namespace openswmmvis::ui {

namespace {

using OpenSWMM::Render::ContourBandStyle;
using OpenSWMM::Render::ISublayer;
using OpenSWMM::Render::IsolineStyle;
using OpenSWMM::Render::MeshEdgeStyle;
using OpenSWMM::Render::MeshNodeStyle;
using OpenSWMM::Render::VelocityVectorStyle;

// Spin factories, minimum widths, and the "Show <name>" + opacity header
// row now live in ui/dialogs/sublayertabhelpers.h, shared with the mesh
// styling panel.

/*! Colour-source block shared by the Depth and Contour-band tabs:
 *  [Two-colour gradient | Colour ramp] selector, ramp combo + invert,
 *  low/high colour buttons. `rampName`/`setRampName` etc. adapt the two
 *  style bags without a common base.
 *  NOTE: name-only binding — user CUSTOM ramps resolve through
 *  RasterColorRamp::builtin() and degrade to grayscale. If this block is
 *  revived, bind the full RasterColorRamp payload like
 *  ClassificationEditor::onRampChanged does (ClassificationScheme::
 *  setCustomRamp), not just the name. */
struct ColorSourceBindings {
    std::function<QString()>            rampName;
    std::function<void(const QString &)> setRampName;
    std::function<bool()>               invert;
    std::function<void(bool)>           setInvert;
    std::function<QColor()>             lowColor;
    std::function<void(const QColor &)> setLowColor;
    std::function<QColor()>             highColor;
    std::function<void(const QColor &)> setHighColor;
};

QGroupBox *makeColorSourceGroup(QWidget *parent, const ColorSourceBindings &b)
{
    auto *box  = new QGroupBox(QObject::tr("Colour"), parent);
    auto *form = new QFormLayout(box);

    const bool usingRamp = !b.rampName().isEmpty();

    auto *source = new QComboBox(box);
    source->addItem(QObject::tr("Colour ramp"));
    source->addItem(QObject::tr("Two-colour gradient"));
    source->setCurrentIndex(usingRamp ? 0 : 1);
    source->setMinimumWidth(kComboMinWidthPx);
    form->addRow(QObject::tr("Source:"), source);

    auto *ramp = new ColorRampComboBox(box);
    if (usingRamp) ramp->setCurrentRampByName(b.rampName());
    ramp->setMinimumWidth(kComboMinWidthPx);
    form->addRow(QObject::tr("Ramp:"), ramp);

    auto *invert = new QCheckBox(QObject::tr("Invert ramp"), box);
    invert->setChecked(b.invert());
    form->addRow(QString(), invert);

    auto *low = new ColorButton(box);
    low->setShowAlpha(true);
    low->setColor(b.lowColor());
    form->addRow(QObject::tr("Low colour:"), low);

    auto *high = new ColorButton(box);
    high->setShowAlpha(true);
    high->setColor(b.highColor());
    form->addRow(QObject::tr("High colour:"), high);

    auto applyEnabled = [source, ramp, low, high]() {
        const bool useRamp = (source->currentIndex() == 0);
        ramp->setEnabled(useRamp);
        low->setEnabled(!useRamp);
        high->setEnabled(!useRamp);
    };
    applyEnabled();

    QObject::connect(source, qOverload<int>(&QComboBox::currentIndexChanged), box,
        [b, ramp, applyEnabled](int idx) {
            applyEnabled();
            b.setRampName(idx == 0 ? ramp->currentText() : QString());
        });
    QObject::connect(ramp, &ColorRampComboBox::rampChanged, box,
        [b, source, ramp](const RasterColorRamp &) {
            if (source->currentIndex() == 0)
                b.setRampName(ramp->currentText());
        });
    QObject::connect(invert, &QCheckBox::toggled, box,
                     [b](bool on) { b.setInvert(on); });
    QObject::connect(low, &ColorButton::colorChanged, box,
                     [b](const QColor &c) { b.setLowColor(c); });
    QObject::connect(high, &ColorButton::colorChanged, box,
                     [b](const QColor &c) { b.setHighColor(c); });
    return box;
}

} // namespace

Swmm2DResultsStylePanel::Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    if (!m_layer) return;

    auto *water = new QGroupBox(tr("Water visibility"), this);
    auto *waterForm = new QFormLayout(water);
    auto *modelDepth = new QCheckBox(tr("Use model dry depth"), water);
    modelDepth->setObjectName(QStringLiteral("useModelThinFilmDepth"));
    auto *filmDepth = new QDoubleSpinBox(water);
    filmDepth->setObjectName(QStringLiteral("thinFilmDepth"));
    filmDepth->setDecimals(6);
    filmDepth->setRange(0.0, 1000.0);
    filmDepth->setSingleStep(0.001);
    filmDepth->setSuffix(tr(" m"));
    auto *showFilms = new QCheckBox(tr("Show thin films"), water);
    showFilms->setObjectName(QStringLiteral("showThinFilms"));
    auto *hint = new QLabel(tr("Hide water in cells that are shallow everywhere. "
        "Partially wet cells retain their shoreline. Applies to maps and profiles; "
        "simulation results are unchanged."), water);
    hint->setWordWrap(true);
    waterForm->addRow(modelDepth);
    waterForm->addRow(tr("Thin-film depth:"), filmDepth);
    waterForm->addRow(showFilms);
    waterForm->addRow(hint);
    root->insertWidget(0, water);
    const auto syncWater = [this,modelDepth,filmDepth,showFilms] {
        if (!m_layer) return;
        const QSignalBlocker a(modelDepth), b(filmDepth), c(showFilms);
        modelDepth->setChecked(m_layer->usesModelThinFilmDepth());
        filmDepth->setValue(m_layer->thinFilmDepth());
        filmDepth->setEnabled(!modelDepth->isChecked());
        showFilms->setChecked(m_layer->showThinFilms());
    };
    syncWater();
    connect(modelDepth, &QCheckBox::toggled, this, [this,filmDepth](bool on) {
        if (m_layer) m_layer->setThinFilmDepth(on ? -1.0 : filmDepth->value());
    });
    connect(filmDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double depth) { if (m_layer) m_layer->setThinFilmDepth(depth); });
    connect(showFilms, &QCheckBox::toggled, this,
            [this](bool show) { if (m_layer) m_layer->setShowThinFilms(show); });
    connect(m_layer, &SWMM2DResultsLayer::waterDisplayPolicyChanged, this, syncWater);

    // Each page sits in a scroll area so a narrow/short dialog scrolls
    // instead of compressing the editors below their minimum sizes.
    auto wrapScroll = [tabs](QWidget *page) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        return scroll;
    };
    // Routing contract: LayerStyleDialog::focusInitialSubject() matches
    // tabToolTip == the sublayer id, so right-clicking a sublayer row in the
    // layer tree (or a legend entry) lands on the matching tab instead of
    // always the first one.
    auto addTab = [tabs, &wrapScroll](QWidget *page, const QString &label,
                                      OpenSWMM::Render::ISublayer *sub) {
        const int idx = tabs->addTab(wrapScroll(page), label);
        if (sub) tabs->setTabToolTip(idx, sub->id());
    };

    // The two direct-mesh depth fills (flat per-cell / Gouraud per-vertex).
    // Both sublayers pre-date this panel but had no styling UI at all — the
    // only "smooth" control users could find was the contour-band boundary
    // interpolation checkbox, which is a different knob entirely.
    auto *cellSub   = m_layer->cellDepthFillSublayer();
    auto *smoothSub = m_layer->smoothDepthFillSublayer();
    addTab(buildScalarFillTab(cellSub, cellSub ? cellSub->fillStyle() : nullptr,
                              tr("Show cell depth fill"), tabs),
           tr("&Cell Depth Fill"), cellSub);
    addTab(buildScalarFillTab(smoothSub, smoothSub ? smoothSub->fillStyle() : nullptr,
                              tr("Show smooth depth fill"), tabs),
           tr("&Smooth Depth Fill"), smoothSub);
    // 2026-06-21 — the "Depth Fill" (depth color ramp) and "Flow Arrows" tabs
    // were removed: contour bands now provide the depth fill, and velocity
    // vectors already convey flow direction.
    addTab(buildContourBandTab(tabs), tr("D&epth Contours"),  m_layer->contourBandSublayer());
    addTab(buildIsolineTab(tabs),     tr("De&pth Isolines"),  m_layer->isolineSublayer());
    addTab(buildVelocityTab(tabs),    tr("&Flow Velocity"),   m_layer->velocityVectorSublayer());
    // Issue 6 — Edges/Vertices were previously only editable via the generic
    // property grid; give them dedicated tabs so the edge colour/width (the
    // knobs behind the dark-edge artifact) and the vertex markers are tunable
    // here. The slope-emphasis and tagged-vertex groups are omitted because the
    // results renderer does not honour them (no dead knobs).
    addTab(buildMeshEdgeTab(tabs),     tr("&Mesh Edges"),    m_layer->meshEdgeSublayer());
    addTab(buildMeshNodeTab(tabs),     tr("Mes&h Vertices"), m_layer->meshNodeSublayer());
    tabs->addTab(wrapScroll(buildAdditionalResultsTab(tabs)),tr("Additional &Results"));
    connect(m_layer,&QObject::destroyed,this,[this,tabs] {
        m_layer.clear(); m_refreshResultData={}; setEnabled(false);
        m_resultList=nullptr; m_resultCatalog=nullptr; m_resultDetailHost=nullptr;
        while(tabs->count()) {
            auto *page=tabs->widget(0); tabs->removeTab(0);
            page->setEnabled(false);
            // Silence pending editor callbacks without disconnecting Qt's
            // internal destroyed/model bookkeeping. destroyed() is emitted
            // even while signals are blocked, so deferred deletion stays safe.
            for(auto *object:page->findChildren<QObject *>()) object->blockSignals(true);
            page->hide(); page->setParent(nullptr); page->deleteLater();
        }
    });
}

QWidget *Swmm2DResultsStylePanel::buildAdditionalResultsTab(QWidget *parent)
{
    auto *page=new QWidget(parent);
    auto *layout=new QVBoxLayout(page);
    m_resultList=new QListWidget(page);
    m_resultList->setObjectName("additionalResultList");
    m_resultList->setAccessibleName(tr("Configured result layers"));
    m_resultList->setMaximumHeight(100);
    layout->addWidget(m_resultList);
    auto *row=new QHBoxLayout;
    m_resultCatalog=new QComboBox(page);
    m_resultCatalog->setObjectName("additionalResultCatalog");
    m_resultCatalog->setAccessibleName(tr("Result variable to add"));
    row->addWidget(m_resultCatalog,1);
    auto *add=new QPushButton(tr("&Add result"),page); add->setObjectName("additionalResultAdd");
    row->addWidget(add);
    auto *remove=new QPushButton(tr("&Remove result"),page); remove->setObjectName("additionalResultRemove");
    row->addWidget(remove); layout->addLayout(row);
    m_resultDetailHost=new QWidget(page); new QVBoxLayout(m_resultDetailHost);
    layout->addWidget(m_resultDetailHost);
    connect(add,&QPushButton::clicked,this,[this] {
        if(!m_layer || m_resultCatalog->currentIndex()<0) return;
        auto *sub=m_layer->addResultSublayer(m_resultCatalog->currentData().toString());
        if(!sub) return;
        for(int i=0;i<m_resultList->count();++i)
            if(m_resultList->item(i)->data(Qt::UserRole).toString()==sub->id()) m_resultList->setCurrentRow(i);
    });
    connect(remove,&QPushButton::clicked,this,[this] {
        if(m_layer && m_resultList->currentItem())
            m_layer->removeResultSublayer(m_resultList->currentItem()->data(Qt::UserRole).toString());
    });
    connect(m_resultList,&QListWidget::currentRowChanged,this,[this,remove](int row) {
        remove->setEnabled(row>=0); rebuildResultDetails();
    });
    connect(m_layer,&SWMM2DResultsLayer::resultSublayersChanged,this,&Swmm2DResultsStylePanel::refreshAdditionalResults);
    connect(m_layer,&SWMM2DResultsLayer::timeRangeChanged,this,&Swmm2DResultsStylePanel::refreshAdditionalResults);
    connect(m_layer,&SWMM2DResultsLayer::currentTimeChanged,this,[this]{if(m_refreshResultData)m_refreshResultData();});
    connect(m_layer,&SWMM2DResultsLayer::highlightedCellsChanged,this,[this]{if(m_refreshResultData)m_refreshResultData();});
    refreshAdditionalResults();
    add->setEnabled(m_resultCatalog->count()>0); remove->setEnabled(m_resultList->currentRow()>=0);
    connect(m_resultCatalog,qOverload<int>(&QComboBox::currentIndexChanged),add,[add](int row){add->setEnabled(row>=0);});
    return page;
}

namespace {
QString resultVariableLabel(const openswmmvis::io::Mesh2DResultVariable &v)
{
    using V=openswmmvis::io::Mesh2DResultVariable;
    QString domain=v.domain==V::Domain::Groundwater?QObject::tr("Groundwater"):QObject::tr("Surface");
    if(v.zone==V::Zone::Saturated) domain+=QObject::tr(" / saturated");
    else if(v.zone==V::Zone::Unsaturated) domain+=QObject::tr(" / unsaturated");
    else if(v.zone==V::Zone::Sigma) domain+=QObject::tr(" / sigma %1").arg(v.layer);
    const QString label=v.label.isEmpty()?v.dataset:v.label;
    const QString named=label.startsWith(domain,Qt::CaseInsensitive)
        ?label:QStringLiteral("%1 — %2").arg(domain,label);
    return QStringLiteral("%1 [%2]").arg(named,v.unitsKnown?v.units:QObject::tr("units unknown"));
}
}

bool Swmm2DResultsStylePanel::focusResult(const QString &id)
{
    if(!m_resultList)return false;
    for(int row=0;row<m_resultList->count();++row) {
        if(m_resultList->item(row)->data(Qt::UserRole).toString()!=id)continue;
        m_resultList->setCurrentRow(row);
        for(auto *tabs:findChildren<QTabWidget *>())
            for(int i=0;i<tabs->count();++i)
                if(tabs->widget(i)->isAncestorOf(m_resultList))tabs->setCurrentIndex(i);
        m_resultList->setFocus();return true;
    }
    return false;
}

void Swmm2DResultsStylePanel::refreshAdditionalResults()
{
    if(!m_layer || !m_resultList) return;
    const QString selected=m_resultList->currentItem()?m_resultList->currentItem()->data(Qt::UserRole).toString():QString();
    const QString adding=m_resultCatalog->currentData().toString();
    {
        QSignalBlocker block(m_resultCatalog);
        m_resultCatalog->clear();
        for(const auto &v:m_layer->resultVariables()) m_resultCatalog->addItem(resultVariableLabel(v),v.key());
        const int index=m_resultCatalog->findData(adding);
        if(index>=0) m_resultCatalog->setCurrentIndex(index);
    }
    {
        QSignalBlocker block(m_resultList); m_resultList->clear();
        int row=0,chosen=-1;
        for(auto *base:m_layer->sublayers()) if(auto *sub=qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base)) {
            QString label=tr("Unavailable — %1").arg(sub->variableKey());
            for(const auto &v:m_layer->resultVariables())
                if(v.key()==sub->variableKey()){label=resultVariableLabel(v);break;}
            auto *item=new QListWidgetItem(label,m_resultList);
            item->setData(Qt::UserRole,sub->id()); item->setToolTip(sub->variableKey());
            if(sub->id()==selected) chosen=row;
            ++row;
        }
        m_resultList->setCurrentRow(chosen>=0?chosen:(row>0?0:-1));
    }
    if(auto *add=findChild<QPushButton *>("additionalResultAdd")) add->setEnabled(m_resultCatalog->count()>0);
    if(auto *remove=findChild<QPushButton *>("additionalResultRemove")) remove->setEnabled(m_resultList->currentRow()>=0);
    rebuildResultDetails();
}

void Swmm2DResultsStylePanel::rebuildResultDetails()
{
    using namespace OpenSWMM::Render;
    using namespace openswmmvis::io;
    m_refreshResultData={};
    if(!m_resultDetailHost) return;
    // A change can arrive inside a child signal callback. Remove old controls
    // from discovery/focus immediately, but defer their destruction safely.
    auto *layout=qobject_cast<QVBoxLayout *>(m_resultDetailHost->layout());
    while(auto *item=layout->takeAt(0)) {
        if(auto *w=item->widget()) {w->setEnabled(false); w->hide(); w->setParent(nullptr); w->deleteLater();}
        delete item;
    }
    if(!m_layer || !m_resultList->currentItem()) return;
    QPointer<ResultScalarSublayer> sub=qobject_cast<ResultScalarSublayer *>(
        ISublayerHost::findSublayer(*m_layer,m_resultList->currentItem()->data(Qt::UserRole).toString()));
    if(!sub) return;
    QPointer<SWMM2DResultsLayer> layer=m_layer;
    QPointer<ResultScalarStyle> style=sub->fillStyle();
    auto *page=new QWidget(m_resultDetailHost); layout->addWidget(page);
    auto *form=new QFormLayout(page);
    form->setFormAlignment(Qt::AlignTop);
    auto *variable=new QComboBox(page); variable->setObjectName("additionalResultVariable");
    variable->setAccessibleName(tr("Configured result variable"));
    QStringList warnings;
    const auto variables=layer->resultVariables(&warnings);
    for(const auto &v:variables) variable->addItem(resultVariableLabel(v),v.key());
    int current=variable->findData(sub->variableKey());
    if(current<0) {
        current=variable->count(); variable->addItem(tr("Unavailable — %1").arg(sub->variableKey()),sub->variableKey());
        if(auto *model=qobject_cast<QStandardItemModel *>(variable->model())) model->item(current)->setEnabled(false);
    }
    variable->setCurrentIndex(current); form->addRow(tr("&Variable:"),variable);
    connect(variable,qOverload<int>(&QComboBox::currentIndexChanged),page,[this,sub,variable](int index) {
        if(sub && index>=0) {sub->setVariableKey(variable->itemData(index).toString()); refreshAdditionalResults();}
    });
    auto *visible=new QCheckBox(tr("Show result layer"),page); visible->setChecked(sub->isVisible());
    visible->setObjectName("additionalResultVisible"); form->addRow(visible);
    connect(visible,&QCheckBox::toggled,page,[sub](bool value){if(sub)sub->setVisible(value);});
    auto *opacity=new QDoubleSpinBox(page); opacity->setObjectName("additionalResultOpacity");
    opacity->setRange(0,100); opacity->setSuffix(tr(" %")); opacity->setValue(sub->opacity()*100);
    form->addRow(tr("&Opacity:"),opacity);
    connect(opacity,qOverload<double>(&QDoubleSpinBox::valueChanged),page,[sub](double value){if(sub)sub->setOpacity(value/100);});
    auto frameFor=[layer,sub,style] {
        return layer&&sub&&style?layer->resultFrame(sub->variableKey(),layer->currentTimeIndex(),
            !style->scheme().useCustomRange() && style->scheme().rangeMode()==RangeMode::FixedOverRun):std::shared_ptr<const Mesh2DScalarFrame>();
    };
    auto *binding=new SublayerSchemeBinding(
        [style]{return style?style->scheme():ClassificationScheme();},
        [style](const ClassificationScheme&s){if(style)style->setScheme(s);},
        [frameFor]{auto f=frameFor();return f?f->samples:QVector<double>();},
        [frameFor]{auto f=frameFor();const double nan=std::numeric_limits<double>::quiet_NaN();
            return f?qMakePair(f->minimum,f->maximum):qMakePair(nan,nan);},true,true);
    auto *classification=new ClassificationEditor(binding,true,page);
    classification->setObjectName("additionalResultClassification"); form->addRow(classification);
    auto addColor=[&](const QString &label,const char *name,QColor value,auto setter) {
        auto *button=new ColorButton(page); button->setObjectName(name); button->setAccessibleName(label);
        button->setShowAlpha(true); button->setColor(value); form->addRow(label,button);
        connect(button,&ColorButton::colorChanged,page,[style,setter](const QColor &c){if(style)(style.data()->*setter)(c);});
    };
    addColor(tr("Missing data:"),"additionalResultMissing",style->missingColor(),&ResultScalarStyle::setMissingColor);
    addColor(tr("Waterless cell:"),"additionalResultWaterless",style->waterlessColor(),&ResultScalarStyle::setWaterlessColor);
    addColor(tr("Not applicable:"),"additionalResultNotApplicable",style->notApplicableColor(),&ResultScalarStyle::setNotApplicableColor);
    auto *status=new QLabel(page); status->setObjectName("additionalResultStatus");
    status->setTextFormat(Qt::PlainText); status->setWordWrap(true); status->setAccessibleName(tr("Result availability"));
    form->addRow(status);
    auto *table=new QTableWidget(0,4,page); table->setObjectName("additionalResultInspection");
    table->setMaximumHeight(90);
    table->setAccessibleName(tr("Selected cell result")); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalHeaderLabels({tr("Cell"),tr("Status"),tr("Raw value"),tr("Units")});
    table->setToolTip(tr("Select a mesh cell on the map to inspect this variable. Missing and waterless values are not physical zeros."));
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); form->addRow(table);
    auto *exportButton=new QPushButton(tr("Export current variable to CSV…"),page);
    exportButton->setObjectName("additionalResultExport");
    exportButton->setToolTip(tr("Exports all cells for this variable at the current report frame, with native units and availability status."));
    form->addRow(exportButton);
    m_refreshResultData=[layer,sub,classification,status,table,exportButton,warnings] {
        if(!layer || !sub)return;
        const auto frame=layer->resultFrame(sub->variableKey(),layer->currentTimeIndex());
        const bool available=frame && frame->error.isEmpty();
        // Keep the editor and any invalid visible draft intact during animation
        // and selection changes. Only refresh a complete, valid draft.
        if(classification->hasValidDraft()) classification->refresh();
        classification->setEnabled(available && std::isfinite(frame->minimum) && std::isfinite(frame->maximum));
        table->setRowCount(0);
        QStringList messages=warnings;
        if(!available) messages<< (frame?frame->error:tr("Result data unavailable."));
        else {
            const auto &v=frame->descriptor;
            messages<< (v.unitsKnown?tr("Native units: %1.").arg(v.units):tr("Units are unknown. Raw inspection is available; scientific export is disabled."));
            switch(v.temporal) {
            case Mesh2DResultVariable::Temporal::Held: messages<<tr("Held result sampled at report times; this is not the solver firing time."); break;
            case Mesh2DResultVariable::Temporal::Static: messages<<tr("Static result; independent of report time."); break;
            case Mesh2DResultVariable::Temporal::Envelope: messages<<tr("Run envelope; values need not occur simultaneously."); break;
            default: messages<<tr("Result at the current report frame."); break;
            }
        }
        status->setText(messages.join('\n'));
        QSet<int> selectedCells;
        const auto &triCell=layer->triCellMap();
        for(int triangle:layer->highlightedCells())
            if(triangle>=0 && size_t(triangle)<triCell.size())selectedCells.insert(triCell[size_t(triangle)]);
        if(available && !selectedCells.isEmpty()) {
            const int cell=*std::min_element(selectedCells.begin(),selectedCells.end());
            if(cell>=0 && cell<int(frame->values.size()) && cell<int(frame->status.size())) {
                QString state;
                switch(frame->status[size_t(cell)]) {
                case Mesh2DValueStatus::Valid:state=tr("Valid");break;
                case Mesh2DValueStatus::Missing:state=tr("Missing");break;
                case Mesh2DValueStatus::Waterless:state=tr("Waterless");break;
                case Mesh2DValueStatus::NotApplicable:state=tr("Not applicable");break;
                }
                table->setRowCount(1);
                const double raw=frame->values[size_t(cell)];
                const QStringList cells={tr("%1 (first of %2 selected)").arg(cell+1).arg(selectedCells.size()),state,
                    std::isfinite(raw)?QString::number(raw,'g',9):tr("Unavailable"),frame->descriptor.unitsKnown?frame->descriptor.units:tr("Unknown")};
                for(int i=0;i<cells.size();++i)table->setItem(0,i,new QTableWidgetItem(cells[i]));
            }
        }

        exportButton->setEnabled(available && frame->descriptor.unitsKnown && !frame->descriptor.units.isEmpty());
    };
    m_refreshResultData();

    connect(exportButton,&QPushButton::clicked,page,[this,layer,sub,status] {
        if(!layer || !sub || !layer->source())return;
        const QString key=sub->variableKey(); const int time=layer->currentTimeIndex();
        const quint64 revision=layer->sourceRevision(); auto *source=layer->source();
        QPointer<QLabel> safeStatus=status;
        const QString path=QFileDialog::getSaveFileName(this,tr("Export result variable"),QString(),tr("CSV files (*.csv)"));
        if(path.isEmpty() || !safeStatus)return;
        if(!layer || !sub || layer->source()!=source || layer->sourceRevision()!=revision ||
           layer->currentTimeIndex()!=time || sub->variableKey()!=key) {
            safeStatus->setText(tr("The result source or selection changed. Reopen export for the current result."));return;
        }
        QString error;
        if(!exportMesh2DVariableCsv(*source,key,time,path,&error))safeStatus->setText(error);
        else safeStatus->setText(tr("Exported %1").arg(path));
    });
}

// ─── Scalar depth fills (cell / smooth) ─────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildScalarFillTab(
    OpenSWMM::Render::ISublayer *sub, OpenSWMM::Render::ScalarFillStyle *st,
    const QString &showLabel, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    lay->addWidget(makeSublayerHeader(page, sub, showLabel));

    if (st) {
        auto *attrBox  = new QGroupBox(tr("Attribute"), page);
        auto *attrForm = new QFormLayout(attrBox);
        auto *attr = new QComboBox(attrBox);
        attr->addItem(tr("Depth"),     QStringLiteral("depth"));
        const int cur = attr->findData(st->attribute());
        if(cur<0) {
            attr->addItem(tr("Unsupported — %1").arg(st->attribute()),st->attribute());
            if(auto *model=qobject_cast<QStandardItemModel *>(attr->model()))model->item(attr->count()-1)->setEnabled(false);
        }
        attr->setCurrentIndex(cur>=0?cur:attr->count()-1);
        attr->setMinimumWidth(kComboMinWidthPx);
        attrForm->addRow(tr("&Attribute:"), attr);
        lay->addWidget(attrBox);
        connect(attr, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, attr](int idx) { st->setAttribute(attr->itemData(idx).toString()); });

        // Continuous mode samples the ramp per value (seam-free gradient);
        // Classified bins into discrete classes — the scheme's ClassMode is
        // the toggle, surfaced by the shared editor.
        auto *L = m_layer.data();
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },
            [L] { return qMakePair(0.0, L->maxDepth()); },
            /*supportsContinuousMode=*/true,
            /*supportsRangeModes=*/true);
        lay->addWidget(new ClassificationEditor(binding, /*ownBinding=*/true, page));
    }
    lay->addStretch();
    return page;
}

// ─── Contour bands ──────────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildContourBandTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->contourBandSublayer();
    ContourBandStyle *st = sub ? sub->bandStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show filled contour bands")));

    if (st) {
        // Slice US.2 — the shared classification editor: method (equal
        // interval / quantile / Jenks / …), class count, colour ramp + invert,
        // custom range, and a per-class colour/label table. The renderer reads
        // st->scheme() to march the bands.
        auto *L = m_layer.data();
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },            // table preview only; map samples per frame
            [L] { return qMakePair(0.0, L->maxDepth()); },
            /*supportsContinuousMode=*/false,
            /*supportsRangeModes=*/false);
        lay->addWidget(new ClassificationEditor(binding, /*ownBinding=*/true, page));

        auto *renderBox  = new QGroupBox(tr("Rendering"), page);
        auto *renderForm = new QFormLayout(renderBox);
        auto *smooth = new QCheckBox(tr("Interpolate band boundaries (marching triangles)"),
                                     renderBox);
        smooth->setChecked(st->smoothBands());
        smooth->setToolTip(tr("On: class boundaries are interpolated through "
                              "cells (marching triangles). Off: each cell is "
                              "filled flat with its band colour. For a "
                              "seam-free continuous gradient use the Smooth "
                              "Depth Fill tab instead."));
        renderForm->addRow(QString(), smooth);
        lay->addWidget(renderBox);

        connect(smooth, &QCheckBox::toggled, this,
                [st](bool on) { st->setSmoothBands(on); });
    }
    lay->addStretch();
    return page;
}

// ─── Isolines ───────────────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildIsolineTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->isolineSublayer();
    IsolineStyle *st = sub ? sub->isolineStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show isolines")));

    if (st) {
        using LM = IsolineStyle::LevelMode;

        auto *levelBox  = new QGroupBox(tr("Levels"), page);
        auto *levelForm = new QFormLayout(levelBox);
        auto *mode = new QComboBox(levelBox);
        mode->addItem(tr("Fixed count"),             int(LM::Count));
        mode->addItem(tr("Fixed interval + base"),   int(LM::FixedInterval));
        mode->setCurrentIndex(mode->findData(int(st->levelMode())));
        mode->setMinimumWidth(kComboMinWidthPx);
        levelForm->addRow(tr("M&ode:"), mode);
        auto *count = makeSpin(levelBox, 1, 64, st->isoValueCount());
        levelForm->addRow(tr("Co&unt:"), count);
        // Slice US.2 — classification method for the Count idiom (equal
        // interval reproduces the legacy even spacing; quantile / Jenks /
        // std-dev bin the wet-cell depths). FixedInterval ignores it.
        using BM = OpenSWMM::Render::BinMethod;
        auto *method = new QComboBox(levelBox);
        method->addItem(tr("Equal interval"),         int(BM::EqualInterval));
        method->addItem(tr("Quantile"),               int(BM::Quantile));
        method->addItem(tr("Natural breaks (Jenks)"), int(BM::NaturalBreaks));
        method->addItem(tr("Standard deviation"),     int(BM::StdDev));
        method->addItem(tr("Logarithmic"),            int(BM::Logarithmic));
        method->addItem(tr("Exponential"),            int(BM::Exponential));
        method->setCurrentIndex(method->findData(int(st->scheme().method())));
        method->setMinimumWidth(kComboMinWidthPx);
        levelForm->addRow(tr("Me&thod:"), method);
        auto *interval = makeDSpin(levelBox, 1e-6, 1e6, 0.1, 3,
                                   st->levelInterval(), tr(" m"));
        levelForm->addRow(tr("&Interval:"), interval);
        auto *base = makeDSpin(levelBox, -1e6, 1e6, 0.1, 3,
                               st->baseLevel(), tr(" m"));
        base->setToolTip(tr("Contours fall at base + k × interval"));
        levelForm->addRow(tr("B&ase level:"), base);
        lay->addWidget(levelBox);

        auto applyMode = [count, method, interval, base](LM m) {
            count->setEnabled(m == LM::Count);
            method->setEnabled(m == LM::Count);
            interval->setEnabled(m == LM::FixedInterval);
            base->setEnabled(m == LM::FixedInterval);
        };
        applyMode(st->levelMode());

        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);
        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Co&lour:"), color);
        auto *width = makeDSpin(symBox, 0.25, 20.0, 0.25, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("&Width:"), width);
        auto *dash = new DashStyleCombo(symBox);
        dash->setPenStyle(st->dashPattern());
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("St&roke style:"), dash);
        auto *idxEvery = makeSpin(symBox, 0, 50, st->indexEvery());
        idxEvery->setSpecialValueText(tr("Off"));
        idxEvery->setToolTip(tr("Emphasise every Nth contour (topographic "
                                "index contours); 0 disables"));
        symForm->addRow(tr("I&ndex contour every:"), idxEvery);
        auto *idxWidth = makeDSpin(symBox, 0.25, 20.0, 0.25, 2,
                                   st->indexWidthPx(), tr(" px"));
        idxWidth->setEnabled(st->indexEvery() >= 2);
        symForm->addRow(tr("Inde&x width:"), idxWidth);
        lay->addWidget(symBox);

        auto *labelBox  = new QGroupBox(tr("Labels"), page);
        auto *labelForm = new QFormLayout(labelBox);
        auto *labelsOn = new QCheckBox(tr("Label contour values along lines"),
                                       labelBox);
        labelsOn->setChecked(st->labels());
        labelForm->addRow(QString(), labelsOn);
        auto *decimals = makeSpin(labelBox, 0, 9, st->labelDecimals());
        decimals->setEnabled(st->labels());
        labelForm->addRow(tr("Decimals:"), decimals);
        auto *fontPt = makeDSpin(labelBox, 4.0, 72.0, 0.5, 1,
                                 st->labelFontPt(), tr(" pt"));
        fontPt->setEnabled(st->labels());
        labelForm->addRow(tr("Font si&ze:"), fontPt);
        auto *halo = new QCheckBox(tr("White halo"), labelBox);
        halo->setChecked(st->labelHalo());
        halo->setEnabled(st->labels());
        labelForm->addRow(QString(), halo);
        lay->addWidget(labelBox);

        connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, mode, applyMode](int i) {
                    const auto m = static_cast<LM>(mode->itemData(i).toInt());
                    applyMode(m);
                    st->setLevelMode(m);
                });
        connect(count, qOverload<int>(&QSpinBox::valueChanged), this,
                [st](int n) { st->setIsoValueCount(n); });
        connect(method, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, method](int i) {
                    auto s = st->scheme();
                    s.setMethod(static_cast<OpenSWMM::Render::BinMethod>(
                        method->itemData(i).toInt()));
                    st->setScheme(s);
                });
        connect(interval, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLevelInterval(v); });
        connect(base, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setBaseLevel(v); });
        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double w) { st->setLineWidthPx(w); });
        connect(dash, &DashStyleCombo::penStyleChanged, this,
                [st](Qt::PenStyle s) { st->setDashPattern(s); });
        connect(idxEvery, qOverload<int>(&QSpinBox::valueChanged), this,
                [st, idxWidth](int n) {
                    idxWidth->setEnabled(n >= 2);
                    st->setIndexEvery(n);
                });
        connect(idxWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double w) { st->setIndexWidthPx(w); });
        connect(labelsOn, &QCheckBox::toggled, this,
                [st, decimals, fontPt, halo](bool on) {
                    decimals->setEnabled(on);
                    fontPt->setEnabled(on);
                    halo->setEnabled(on);
                    st->setLabels(on);
                });
        connect(decimals, qOverload<int>(&QSpinBox::valueChanged), this,
                [st](int n) { st->setLabelDecimals(n); });
        connect(fontPt, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLabelFontPt(v); });
        connect(halo, &QCheckBox::toggled, this,
                [st](bool on) { st->setLabelHalo(on); });
    }
    lay->addStretch();
    return page;
}

// ─── Velocity vectors ───────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildVelocityTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->velocityVectorSublayer();
    VelocityVectorStyle *st = sub ? sub->vectorStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show velocity vectors")));

    if (st) {
        using LS = VelocityVectorStyle::LengthScaling;

        auto *sizeBox  = new QGroupBox(tr("Sizing"), page);
        auto *sizeForm = new QFormLayout(sizeBox);
        auto *scaling = new QComboBox(sizeBox);
        scaling->addItem(tr("Linear"),      int(LS::Linear));
        scaling->addItem(tr("Square root"), int(LS::SquareRoot));
        scaling->addItem(tr("Logarithmic"), int(LS::Log));
        scaling->setCurrentIndex(scaling->findData(int(st->lengthScaling())));
        scaling->setToolTip(tr("How |v| maps to arrow length before the "
                               "min/max clamps"));
        scaling->setMinimumWidth(kComboMinWidthPx);
        sizeForm->addRow(tr("Len&gth scaling:"), scaling);
        auto *scale = makeDSpin(sizeBox, 0.1, 500.0, 1.0, 1,
                                st->glyphLengthScalePxPerMps(),
                                tr(" px per m/s"));
        sizeForm->addRow(tr("Scale:"), scale);
        auto *minLen = makeDSpin(sizeBox, 0.0, 200.0, 1.0, 1,
                                 st->glyphLengthMinPx(), tr(" px"));
        sizeForm->addRow(tr("Min length:"), minLen);
        auto *maxLen = makeDSpin(sizeBox, 1.0, 500.0, 1.0, 1,
                                 st->glyphLengthMaxPx(), tr(" px"));
        sizeForm->addRow(tr("Max length:"), maxLen);
        auto *head = makeDSpin(sizeBox, 0.0, 50.0, 0.5, 1,
                               st->headSizePx(), tr(" px"));
        sizeForm->addRow(tr("Head size:"), head);
        auto *shaft = makeDSpin(sizeBox, 0.1, 10.0, 0.1, 1,
                                st->shaftWidthPx(), tr(" px"));
        sizeForm->addRow(tr("Shaft width:"), shaft);
        lay->addWidget(sizeBox);

        auto *colorBox  = new QGroupBox(tr("Colour"), page);
        auto *colorForm = new QFormLayout(colorBox);
        auto *byMag = new QCheckBox(tr("Colour by magnitude"), colorBox);
        byMag->setChecked(st->colorByMagnitude());
        colorForm->addRow(QString(), byMag);
        auto *flat = new ColorButton(colorBox);
        flat->setShowAlpha(true);
        flat->setColor(st->color());
        flat->setEnabled(!st->colorByMagnitude());
        colorForm->addRow(tr("Single colour:"), flat);
        lay->addWidget(colorBox);

        // Slice US.2 — the shared classification editor owns the ramp + invert,
        // method (equal interval / quantile / Jenks / …), class count, and the
        // speed range. Range provider yields the style's speed min/max; map
        // samples per frame so the table preview just degrades to equal spacing.
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },
            [st] {
                const double lo = st->speedMinMps();
                const double hi = (st->speedMaxMps() > lo) ? st->speedMaxMps()
                                                           : lo + 1.0;
                return qMakePair(lo, hi);
            },
            /*supportsContinuousMode=*/true,
            /*supportsRangeModes=*/false);
        auto *classEditor = new ClassificationEditor(binding, /*ownBinding=*/true, page);
        classEditor->setEnabled(st->colorByMagnitude());
        lay->addWidget(classEditor);

        auto *placeBox  = new QGroupBox(tr("Placement && filtering"), page);
        auto *placeForm = new QFormLayout(placeBox);
        auto *spacing = makeDSpin(placeBox, 1.0, 500.0, 5.0, 0,
                                  st->glyphSpacingPx(), tr(" px"));
        spacing->setToolTip(tr("Minimum on-screen spacing between arrows "
                               "(strongest cell per grid slot wins); 1 px "
                               "draws every wet cell"));
        placeForm->addRow(tr("Spacing:"), spacing);
        auto *dryCut = makeDSpin(placeBox, 0.0, 1000.0, 0.01, 3,
                                 st->dryDepthCutoff(), tr(" m"));
        dryCut->setToolTip(tr("Suppress arrows where depth is below this"));
        placeForm->addRow(tr("Dr&y depth cutoff:"), dryCut);
        lay->addWidget(placeBox);

        connect(scaling, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, scaling](int i) {
                    st->setLengthScaling(
                        static_cast<LS>(scaling->itemData(i).toInt()));
                });
        connect(scale, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthScalePxPerMps(v); });
        connect(minLen, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthMinPx(v); });
        connect(maxLen, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthMaxPx(v); });
        connect(head, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setHeadSizePx(v); });
        connect(shaft, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setShaftWidthPx(v); });
        connect(byMag, &QCheckBox::toggled, this,
                [st, flat, classEditor](bool on) {
                    flat->setEnabled(!on);
                    classEditor->setEnabled(on);
                    st->setColorByMagnitude(on);
                });
        connect(flat, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(spacing, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphSpacingPx(v); });
        connect(dryCut, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setDryDepthCutoff(v); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh edges (Issue 6) ────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshEdgeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshEdgeSublayer();
    MeshEdgeStyle *st = sub ? sub->edgeStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh edges")));

    if (st) {
        // Symbology only — the slope-emphasis (thin/wide) group is intentionally
        // omitted: the 2D results edge pass uses a single uniform width + flat
        // colour (the wide-colour split needs per-edge colour the flat edge node
        // can't carry), so surfacing those knobs would be misleading.
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        color->setToolTip(tr("Edge colour. The wireframe is now drawn once per "
                             "unique edge, so a translucent colour no longer "
                             "darkens where cells meet."));
        symForm->addRow(tr("Colour:"), color);

        auto *width = makeDSpin(symBox, 0.1, 10.0, 0.05, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("Width:"), width);

        auto *dash = new DashStyleCombo(symBox);
        dash->setPenStyle(st->dashPattern());
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("Stro&ke style:"), dash);

        lay->addWidget(symBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLineWidthPx(v); });
        connect(dash, &DashStyleCombo::penStyleChanged, this,
                [st](Qt::PenStyle s) { st->setDashPattern(s); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh vertices (Issue 6) ─────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshNodeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshNodeSublayer();
    MeshNodeStyle *st = sub ? sub->nodeStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh vertices")));

    if (st) {
        // Symbology + outline only — the shape and tagged-vertex groups are
        // omitted: the results renderer draws a fixed marker and carries no
        // tagged-vertex data, so those knobs would be no-ops here.
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Colour:"), color);

        auto *size = makeDSpin(symBox, 0.5, 20.0, 0.5, 1,
                               st->markerSizePx(), tr(" px"));
        symForm->addRow(tr("Marker size:"), size);

        auto *outlineColor = new ColorButton(symBox);
        outlineColor->setShowAlpha(true);
        outlineColor->setColor(st->outlineColor());
        symForm->addRow(tr("Outline colour:"), outlineColor);

        auto *outlineWidth = makeDSpin(symBox, 0.0, 5.0, 0.25, 2,
                                       st->outlineWidthPx(), tr(" px"));
        symForm->addRow(tr("Outline width:"), outlineWidth);

        lay->addWidget(symBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(size, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setMarkerSizePx(v); });
        connect(outlineColor, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setOutlineColor(c); });
        connect(outlineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setOutlineWidthPx(v); });
    }
    lay->addStretch();
    return page;
}

} // namespace openswmmvis::ui
