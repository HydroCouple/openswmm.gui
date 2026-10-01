#include "ui/dialogs/meshprofileplotdialog.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofiletrackswidget.h"
#include "plot/meshprofileserieseditor.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/meshprofileoverlay.h"
#include "map/spatialreferencesystem.h"
#include "swmmvisprojectwindow.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace ProfileSection;
namespace {
QString normalizedPath(const QString &path)
{
    if(path.isEmpty())return {};
    const QFileInfo file(path); return file.canonicalFilePath().isEmpty() ? file.absoluteFilePath() : file.canonicalFilePath();
}
}
QString MeshProfilePlotDialog::sourceId(SWMM2DResultsLayer *layer) const
{
    if(!layer)return {};
    const QString path=layer->source() ? layer->source()->sourcePath() : QString();
    if(!path.isEmpty()) {
        for(const auto &source:m_definition.sources)
            if(!source.path.isEmpty() && normalizedPath(source.path)==normalizedPath(path))return source.id;
        return normalizedPath(path);
    }
    return layer->layerId();
}
Definition MeshProfilePlotDialog::definition() const
{
    auto result=m_definition; result.scenePolyline=m_scenePolyline;
    result.displayOptions=m_options->toJson(); return result;
}
bool MeshProfilePlotDialog::setDefinition(const Definition &definition,QString *error)
{
    if(!validateDefinition(definition,error))return false;
    auto *currentSrs = m_projectWindow && m_projectWindow->canvas() ? m_projectWindow->canvas()->canvasSRS()
        : (m_results ? m_results->srs() : (m_mesh ? m_mesh->srs() : nullptr));
    SpatialReferenceSystem savedSrs(definition.sceneCRS, static_cast<QObject *>(nullptr));
    if (!currentSrs || !currentSrs->equals(savedSrs)) {
        if (error) *error = tr("The saved section uses a different or unavailable coordinate system. Restore that coordinate system before opening the section.");
        return false;
    }
    m_loadingDefinition=true;
    if(!m_options->fromJson(definition.displayOptions,error)) {m_loadingDefinition=false;return false;}
    m_definition=definition; m_definition.sceneCRS=currentSrs->toWkt(); m_scenePolyline=definition.scenePolyline;
    if(auto *title=findChild<QLineEdit *>("sectionTitle"))title->setText(definition.title);
    m_seriesEditor->setDefinition(definition); m_seriesStations.clear();
    refreshSourceChoices(); rebuildProfile();
    if(m_overlay)m_overlay->setPolyline(m_scenePolyline);
    setWindowTitle(definition.title.isEmpty() ? tr("2D Mesh Profile") : definition.title);
    m_loadingDefinition=false; return true;
}
void MeshProfilePlotDialog::setResultSources(const QList<SWMM2DResultsLayer *> &sources)
{
    // Connections are context-bound and sender guarded. Keep the primary's
    // existing animation connections; replace only the discovery connections.
    for(const auto &connection:m_sectionConnections)disconnect(connection);
    m_sectionConnections.clear(); m_sectionSources.clear(); m_seriesStations.clear();
    QList<SWMM2DResultsLayer *> unique=sources;
    if(m_results && !unique.contains(m_results.data()))unique.prepend(m_results.data());
    for(auto *source:unique) {
        if(!source)continue;
        bool seen=false; for(const auto &existing:m_sectionSources)if(existing==source)seen=true;
        if(seen)continue;
        m_sectionSources.append(source);
        m_sectionConnections.append(connect(source,&QObject::destroyed,this,[this] {
            m_seriesStations.clear(); refreshSourceChoices(); rebuildProfile();
        }));
        m_sectionConnections.append(connect(source,&SWMM2DResultsLayer::timeRangeChanged,this,[this] {
            m_seriesStations.clear(); refreshSourceChoices(); refreshSectionSeries();
        }));
        if(source!=m_results)m_sectionConnections.append(connect(source,&SWMM2DResultsLayer::currentTimeChanged,this,[this](int) { refreshSectionSeries(); }));
        const QString id=sourceId(source); bool referenced=false;
        for(const auto &ref:m_definition.sources)if(ref.id==id)referenced=true;
        if(!referenced)m_definition.sources.append({id,source->source() ? source->source()->sourcePath() : QString(),{}});
    }
    refreshSourceChoices(); refreshSectionSeries();
}
void MeshProfilePlotDialog::setSavedDefinitions(const QVector<Definition> &definitions)
{
    const QString selected=m_savedSections->currentData().toString(); m_savedSections->clear();
    for(const auto &definition:definitions)m_savedSections->addItem(definition.title.isEmpty() ? definition.id : definition.title,definition.id);
    const int index=m_savedSections->findData(selected); if(index>=0)m_savedSections->setCurrentIndex(index);
}
void MeshProfilePlotDialog::refreshSourceChoices()
{
    QVector<MeshProfileSeriesEditor::SourceChoice> choices;
    for(const auto &layer:m_sectionSources)if(layer && layer->source()) {
        MeshProfileSeriesEditor::SourceChoice choice; choice.id=sourceId(layer); choice.label=layer->name();
        if(choice.label.isEmpty())choice.label=QFileInfo(layer->source()->sourcePath()).fileName();
        choice.variables=layer->resultVariables(); choices.append(choice);
    }
    m_seriesEditor->setSources(choices); m_seriesEditor->setDefinition(m_definition);
}
void MeshProfilePlotDialog::refreshSectionSeries()
{
    if(!m_plot || !m_samplesModel)return;
    QDateTime requested=m_requestedTime;
    if(!requested.isValid() && m_results && m_results->source() && m_results->currentTimeIndex()>=0)requested=m_results->source()->simTimeAt(m_results->currentTimeIndex());
    QVector<SourceBinding> bindings;
    for(const auto &layer:m_sectionSources)if(layer && layer->source()) {
        SourceBinding binding; binding.sourceId=sourceId(layer); binding.source=layer->source();
        auto *srs=m_projectWindow && m_projectWindow->canvas() ? m_projectWindow->canvas()->canvasSRS() : layer->srs();
        binding.sceneCRS=srs ? srs->toWkt() : QString();
        for(const auto &ref:m_definition.sources)if(ref.id==binding.sourceId)binding.verticalDatum=ref.verticalDatum;
        if(layer==m_results && layer->currentTimeIndex()>=0)binding.surfaceTime=layer->source()->simTimeAt(layer->currentTimeIndex());
        const auto cached=m_seriesStations.constFind(binding.sourceId);
        if(cached!=m_seriesStations.cend() && cached->first==layer->geomRevision())binding.stations=cached->second;
        else {
            const auto independent=MeshProfileSampler::buildMeshProfile(nullptr,layer,m_scenePolyline);
            for(const auto &sample:independent.samples)binding.stations.append({sample.chainage,sample.scenePt,sample.triIdx,sample.breakBefore});
            m_seriesStations.insert(binding.sourceId,{layer->geomRevision(),binding.stations});
        }
        const QPointer<SWMM2DResultsLayer> guarded=layer;
        binding.mapCell=[guarded](const QPointF &point) { return guarded ? guarded->pickCellAt(point) : -1; };
        bindings.append(binding);
    }
    bool surfaceCovered = false;
    if (m_results && m_results->source() && m_results->source()->timeCount() > 0) {
        const auto *source = m_results->source();
        surfaceCovered = requested.isValid() && requested >= source->simTimeAt(0)
            && requested <= source->simTimeAt(source->timeCount()-1);
    }
    m_profile.hasResults = surfaceCovered;
    QString error;
    if(!sampleSeries(m_profile,m_definition,bindings,requested,&error))m_profile.series.clear();
    QStringList messages; if(!error.isEmpty())messages.append(error);
    if(m_results && !surfaceCovered)messages.append(tr("Surface result is outside its available time coverage."));
    for(const auto &series:m_profile.series)if(!series.error.isEmpty())messages.append(tr("%1: %2").arg(series.definition.label,series.error));
    m_sectionStatus->setText(messages.join(QStringLiteral("\n")));
    m_plot->setProfile(m_profile);
    m_plot->setAxisLabels(tr("Distance [%1]").arg(m_definition.horizontalUnits),tr("Elevation [%1]").arg(m_definition.elevationUnits));
    m_tracks->setSection(m_profile); m_tracks->setViewRange(m_plot->visibleDataRange()); m_tracks->setVisible(m_tracks->trackCount()>0);
    m_samplesModel->setSection(m_profile,m_definition);
}
void MeshProfilePlotDialog::buildSectionControls(QVBoxLayout *layout)
{
    auto *row=new QHBoxLayout;
    auto *title=new QLineEdit(m_definition.title,this); title->setObjectName("sectionTitle"); title->setAccessibleName(tr("Section title"));
    auto *titleLabel=new QLabel(tr("Section &title"),this); titleLabel->setBuddy(title);
    auto *save=new QPushButton(tr("&Save section"),this); save->setObjectName("sectionSave"); save->setAutoDefault(false); save->setToolTip(tr("Store this section in the project. Closing without saving discards this dialog's changes."));
    m_savedSections=new QComboBox(this); m_savedSections->setObjectName("savedSections"); m_savedSections->setAccessibleName(tr("Saved section"));
    auto *open=new QPushButton(tr("&Open saved"),this); open->setAutoDefault(false); open->setObjectName("sectionOpenSaved");
    auto *image=new QPushButton(tr("Export &image…"),this); image->setAutoDefault(false); image->setObjectName("sectionExportImage");
    auto *csv=new QPushButton(tr("Export &CSV…"),this); csv->setAutoDefault(false); csv->setObjectName("sectionExportCsv");
    row->addWidget(titleLabel); row->addWidget(title,1); row->addWidget(save); row->addWidget(m_savedSections,1); row->addWidget(open); row->addWidget(image); row->addWidget(csv); layout->addLayout(row);
    auto *tabs=new QTabWidget(this); tabs->setObjectName("sectionDetails"); tabs->setAccessibleName(tr("Section series and samples"));
    m_seriesEditor=new MeshProfileSeriesEditor(tabs); m_seriesEditor->setDefinition(m_definition);
    tabs->addTab(m_seriesEditor,tr("Series and styles"));
    m_samplesModel=new MeshProfileSamplesModel(this); m_samplesTable=new QTableView(tabs); m_samplesTable->setObjectName("sectionSamples");
    m_samplesTable->setModel(m_samplesModel); m_samplesTable->setSelectionBehavior(QAbstractItemView::SelectRows); m_samplesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_samplesTable->setAccessibleName(tr("Section samples, values, units and effective times"));
    m_samplesTable->setAccessibleDescription(tr("Select a row to move the section and map station cursor. Missing and waterless values remain explicitly identified."));
    m_samplesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tabs->addTab(m_samplesTable,tr("Sample table")); tabs->setMaximumHeight(270); layout->addWidget(tabs);
    m_sectionStatus=new QLabel(this); m_sectionStatus->setObjectName("sectionStatus"); m_sectionStatus->setTextFormat(Qt::PlainText); m_sectionStatus->setWordWrap(true); m_sectionStatus->setAccessibleName(tr("Section availability and validation")); layout->addWidget(m_sectionStatus);
    connect(title,&QLineEdit::textEdited,this,[this](const QString &text) { m_definition.title=text; emit definitionChanged(); });
    connect(m_seriesEditor,&MeshProfileSeriesEditor::definitionEdited,this,[this] { m_definition.series=m_seriesEditor->definition().series; refreshSectionSeries(); emit definitionChanged(); });
    connect(m_options,&MeshProfilePlotOptions::changed,this,[this] { if(!m_loadingDefinition)emit definitionChanged(); });
    connect(save,&QPushButton::clicked,this,[this] { QString error; const auto def=definition(); if(!validateDefinition(def,&error)) {m_sectionStatus->setText(error);return;} emit saveDefinitionRequested(def); });
    connect(open,&QPushButton::clicked,this,[this] { if(m_savedSections->currentIndex()>=0)emit openSavedDefinitionRequested(m_savedSections->currentData().toString()); });
    connect(image,&QPushButton::clicked,this,&MeshProfilePlotDialog::exportSectionImage);
    connect(csv,&QPushButton::clicked,this,&MeshProfilePlotDialog::exportSectionTable);
    connect(m_samplesTable->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this](const QModelIndex &current,const QModelIndex &) { if(!current.isValid())return; const double chain=current.data(Qt::UserRole).toDouble(); m_plot->setCursorChainage(chain); m_tracks->setCursorChainage(chain); emit m_plot->cursorChainageChanged(chain); });
}
void MeshProfilePlotDialog::exportSectionTable()
{
    QPointer<MeshProfilePlotDialog> guard(this);
    const QString path=QFileDialog::getSaveFileName(this,tr("Export section samples"),QString(),tr("CSV (*.csv)"));
    if(!guard || !m_contextValid || path.isEmpty())return;
    QString error; if(!exportSectionCsv(m_profile,definition(),path,&error))m_sectionStatus->setText(error);
    else m_sectionStatus->setText(tr("Section samples exported."));
}
void MeshProfilePlotDialog::exportSectionImage()
{
    QPointer<MeshProfilePlotDialog> guard(this);
    const QString path=QFileDialog::getSaveFileName(this,tr("Export section figure"),QString(),tr("PNG (*.png)"));
    if(!guard || !m_contextValid || path.isEmpty())return;
    const QSize size=m_figure->size();
    if(qint64(size.width())*size.height()>64000000) {m_sectionStatus->setText(tr("The figure is too large to export. Reduce the number of visible tracks."));return;}
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(palette().base().color()); m_figure->render(&image);
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG") || !file.commit()) {m_sectionStatus->setText(tr("The section image could not be saved."));return;}
    m_sectionStatus->setText(tr("Section figure exported."));
}
