/**
 * @file linkendpointcombobox.cpp
 * @brief Searchable node picker with sorted completion for large models.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ui/properties/linkendpointcombobox.h"

#include <QCompleter>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStringListModel>
#include <algorithm>

LinkEndpointComboBox::LinkEndpointComboBox(QWidget *parent) : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
    setMaxVisibleItems(8);
    setModel(new QStringListModel(this));
    auto *completion = new QCompleter(model(), this);
    completion->setCaseSensitivity(Qt::CaseInsensitive);
    completion->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    completion->setFilterMode(Qt::MatchStartsWith);
    completion->setCompletionMode(QCompleter::PopupCompletion);
    completion->setMaxVisibleItems(8);
    setCompleter(completion);
    lineEdit()->setPlaceholderText(tr("Type a node ID…"));
    setToolTip(tr("Type a node ID to find matching nodes, then select a suggestion."));
    connect(this, qOverload<int>(&QComboBox::activated), this, [this] { finishEditing(); });
    connect(lineEdit(), &QLineEdit::editingFinished, this, &LinkEndpointComboBox::finishEditing);
}

void LinkEndpointComboBox::setValue(const LinkEndpointRef &ref)
{
    const QSignalBlocker blocker(this);
    m_ref = ref;
    QStringList nodes;
    const int link =
        ref.engine ? swmm_link_index(ref.engine, ref.linkName.toUtf8().constData()) : -1;
    const int count = ref.engine ? swmm_node_count(ref.engine) : 0;
    if (link >= 0 && count > 0) {
        nodes.reserve(count);
        for (int node = 0; node < count; ++node) {
            if (linkendpoint::allowed(ref.engine, link, ref.upstream, node))
                nodes.append(QString::fromUtf8(swmm_node_id(ref.engine, node)));
        }
    }
    // One model reset per editing session; the completer uses binary prefix
    // lookup instead of enumerating the engine's nodes on each keystroke.
    std::sort(nodes.begin(), nodes.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    static_cast<QStringListModel *>(model())->setStringList(nodes);
    setCurrentIndex(-1);
    setEditText(ref.nodeName);
}

LinkEndpointRef LinkEndpointComboBox::value() const
{
    auto ref = m_ref;
    const QByteArray text = currentText().trimmed().toUtf8();
    const int link =
        ref.engine ? swmm_link_index(ref.engine, ref.linkName.toUtf8().constData()) : -1;
    const int node = ref.engine ? swmm_node_index(ref.engine, text.constData()) : -1;
    if (linkendpoint::allowed(ref.engine, link, ref.upstream, node))
        ref.nodeName = QString::fromUtf8(swmm_node_id(ref.engine, node));
    return ref; // An incomplete or invalid ID never becomes a commit candidate.
}

void LinkEndpointComboBox::finishEditing()
{
    const auto next = value();
    const bool changed = next.nodeName != m_ref.nodeName;
    m_ref = next;
    setEditText(m_ref.nodeName);
    if (changed)
        emit valueChanged();
}
