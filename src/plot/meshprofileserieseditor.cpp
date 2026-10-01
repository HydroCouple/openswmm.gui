#include "plot/meshprofileserieseditor.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>

using namespace ProfileSection;
MeshProfileSeriesEditor::MeshProfileSeriesEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto *addRow = new QHBoxLayout;
    m_source = new QComboBox(this); m_source->setObjectName("sectionSource");
    m_variable = new QComboBox(this); m_variable->setObjectName("sectionVariable");
    auto *sourceLabel = new QLabel(tr("&Source"),this); sourceLabel->setBuddy(m_source);
    auto *variableLabel = new QLabel(tr("&Quantity"),this); variableLabel->setBuddy(m_variable);
    m_source->setAccessibleName(tr("Section result source")); m_variable->setAccessibleName(tr("Section quantity"));
    auto *add = new QPushButton(tr("&Add series"),this); add->setObjectName("sectionAddSeries"); add->setAutoDefault(false);
    auto *remove = new QPushButton(tr("&Remove selected"),this); remove->setObjectName("sectionRemoveSeries"); remove->setAutoDefault(false);
    addRow->addWidget(sourceLabel); addRow->addWidget(m_source,1); addRow->addWidget(variableLabel); addRow->addWidget(m_variable,2); addRow->addWidget(add); addRow->addWidget(remove);
    layout->addLayout(addRow);
    m_table = new QTableWidget(0,10,this); m_table->setObjectName("sectionSeries");
    m_table->setHorizontalHeaderLabels({tr("Show"),tr("Legend label"),tr("Source / quantity"),tr("Axis"),tr("Time policy"),tr("Color"),tr("Line"),tr("Width"),tr("Opacity"),tr("Scalar range")});
    m_table->setAccessibleName(tr("Section series and independent styles"));
    m_table->setAccessibleDescription(tr("Select rows to remove. Each scalar quantity has its own axis and units. Unknown or missing quantities remain listed."));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    layout->addWidget(m_table);
    m_error = new QLabel(this); m_error->setObjectName("sectionSeriesError"); m_error->setWordWrap(true); m_error->setTextFormat(Qt::PlainText);
    m_error->setAccessibleName(tr("Section configuration status")); layout->addWidget(m_error);
    connect(m_source,&QComboBox::currentIndexChanged,this,&MeshProfileSeriesEditor::refreshVariables);
    connect(add,&QPushButton::clicked,this,&MeshProfileSeriesEditor::addSeries);
    connect(remove,&QPushButton::clicked,this,[this] {
        QList<int> rows; for (const auto &index : m_table->selectionModel()->selectedRows()) rows.append(index.row());
        std::sort(rows.begin(),rows.end(),std::greater<int>());
        for (int row : rows) m_definition.series.removeAt(row);
        if (!rows.isEmpty()) { rebuildRows(); announceEdit(); }
    });
}
void MeshProfileSeriesEditor::setDefinition(const Definition &definition) { m_definition = definition; rebuildRows(); }
void MeshProfileSeriesEditor::setSources(const QVector<SourceChoice> &sources)
{
    const QString selected = m_source->currentData().toString(); m_sources = sources;
    m_source->clear(); for (const auto &source : sources) m_source->addItem(source.label,source.id);
    const int index = m_source->findData(selected); if (index >= 0) m_source->setCurrentIndex(index);
    refreshVariables(); rebuildRows();
}
void MeshProfileSeriesEditor::refreshVariables()
{
    m_variable->clear();
    for (const auto &source : m_sources) if (source.id == m_source->currentData().toString())
        for (const auto &variable : source.variables)
            m_variable->addItem(QStringLiteral("%1 [%2]").arg(variable.label,
                variable.unitsKnown ? variable.units : tr("units unknown")),variable.key());
}
void MeshProfileSeriesEditor::addSeries()
{
    if (m_source->currentIndex() < 0 || m_variable->currentIndex() < 0) {
        m_error->setText(tr("Choose an available source and quantity before adding a series.")); m_variable->setFocus(); return;
    }
    SeriesDefinition series; series.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    series.sourceId = m_source->currentData().toString(); series.variableKey = m_variable->currentData().toString();
    for (const auto &source : m_sources) if (source.id == series.sourceId)
        for (const auto &v : source.variables) if (v.key() == series.variableKey) {
            series.label = v.label;
            if (v.dataset == "Mesh2_face_gw_table_elev" || v.dataset == "Mesh2_face_gw_bed_elev") {
                series.role = SeriesRole::Elevation;
                series.pen = QPen(QColor("#16836b"),2,v.dataset.endsWith("bed_elev") ? Qt::DashLine : Qt::SolidLine);
            }
        }
    m_definition.series.append(series); rebuildRows(); announceEdit();
}
void MeshProfileSeriesEditor::announceEdit()
{
    if (m_refreshing) return;
    QString error; validateDefinition(m_definition,&error); m_error->setText(error);
    emit definitionEdited();
}
void MeshProfileSeriesEditor::rebuildRows()
{
    m_refreshing = true; m_table->setRowCount(0);
    for (int row = 0; row < m_definition.series.size(); ++row) {
        m_table->insertRow(row); const auto def = m_definition.series[row];
        const auto identify = [row](QWidget *widget,const QString &field) { widget->setAccessibleName(QObject::tr("Series %1, %2").arg(row+1).arg(field)); widget->setAccessibleDescription(widget->accessibleName()); };
        auto *visible = new QCheckBox(this); visible->setChecked(def.visible); identify(visible,tr("Visible")); m_table->setCellWidget(row,0,visible);
        auto *label = new QLineEdit(def.label,this); identify(label,tr("Legend label")); m_table->setCellWidget(row,1,label);
        QString sourceName = def.sourceId, quantity = def.variableKey; bool found = false;
        for (const auto &source : m_sources) if (source.id == def.sourceId) {
            sourceName = source.label; for (const auto &v : source.variables) if (v.key() == def.variableKey) { quantity = v.label; found = true; }
        }
        auto *identity = new QTableWidgetItem(QStringLiteral("%1 / %2%3").arg(sourceName,quantity,found ? QString() : tr(" (unavailable)")));
        identity->setFlags(identity->flags() & ~Qt::ItemIsEditable); m_table->setItem(row,2,identity);
        auto *role = new QComboBox(this); role->addItem(tr("Elevation"),int(SeriesRole::Elevation)); role->addItem(tr("Separate scalar"),int(SeriesRole::Scalar)); role->setCurrentIndex(role->findData(int(def.role))); identify(role,tr("Axis")); m_table->setCellWidget(row,3,role);
        auto *time = new QComboBox(this); time->addItem(tr("Exact report"),int(TimePolicy::Exact)); time->addItem(tr("Previous report"),int(TimePolicy::Hold)); time->setCurrentIndex(time->findData(int(def.timePolicy))); identify(time,tr("Time policy")); time->setToolTip(tr("No value is shown outside the run's time coverage. Previous report does not interpolate.")); m_table->setCellWidget(row,4,time);
        auto *color = new QPushButton(def.pen.color().name(QColor::HexArgb),this); color->setAutoDefault(false); identify(color,tr("Line color")); m_table->setCellWidget(row,5,color);
        auto *dash = new QComboBox(this); dash->addItem(tr("Solid"),int(Qt::SolidLine)); dash->addItem(tr("Dashed"),int(Qt::DashLine)); dash->addItem(tr("Dotted"),int(Qt::DotLine)); dash->addItem(tr("Dash-dot"),int(Qt::DashDotLine)); dash->setCurrentIndex(dash->findData(int(def.pen.style()))); identify(dash,tr("Line pattern")); m_table->setCellWidget(row,6,dash);
        auto *width = new QDoubleSpinBox(this); width->setRange(0.1,32); width->setDecimals(2); width->setValue(def.pen.widthF()); identify(width,tr("Line width in pixels")); m_table->setCellWidget(row,7,width);
        auto *opacity = new QDoubleSpinBox(this); opacity->setRange(0,1); opacity->setSingleStep(0.1); opacity->setDecimals(2); opacity->setValue(def.opacity); identify(opacity,tr("Opacity")); m_table->setCellWidget(row,8,opacity);
        auto *range = new QWidget(this); auto *rangeLayout = new QHBoxLayout(range); rangeLayout->setContentsMargins(0,0,0,0);
        auto *custom = new QCheckBox(tr("Fixed"),range); custom->setChecked(def.customRange); identify(custom,tr("Fixed scalar range"));
        auto *minimum = new QDoubleSpinBox(range), *maximum = new QDoubleSpinBox(range);
        for (auto *spin : {minimum,maximum}) { spin->setRange(-1e12,1e12); spin->setDecimals(6); spin->setEnabled(def.customRange && def.role == SeriesRole::Scalar); }
        minimum->setValue(def.minimum); maximum->setValue(def.maximum); identify(minimum,tr("Scalar minimum")); identify(maximum,tr("Scalar maximum"));
        custom->setEnabled(def.role == SeriesRole::Scalar); rangeLayout->addWidget(custom); rangeLayout->addWidget(minimum); rangeLayout->addWidget(maximum); m_table->setCellWidget(row,9,range);
        connect(visible,&QCheckBox::toggled,this,[this,row](bool on) { m_definition.series[row].visible=on; announceEdit(); });
        connect(label,&QLineEdit::textEdited,this,[this,row](const QString &text) { m_definition.series[row].label=text; announceEdit(); });
        connect(role,&QComboBox::currentIndexChanged,this,[this,row,role,custom,minimum,maximum] { auto &s=m_definition.series[row]; s.role=SeriesRole(role->currentData().toInt()); custom->setEnabled(s.role==SeriesRole::Scalar); minimum->setEnabled(s.customRange&&s.role==SeriesRole::Scalar); maximum->setEnabled(minimum->isEnabled()); announceEdit(); });
        connect(time,&QComboBox::currentIndexChanged,this,[this,row,time] { m_definition.series[row].timePolicy=TimePolicy(time->currentData().toInt()); announceEdit(); });
        connect(color,&QPushButton::clicked,this,[this,row,color] {
            const QString id=m_definition.series[row].id; QPointer<MeshProfileSeriesEditor> guard(this); QPointer<QPushButton> button(color);
            const QColor result=QColorDialog::getColor(m_definition.series[row].pen.color(),this,tr("Series line color"),QColorDialog::ShowAlphaChannel);
            if (!guard || !button || !result.isValid()) return;
            for (auto &s : m_definition.series) if (s.id==id) { s.pen.setColor(result); button->setText(result.name(QColor::HexArgb)); announceEdit(); break; }
        });
        connect(dash,&QComboBox::currentIndexChanged,this,[this,row,dash] { m_definition.series[row].pen.setStyle(Qt::PenStyle(dash->currentData().toInt())); announceEdit(); });
        connect(width,&QDoubleSpinBox::valueChanged,this,[this,row](double value) { m_definition.series[row].pen.setWidthF(value); announceEdit(); });
        connect(opacity,&QDoubleSpinBox::valueChanged,this,[this,row](double value) { m_definition.series[row].opacity=value; announceEdit(); });
        connect(custom,&QCheckBox::toggled,this,[this,row,minimum,maximum](bool on) { m_definition.series[row].customRange=on; minimum->setEnabled(on); maximum->setEnabled(on); announceEdit(); });
        connect(minimum,&QDoubleSpinBox::valueChanged,this,[this,row](double value) { m_definition.series[row].minimum=value; announceEdit(); });
        connect(maximum,&QDoubleSpinBox::valueChanged,this,[this,row](double value) { m_definition.series[row].maximum=value; announceEdit(); });
    }
    m_refreshing=false;
}
