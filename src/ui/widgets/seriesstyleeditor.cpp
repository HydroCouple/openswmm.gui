/*!
 * \file   seriesstyleeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/seriesstyleeditor.h"

#include "plot/seriesstyleobject.h"

#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::plot::SeriesStyle;
using openswmmvis::plot::SeriesStyleObject;

SeriesStyleEditor::SeriesStyleEditor(QWidget *parent)
    : QWidget(parent)
{
    m_obj = new SeriesStyleObject(this);
    buildUi();

    // Forward every aggregate style change as our own styleChanged() so
    // hosts can subscribe in a single place.
    connect(m_obj, &SeriesStyleObject::styleChanged,
            this, &SeriesStyleEditor::styleChanged);
    // Spec-level legendOverride is distinct from the style — forward it as
    // its own signal so the dialog can write it back to SeriesSpec.
    connect(m_obj, &SeriesStyleObject::legendOverrideChanged,
            this, &SeriesStyleEditor::legendOverrideChanged);
}

SeriesStyleEditor::~SeriesStyleEditor() = default;

void SeriesStyleEditor::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);

    m_model = new QPropertyModel(m_obj, this);
    m_tree->setModel(m_model);
    m_tree->setItemDelegate(new QPropertyItemDelegate(m_tree));
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->expandAll();

    root->addWidget(m_tree, 1);
}

void SeriesStyleEditor::setStyle(const SeriesStyle& style)
{
    if (!m_obj) return;
    m_obj->setStyle(style);
    if (m_model) m_model->refreshValues();
}

SeriesStyle SeriesStyleEditor::style() const
{
    return m_obj ? m_obj->style() : SeriesStyle{};
}

void SeriesStyleEditor::setLegendOverride(const QString& override)
{
    if (!m_obj) return;
    m_obj->setLegendOverride(override);
    if (m_model) m_model->refreshValues();
}

QString SeriesStyleEditor::legendOverride() const
{
    return m_obj ? m_obj->legendOverride() : QString();
}

} // namespace openswmmvis::ui
