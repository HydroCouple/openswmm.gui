#include "ui/util/numerictablewidgetitem.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/traceanalysisdialog.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/traceanalysislayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "output/outputstatsregistry.h"
#include "output/traceanalysisstore.h"
#include "output/tracecontroller.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include "ui/util/fileopendialog.h"
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <ogr_spatialref.h>

namespace openswmmvis::trace
{
namespace
{
QString number(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 6) : QObject::tr("Unavailable");
}
QString runId(SWMMVisProjectWindow *p, SWMMResultsLayer *layer)
{
    for (const auto &id : p->statsRegistry()->identities())
        if (id.layer == layer)
            return id.runId;
    return {};
}
} // namespace
TraceAnalysisDialog *TraceAnalysisDialog::showFor(SWMMVisProjectWindow *p, bool upstream,
                                                  bool travel, const QString &seed,
                                                  SWMMResultsLayer *output)
{
    if (!p || !p->modelLayer())
        return nullptr;
    auto *d = p->findChild<TraceAnalysisDialog *>("flowTraceDialog", Qt::FindDirectChildrenOnly);
    if (!d)
        d = new TraceAnalysisDialog(p);
    if (d->m_controller->busy())
    {
        d->m_progress->show();
        d->m_progress->raise();
        return d;
    }
    output = output ? output : p->activeResultsLayer();
    if (!output)
    {
        const auto path = openswmmvis::ui::FileOpenDialog::getOpenFileName(QStringLiteral("results-1d"), p, tr("Choose completed output"), {},
                                                       tr("SWMM output (*.out)"));
        if (path.isEmpty())
            return d;
        output = new SWMMResultsLayer(path, p->modelLayer(), p->modelLayer()->workspace());
        QList<QString> warnings, errors;
        if (!output->openResults(warnings, errors))
        {
            delete output;
            QMessageBox::warning(p, tr("Cannot open output"), errors.join('\n'));
            return d;
        }
        p->canvas()->addLayer(output);
        p->setActiveResultsLayer(output);
    }
    d->m_runId = runId(p, output);
    auto run = p->statsRegistry()->run(d->m_runId);
    if (!run.canAnalyze())
    {
        QMessageBox::information(p, tr("Analysis"),
                                 tr("Choose a completed output in Layers first."));
        return d;
    }
    d->m_upstream = upstream;
    d->m_travel = travel;
    QStringList nodes;
    if (!seed.isEmpty())
        nodes << seed;
    else
        for (const auto &item : p->modelLayer()->selectedElements())
            if (item.kinds & SWMMModelLayer::kKindNode)
                nodes << item.name;
    nodes.removeDuplicates();
    d->setSeedNodes(nodes);
    d->setWindowTitle(travel ? tr("Travel time") : tr("Flow balance"));
    d->m_prompt->setText(
        tr("%1\nClick a node on the map, or enter node IDs below.").arg(output->name()));
    const auto snapshot = run.snapshot.isEmpty() ? Snapshot::capture(p->modelLayer(), nullptr)
                                                 : Snapshot::fromJson(run.snapshot);
    QStringList ids;
    for (const auto &node : snapshot.nodes)
        ids << node.id;
    auto *completer = new QCompleter(ids, d->m_nodes);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    d->m_nodes->setCompleter(completer);
    if (!nodes.isEmpty())
        d->start();
    else
    {
        d->show();
        d->raise();
        p->canvas()->setCursor(Qt::CrossCursor);
    }
    return d;
}
TraceAnalysisDialog::TraceAnalysisDialog(SWMMVisProjectWindow *p)
    : QDialog(p), m_project(p), m_controller(TraceController::forProject(p))
{
    setObjectName("flowTraceDialog");
    auto *layout = new QVBoxLayout(this);
    m_prompt = new QLabel;
    m_prompt->setWordWrap(true);
    layout->addWidget(m_prompt);
    m_nodes = new QLineEdit;
    m_nodes->setObjectName("traceNodeIds");
    m_nodes->setPlaceholderText(tr("Node ID (or comma-separated IDs)"));
    layout->addWidget(m_nodes);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create layer"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TraceAnalysisDialog::start);
    connect(buttons, &QDialogButtonBox::rejected, this, &TraceAnalysisDialog::reject);
    resize(420, 130);
    m_progress = new QProgressDialog(tr("Preparing analysis…"), tr("Cancel"), 0, 0, p);
    m_progress->setObjectName("traceComputeProgress");
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->hide();
    connect(m_progress, &QProgressDialog::canceled, m_controller, &TraceController::cancel);
    connect(m_controller, &TraceController::progress, this,
            [this](int percent, const QString &stage)
            {
                if (!m_pending)
                    return;
                m_progress->setRange(0, 100);
                m_progress->setValue(percent);
                m_progress->setLabelText(stage);
            });
    connect(m_controller, &TraceController::resultReady, this,
            [this](std::shared_ptr<Result> r)
            {
                if (m_pending && r->dataset->runId == m_runId)
                    m_controller->attachResult(r, m_travel);
            });
    connect(m_controller, &TraceController::failed, this,
            [this](const QString &message)
            {
                if (!m_pending)
                    return;
                m_pending = false;
                m_progress->hide();
                QMessageBox::warning(m_project, tr("Analysis could not finish"), message);
            });
    connect(m_controller, &TraceController::finished, this,
            [this](const QString &id, bool)
            {
                if (id != m_runId)
                    return;
                m_pending = false;
                m_progress->hide();
            });
    qApp->installEventFilter(this);
}
void TraceAnalysisDialog::setSeedNodes(const QStringList &nodes)
{
    m_nodes->setText(nodes.join(", "));
}
QJsonObject TraceAnalysisDialog::state() const
{
    return {{"runId", m_runId},
            {"nodes", m_nodes->text()},
            {"direction", m_upstream ? 1 : 0},
            {"tab", m_travel ? 1 : 0}};
}
void TraceAnalysisDialog::restore(const QJsonObject &j)
{
    m_runId = j.value("runId").toString();
    m_nodes->setText(j.value("nodes").toString());
    m_upstream = j.value("direction").toInt() == 1;
    m_travel = j.value("tab").toInt() == 1;
}
void TraceAnalysisDialog::reject()
{
    m_project->canvas()->unsetCursor();
    QDialog::reject();
}
void TraceAnalysisDialog::start()
{
    QStringList seeds;
    for (const auto &id : m_nodes->text().split(','))
        if (!id.trimmed().isEmpty())
            seeds << id.trimmed();
    if (seeds.isEmpty())
        return;
    hide();
    m_project->canvas()->unsetCursor();
    m_project->setProperty("tracePanelState", state());
    m_pending = true;
    m_progress->reset();
    m_progress->setRange(0, 0);
    const auto run = m_project->statsRegistry()->run(m_runId);
    m_progress->setWindowTitle(m_travel ? tr("Travel time") : tr("Flow balance"));
    m_progress->setLabelText(run.packagePath.isEmpty()
                                 ? tr("Preparing average flows for this output…")
                                 : tr("Using saved flow analysis…"));
    m_progress->show();
    m_controller->run(m_runId, seeds, m_upstream ? 1 : 0, {},
                      Snapshot::capture(m_project->modelLayer(), nullptr));
}
bool TraceAnalysisDialog::eventFilter(QObject *object, QEvent *event)
{
    if (!isVisible())
        return QDialog::eventFilter(object, event);
    if (event->type() != QEvent::MouseButtonPress)
        return false;
    auto *widget = qobject_cast<QWidget *>(object);
    auto *canvas = m_project->canvas();
    if (!widget || (widget != canvas && !canvas->isAncestorOf(widget)))
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton)
        return false;
    const auto click = canvas->mapFromGlobal(mouse->globalPosition().toPoint());
    const auto run = m_project->statsRegistry()->run(m_runId);
    const auto snapshot = run.snapshot.isEmpty()
                              ? Snapshot::capture(m_project->modelLayer(), nullptr)
                              : Snapshot::fromJson(run.snapshot);
    std::unique_ptr<SpatialReferenceSystem> crs(
        SpatialReferenceSystem::fromWktOrProj(snapshot.wkt));
    auto *ct =
        crs && canvas->canvasSRS() ? crs->createTransformationTo(*canvas->canvasSRS()) : nullptr;
    if (crs && canvas->canvasSRS() && !crs->equals(*canvas->canvasSRS()) && !ct)
        return true;
    double best = 16;
    QString found;
    for (const auto &node : snapshot.nodes)
    {
        if (!node.hasGeometry)
            continue;
        double x = node.point.x(), y = node.point.y();
        if (ct && !ct->Transform(1, &x, &y))
            continue;
        int px, py;
        canvas->toPixelCoords(x, y, px, py);
        double distance = QLineF(click, QPointF(px, py)).length();
        if (distance < best)
        {
            best = distance;
            found = node.id;
        }
    }
    if (ct)
        OCTDestroyCoordinateTransformation(ct);
    if (!found.isEmpty())
    {
        setSeedNodes({found});
        start();
    }
    return true;
}
void TraceAnalysisDialog::showProperties(TraceSublayer *part, QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("traceStyleDialog");
    dialog->setWindowTitle(part->displayName());
    dialog->resize(700, 740);
    QPointer<TraceAnalysisLayer> layer = part->layer();
    QPointer<TraceStyle> style = layer->traceStyle();
    const auto snapshot = style->toJson();
    auto *layout = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("traceStyleTabs");
    layout->addWidget(tabs);
    QList<openswmmvis::ui::ClassificationEditor *> colorEditors;
    QList<std::function<bool()>> validSizes;
    const auto fields = [](bool node, bool labels)
    {
        QStringList out{"ratio", "flow", "gross", "time", "local", "coverage"};
        if (node)
            out.append("volume");
        out.append(labels ? "id" : "uniform");
        return out;
    };
    const auto fieldCombo = [&](bool node, bool labels, const QString &value)
    {
        auto *combo = new QComboBox;
        for (const auto &key : fields(node, labels))
            combo->addItem(key == "id" ? tr("Object ID only") : fieldLabel(key, node), key);
        combo->setCurrentIndex(combo->findData(value));
        return combo;
    };
    const auto spin = [](double value, double min, double max, int decimals, QString suffix = {})
    {
        auto *box = new QDoubleSpinBox;
        box->setDecimals(decimals);
        box->setRange(min, max);
        box->setValue(value);
        box->setSuffix(suffix);
        box->setKeyboardTracking(false);
        return box;
    };
    for (bool node : {false, true})
    {
        auto *page = new QWidget;
        auto *pageLayout = new QVBoxLayout(page);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        tabs->addTab(scroll, node ? tr("Nodes") : tr("Links"));
        auto *color = node ? &style->nodeColor : &style->linkColor;
        auto *size = node ? &style->nodeSize : &style->linkWidth;
        auto *colorField = fieldCombo(node, false, color->field);
        colorField->setObjectName(node ? "traceNodeColorField" : "traceLinkColorField");
        auto *colorForm = new QFormLayout;
        colorForm->addRow(tr("Color by"), colorField);
        pageLayout->addLayout(colorForm);
        auto *binding = new openswmmvis::ui::SublayerSchemeBinding(
            [style, color]
            { return style ? color->colors : OpenSWMM::Render::ClassificationScheme{}; },
            [style, color](const OpenSWMM::Render::ClassificationScheme &scheme)
            {
                if (!style)
                    return;
                color->colors = scheme;
                color->transform = "linear";
                style->changed();
            },
            [layer, color, node]
            { return layer ? layer->samples(node, *color) : QVector<double>{}; },
            [layer, color, node]
            {
                if (!layer)
                    return QPair<double, double>{0, 1};
                auto automatic = *color;
                automatic.automatic = true;
                return layer->range(node, automatic);
            },
            true, false);
        auto *editor = new openswmmvis::ui::ClassificationEditor(binding, true);
        editor->setObjectName(node ? "traceNodeColorScale" : "traceLinkColorScale");
        colorEditors.append(editor);
        pageLayout->addWidget(editor);
        QObject::connect(colorField, &QComboBox::currentIndexChanged, dialog,
                         [style, color, colorField, editor]
                         {
                             if (!style)
                                 return;
                             color->field = colorField->currentData().toString();
                             style->changed();
                             editor->refresh();
                         });
        auto *sizeBox =
            new QGroupBox(node ? tr("Size scale — node area") : tr("Size scale — link width"));
        auto *form = new QFormLayout(sizeBox);
        pageLayout->addWidget(sizeBox);
        auto *sizeField = fieldCombo(node, false, size->field);
        sizeField->setObjectName(node ? "traceNodeSizeField" : "traceLinkSizeField");
        form->addRow(tr("Size by"), sizeField);
        auto *mode = new QComboBox;
        mode->addItem(tr("Proportional flow widths"), true);
        mode->addItem(tr("Custom size scale"), false);
        mode->setCurrentIndex(size->proportional ? 0 : 1);
        if (!node)
            form->addRow(tr("Scaling"), mode);
        else
        {
            mode->setParent(sizeBox);
            mode->hide();
        }
        mode->setObjectName(node ? "traceNodeSizeMode" : "traceLinkSizeMode");
        auto *transform = new QComboBox;
        transform->addItem(tr("Linear"), "linear");
        transform->addItem(tr("Square root"), "sqrt");
        transform->addItem(tr("Logarithmic (log 1 + value)"), "log");
        transform->setCurrentIndex(transform->findData(size->transform));
        form->addRow(tr("Scale"), transform);
        auto *automatic = new QCheckBox(tr("Automatic value range"));
        automatic->setChecked(size->automatic);
        form->addRow(automatic);
        auto *rangeRow = new QHBoxLayout;
        auto r = layer->range(node, *size);
        if (!node && size->proportional)
            r = {0, layer->widthReference()};
        auto *minimum = spin(r.first, 0, 1e15, 8);
        auto *maximum = spin(r.second, 0, 1e15, 8);
        minimum->setObjectName(node ? "traceNodeRangeMin" : "traceLinkRangeMin");
        maximum->setObjectName(node ? "traceNodeRangeMax" : "traceLinkRangeMax");
        rangeRow->addWidget(new QLabel(tr("Min")));
        rangeRow->addWidget(minimum);
        rangeRow->addWidget(new QLabel(tr("Max")));
        rangeRow->addWidget(maximum);
        form->addRow(tr("Values"), rangeRow);
        auto *pixelsRow = new QHBoxLayout;
        auto *minPixels =
            spin(!node && size->proportional ? 0 : size->minimumSize, 0, 80, 1, tr(" px"));
        auto *maxPixels = spin(size->maximumSize, 0.1, 80, 1, tr(" px"));
        minPixels->setObjectName(node ? "traceNodeMinSize" : "traceLinkMinSize");
        maxPixels->setObjectName(node ? "traceNodeMaxSize" : "traceLinkMaxSize");
        pixelsRow->addWidget(new QLabel(tr("Min")));
        pixelsRow->addWidget(minPixels);
        pixelsRow->addWidget(new QLabel(tr("Max")));
        pixelsRow->addWidget(maxPixels);
        form->addRow(node ? tr("Diameter") : tr("Width"), pixelsRow);
        auto *hint = new QLabel;
        hint->setWordWrap(true);
        form->addRow(hint);
        const auto valid = [=]
        {
            bool proportional = !node && mode->currentData().toBool();
            return (proportional || minPixels->value() <= maxPixels->value()) &&
                   (automatic->isChecked() ||
                    maximum->value() > (proportional ? 0 : minimum->value()));
        };
        validSizes.append(valid);
        const auto refresh = [=]
        {
            bool proportional = !node && mode->currentData().toBool();
            transform->setEnabled(!proportional);
            minPixels->setEnabled(!proportional);
            minimum->setEnabled(!proportional && !automatic->isChecked());
            maximum->setEnabled(!automatic->isChecked());
            hint->setText(
                !valid() ? tr("Maximum values must exceed minimum values; minimum size cannot "
                              "exceed maximum size.")
                : proportional ? tr("Widths start at zero and add at balanced junctions. The "
                                    "maximum is the combined junction flow; a fixed maximum sets "
                                    "the reference, without clipping larger flows.")
                : node ? tr("Marker area follows the selected value scale. Uniform uses the "
                            "maximum diameter.")
                       : tr("Custom widths use these size limits. Uniform uses the maximum width; "
                            "custom scales do not preserve flow-width sums."));
        };
        const auto update = [=]
        {
            if (!style)
                return;
            // Keep the displayed automatic limits tied to the chosen field.
            if (automatic->isChecked() && layer)
            {
                auto channel = *size;
                channel.field = sizeField->currentData().toString();
                channel.automatic = true;
                auto bounds = layer->range(node, channel);
                if (!node && mode->currentData().toBool())
                    bounds = {0, layer->widthReference(channel)};
                QSignalBlocker blockMin(minimum), blockMax(maximum);
                minimum->setValue(bounds.first);
                maximum->setValue(bounds.second);
            }
            refresh();
            if (!valid())
                return;
            size->field = sizeField->currentData().toString();
            size->proportional = mode->currentData().toBool();
            size->transform = transform->currentData().toString();
            size->automatic = automatic->isChecked();
            size->minimum = minimum->value();
            size->maximum = maximum->value();
            size->minimumSize = minPixels->value();
            size->maximumSize = maxPixels->value();
            style->changed();
        };
        for (auto *combo : {sizeField, mode, transform})
            QObject::connect(combo, &QComboBox::currentIndexChanged, dialog, update);
        for (auto *box : {minimum, maximum, minPixels, maxPixels})
            QObject::connect(box, &QDoubleSpinBox::valueChanged, dialog, update);
        QObject::connect(automatic, &QCheckBox::toggled, dialog, update);
        refresh();
        if (!node)
        {
            auto *arrows = new QCheckBox(tr("Flow direction arrows"));
            arrows->setChecked(style->arrows());
            pageLayout->addWidget(arrows);
            QObject::connect(arrows, &QCheckBox::toggled, dialog,
                             [style](bool value)
                             {
                                 if (style)
                                     style->setArrows(value);
                             });
        }
        pageLayout->addStretch();
    }
    auto *labelPage = new QWidget;
    auto *labelLayout = new QVBoxLayout(labelPage);
    tabs->addTab(labelPage, tr("Labels"));
    auto *objects = new QComboBox;
    objects->addItems({tr("Links"), tr("Nodes")});
    labelLayout->addWidget(objects);
    auto *labelStack = new QStackedWidget;
    labelLayout->addWidget(labelStack);
    QObject::connect(objects, &QComboBox::currentIndexChanged, labelStack,
                     &QStackedWidget::setCurrentIndex);
    for (bool node : {false, true})
    {
        auto *labels = node ? &style->nodeLabels : &style->linkLabels;
        auto *page = new QWidget;
        labelStack->addWidget(page);
        auto *form = new QFormLayout(page);
        auto *enabled = new QCheckBox(node ? tr("Show node labels") : tr("Show link labels"));
        enabled->setObjectName(node ? "traceNodeLabels" : "traceLinkLabels");
        enabled->setChecked(labels->appearance.enabled);
        form->addRow(enabled);
        auto *field = fieldCombo(node, true, labels->appearance.fieldName);
        field->setObjectName(node ? "traceNodeLabelField" : "traceLinkLabelField");
        form->addRow(tr("Label with"), field);
        auto *showId = new QCheckBox(tr("Include object ID"));
        showId->setChecked(labels->showId);
        form->addRow(showId);
        auto *percent = new QCheckBox(tr("Display fractions as percentages"));
        percent->setChecked(labels->percent);
        form->addRow(percent);
        auto *precision = new QSpinBox;
        precision->setRange(0, 8);
        precision->setValue(labels->precision);
        form->addRow(tr("Decimal places"), precision);
        auto *font = new QPushButton(labels->appearance.font.family().isEmpty()
                                         ? tr("System font…")
                                         : labels->appearance.font.family() + "…");
        form->addRow(tr("Font"), font);
        auto *fontSize = spin(labels->appearance.fontSizePt, 5, 40, 1, tr(" pt"));
        form->addRow(tr("Text size"), fontSize);
        auto *color = new openswmmvis::ui::ColorButton(labels->appearance.color);
        form->addRow(tr("Text color"), color);
        auto *halo = new QCheckBox(tr("Text halo"));
        halo->setChecked(labels->appearance.haloEnabled);
        auto *haloColor = new openswmmvis::ui::ColorButton(labels->appearance.haloColor);
        auto *haloSize = spin(labels->appearance.haloRadiusPx, .1, 6, 1, tr(" px"));
        auto *haloRow = new QHBoxLayout;
        haloRow->addWidget(halo);
        haloRow->addWidget(haloColor);
        haloRow->addWidget(haloSize);
        form->addRow(haloRow);
        auto *preview = new QLabel;
        preview->setObjectName(node ? "traceNodeLabelPreview" : "traceLinkLabelPreview");
        preview->setWordWrap(true);
        form->addRow(tr("Preview"), preview);
        auto *note = new QLabel(
            tr("Flow magnitudes use m³/s and times use minutes. Partial travel times include their "
               "known-path coverage; missing values display Unavailable."));
        note->setWordWrap(true);
        form->addRow(note);
        const auto refresh = [=]
        {
            const auto key = field->currentData().toString();
            showId->setEnabled(key != "id");
            percent->setEnabled(key == "ratio" || key == "coverage");
            haloColor->setEnabled(halo->isChecked());
            haloSize->setEnabled(halo->isChecked());
            QString text;
            if (layer)
            {
                const auto &values = node ? layer->result()->nodes : layer->result()->links;
                for (int i = 0; i < values.size(); ++i)
                    if (values[i].ratio != 0)
                    {
                        text = layer->labelText(node, i);
                        break;
                    }
            }
            preview->setText(text.isEmpty() ? tr("Labels are hidden") : text);
        };
        const auto update = [=]
        {
            if (!style)
                return;
            labels->appearance.enabled = enabled->isChecked();
            labels->appearance.fieldName = field->currentData().toString();
            labels->showId = showId->isChecked();
            labels->percent = percent->isChecked();
            labels->precision = precision->value();
            labels->appearance.fontSizePt = fontSize->value();
            labels->appearance.color = color->color();
            labels->appearance.haloEnabled = halo->isChecked();
            labels->appearance.haloColor = haloColor->color();
            labels->appearance.haloRadiusPx = haloSize->value();
            style->changed();
            refresh();
        };
        for (auto *check : {enabled, showId, percent, halo})
            QObject::connect(check, &QCheckBox::toggled, dialog, update);
        QObject::connect(field, &QComboBox::currentIndexChanged, dialog, update);
        QObject::connect(precision, &QSpinBox::valueChanged, dialog, update);
        QObject::connect(font, &QPushButton::clicked, dialog,
                         [=]
                         {
                             if (!style)
                                 return;
                             bool accepted = false;
                             const auto chosen =
                                 QFontDialog::getFont(&accepted, labels->appearance.effectiveFont(),
                                                      dialog, tr("Label font"));
                             if (!accepted || !style)
                                 return;
                             labels->appearance.font = chosen;
                             font->setText(chosen.family() + "…");
                             fontSize->setValue(chosen.pointSizeF());
                             update();
                         });
        for (auto *box : {fontSize, haloSize})
            QObject::connect(box, &QDoubleSpinBox::valueChanged, dialog, update);
        for (auto *button : {color, haloColor})
            QObject::connect(button, &openswmmvis::ui::ColorButton::colorChanged, dialog, update);
        refresh();
    }
    labelLayout->addStretch();
    auto *details = new QPushButton(tr("Result details…"));
    layout->addWidget(details);
    QObject::connect(details, &QPushButton::clicked, part,
                     [part, parent] { showDetails(part, parent); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog,
                     [dialog, colorEditors, validSizes]
                     {
                         for (auto *editor : colorEditors)
                             if (!editor->hasValidDraft())
                             {
                                 QMessageBox::warning(
                                     dialog, tr("Check color scale"),
                                     tr("Correct the color range or class breaks before saving."));
                                 return;
                             }
                         for (const auto &valid : validSizes)
                             if (!valid())
                             {
                                 QMessageBox::warning(
                                     dialog, tr("Check size scale"),
                                     tr("Correct the value range and size limits before saving."));
                                 return;
                             }
                         dialog->accept();
                     });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(dialog, &QDialog::rejected, dialog,
                     [style, snapshot]
                     {
                         if (style)
                             style->fromJson(snapshot);
                     });
    QObject::connect(part, &QObject::destroyed, dialog,
                     [dialog]
                     {
                         // The layer is being destroyed; rolling back its style
                         // would emit invalidation through a partially destroyed
                         // sublayer. Close without applying a Cancel rollback.
                         QSignalBlocker blocker(dialog);
                         dialog->reject();
                     });
    dialog->show();
}
void TraceAnalysisDialog::showDetails(TraceSublayer *part, QWidget *parent)
{
    auto result = part->layer()->result();
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(part->displayName());
    dialog->resize(780, 450);
    auto *layout = new QVBoxLayout(dialog);
    auto *note = new QLabel(tr("%1\nWhole-report average-flow estimate. Times are in minutes; "
                               "missing paths report their coverage. Hydraulic balance is partial.")
                                .arg(result->dataset->label));
    const auto &summary = result->summary;
    note->setText(
        note->text() +
        tr("\nBoundary inflow: %1 m³ · outflow: %2 m³ · partial balance residual: %3 m³.\n%4")
            .arg(number(summary.boundary_in_m3), number(summary.boundary_out_m3),
                 number(summary.partial_balance_residual_m3), result->dataset->provenance));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *table = new QTableWidget;
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({tr("Type"), tr("ID"), tr("Flow fraction"),
                                      tr("Mean flow m³/s"), tr("Time min"), tr("Coverage %"),
                                      tr("Status")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    layout->addWidget(table);
    for (bool node : {true, false})
    {
        const auto &values = node ? result->nodes : result->links;
        for (int i = 0; i < values.size(); ++i)
        {
            const auto &v = values[i];
            if (v.ratio == 0)
                continue;
            int row = table->rowCount();
            table->insertRow(row);
            QStringList cells{node ? tr("Node") : tr("Link"),
                              node ? result->dataset->snapshot.nodes[i].id
                                   : result->dataset->snapshot.links[i].id,
                              number(v.ratio),
                              number(part->layer()->value(node, i, "flow")),
                              number(v.time_s / 60),
                              number(100 * v.time_coverage),
                              statusText(v.flags)};
            for (int col = 0; col < cells.size(); ++col)
                table->setItem(row, col, new openswmmvis::ui::NumericTableWidgetItem(cells[col]));
        }
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table->setSortingEnabled(true);
    auto *exportButton = new QPushButton(tr("Export CSV…"));
    layout->addWidget(exportButton);
    QObject::connect(
        exportButton, &QPushButton::clicked, dialog,
        [dialog, table, result]
        {
            const auto path =
                QFileDialog::getSaveFileName(dialog, tr("Export analysis"), {}, tr("CSV (*.csv)"));
            if (path.isEmpty())
                return;
            auto quote = [](QString text) { return '"' + text.replace('"', "\"\"") + '"'; };
            QByteArray bytes = "Run,Analysis,Type,ID,Flow fraction,Mean flow m3/s,Time "
                               "min,Coverage percent,Status\n";
            for (int row = 0; row < table->rowCount(); ++row)
            {
                QStringList cells{quote(result->dataset->runId), quote(result->id)};
                for (int col = 0; col < table->columnCount(); ++col)
                    cells << quote(table->item(row, col)->text());
                bytes += cells.join(',').toUtf8() + '\n';
            }
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                !file.commit())
                QMessageBox::warning(dialog, tr("Cannot save"), file.errorString());
        });
    if (!result->saved)
    {
        auto *retry = new QPushButton(tr("Retry saving analysis"));
        layout->addWidget(retry);
        QObject::connect(
            retry, &QPushButton::clicked, dialog,
            [dialog, result, retry]
            {
                QString error;
                if (!AnalysisStore::save(result->dataset->packagePath, *result, &error))
                    QMessageBox::warning(dialog, tr("Cannot save"), error);
                else
                {
                    result->saved = true;
                    retry->setEnabled(false);
                }
            });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->show();
}
} // namespace openswmmvis::trace
