/*!
 * \file   attributepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributepanel.h"
#include "map/tools/maptoolidentify.h"   // IdentifyResult
#include "layers/openswmmvislayer.h"

// QPropertyModel library
#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>
#endif

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AttributePanel::AttributePanel(QWidget *parent)
    : QDockWidget(tr("Attributes"), parent)
{
    setupUi();
}

AttributePanel::~AttributePanel() = default;

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void AttributePanel::setupUi()
{
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(4);

    // Layer selector row
    auto *hlay = new QHBoxLayout;
    hlay->addWidget(new QLabel(tr("Layer:"), central));
    m_layerCombo = new QComboBox(central);
    hlay->addWidget(m_layerCombo, 1);
    vlay->addLayout(hlay);

    // Tree view
    m_treeView = new QTreeView(central);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    vlay->addWidget(m_treeView, 1);

    setWidget(central);

    // Property model / delegate
#ifdef HAVE_QPROPERTYMODEL
    m_model    = new QPropertyModel(this);
    m_delegate = new QPropertyItemDelegate(this);
#else
    m_model    = new QStandardItemModel(this);
#endif

    m_treeView->setModel(m_model);
    if (m_delegate)
        m_treeView->setItemDelegate(m_delegate);
    m_treeView->header()->setDefaultSectionSize(150);

    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AttributePanel::onLayerComboIndexChanged);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AttributePanel::showObject(QObject *object, const QString &title)
{
    setWindowTitle(title.isEmpty() ? tr("Attributes") : title);
    m_layerCombo->clear();
    m_lastResults.clear();

#ifdef HAVE_QPROPERTYMODEL
    auto *pm = qobject_cast<QPropertyModel*>(m_model);
    if (!object)
    {
        if (pm) pm->clear();
        return;
    }
    if (pm) pm->setData(QVariant::fromValue(object));
#else
    Q_UNUSED(object);
#endif
    m_treeView->expandAll();
}

void AttributePanel::showIdentifyResults(const QList<IdentifyResult> &results)
{
    m_lastResults = results;
    m_layerCombo->clear();

    for (const auto &r : results)
        m_layerCombo->addItem(r.layerName);

    if (!results.isEmpty())
        onLayerComboIndexChanged(0);
#ifdef HAVE_QPROPERTYMODEL
    else if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
        pm->clear();
#endif
}

void AttributePanel::showLayerProperties(OpenSWMMVisLayer *layer)
{
    showObject(layer, tr("Layer Properties — %1").arg(layer ? layer->name() : QString()));
}

void AttributePanel::clear()
{
#ifdef HAVE_QPROPERTYMODEL
    if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
        pm->clear();
#endif
    m_layerCombo->clear();
    m_lastResults.clear();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AttributePanel::onIdentifyResult(const QList<IdentifyResult> &results)
{
    showIdentifyResults(results);
}

void AttributePanel::onLayerComboIndexChanged(int index)
{
    if (index < 0 || index >= m_lastResults.size())
        return;

    const IdentifyResult &result = m_lastResults.at(index);

    if (result.features.isEmpty())
    {
#ifdef HAVE_QPROPERTYMODEL
        if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
            pm->clear();
#endif
        return;
    }

    // Show the first matching feature as a plain QVariantMap wrapped in a
    // QVariantHolderHelper.  Multiple features could be shown as sub-items
    // but a single map is clear and sufficient for the common case.
    //
    // QPropertyModel accepts a QVariant of any type; passing a QVariantMap
    // lets it display key/value pairs automatically.

#ifdef HAVE_QPROPERTYMODEL
    if (auto *pm = qobject_cast<QPropertyModel*>(m_model))
        pm->setData(QVariant(result.features.first()));
#endif
    m_treeView->expandAll();

    setWindowTitle(tr("Attributes — %1 (%2 feature%3)")
                   .arg(result.layerName)
                   .arg(result.features.size())
                   .arg(result.features.size() == 1 ? QString() : QStringLiteral("s")));
}
