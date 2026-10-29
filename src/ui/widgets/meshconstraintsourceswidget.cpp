// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshconstraintsourceswidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSet>
#include <QPushButton>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool empty(const mesh::ConstraintPolicyOverride &p)
{
    return p.mode == -1 && !p.maxDeviation && !p.maxZDeviation && !p.maxTurnDegrees &&
           !p.maxAreaChangeFraction && !p.minimumClearance;
}
}
MeshConstraintSourcesWidget::MeshConstraintSourcesWidget(QWidget *parent) : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_source = new QComboBox(this);
    m_source->setObjectName("constraintSourcePicker");
    m_source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    form->addRow(tr("Source:"), m_source);
    m_mode = new QComboBox(this);
    m_mode->setObjectName("constraintSourceMode");
    m_mode->addItems({tr("Use default"), tr("Preserve authored vertices"), tr("Simplify within limits")});
    form->addRow(tr("Geometry policy:"), m_mode);
    const QStringList names{"XY", "Z", "Turn", "Area", "Clearance"};
    const QStringList labels{tr("Maximum XY deviation:"), tr("Maximum authored-Z deviation:"),
                             tr("Preserve turns above:"), tr("Maximum ring area change:"),
                             tr("Minimum clearance:")};
    for (int i = 0; i < names.size(); ++i) {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *check = new QCheckBox(tr("Override"), row);
        check->setObjectName("constraintSource" + names[i] + "Override");
        auto *value = new QDoubleSpinBox(row);
        value->setObjectName("constraintSource" + names[i] + "Value");
        value->setRange(0, i == 2 ? 180 : i == 3 ? 100 : 1e12);
        value->setDecimals(i == 2 ? 2 : i == 3 ? 4 : 6);
        value->setMinimumWidth(115);
        const auto scale = i == 2 ? Scale::Angle : i == 3 ? Scale::Fraction : Scale::Length;
        m_fields.append({check, value, scale});
        layout->addWidget(check);
        layout->addWidget(value, 1);
        form->addRow(labels[i], row);
        connect(check, &QCheckBox::toggled, this, [this] { storeCurrent(); loadCurrent(); });
        connect(value, &QDoubleSpinBox::valueChanged, this, [this] { storeCurrent(); });
    }
    m_fields[1].value->setToolTip(tr("Only authored 3D line heights are checked. Terrain accuracy is configured separately."));
    m_fields[4].value->setToolTip(tr("Shortcuts must keep this clearance from other constraints, except at shared endpoints."));
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setObjectName("constraintSourceStatus");
    form->addRow(m_status);
    m_reset = new QPushButton(tr("Use defaults for this source"), this);
    m_reset->setObjectName("constraintSourceReset");
    form->addRow(m_reset);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_policies.remove(m_source->currentData().toString());
        rebuildSources();
        emit policiesChanged();
    });
    auto *note = new QLabel(tr("Include layers on the Sources tab. Unchecked limits inherit the default above. Feature attributes override these source settings. Shared paths use the stricter owner protections. Unmatched shared samples are retained conservatively."), this);
    note->setWordWrap(true);
    form->addRow(note);
    connect(m_source, &QComboBox::currentIndexChanged, this, [this] { loadCurrent(); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { storeCurrent(); loadCurrent(); });
    setUnits(1, "m");
}
void MeshConstraintSourcesWidget::setSources(const QVector<Source> &sources)
{
    m_sources = sources;
    rebuildSources();
}
void MeshConstraintSourcesWidget::rebuildSources()
{
    const QString selected = m_source->currentData().toString();
    {
        QScopedValueRollback<bool> loading(m_loading, true);
        m_source->clear();
        QSet<QString> available;
        for (const auto &source : m_sources) {
            if (available.contains(source.id)) continue;
            available.insert(source.id);
            m_source->addItem(source.label, source.id);
            m_source->setItemData(m_source->count() - 1, source.label, Qt::ToolTipRole);
        }
        auto saved = m_policies.keys();
        std::sort(saved.begin(), saved.end());
        for (const auto &id : saved)
            if (!available.contains(id)) m_source->addItem(tr("Unavailable source: %1").arg(id), id);
        const int index = m_source->findData(selected);
        m_source->setCurrentIndex(index >= 0 ? index : m_source->count() ? 0 : -1);
    }
    loadCurrent();
}
void MeshConstraintSourcesWidget::setDefaults(const mesh::ConstraintSimplificationPolicy &defaults)
{
    m_defaults = defaults;
    loadCurrent();
}
void MeshConstraintSourcesWidget::setUnits(double metresPerUnit, const QString &label)
{
    if (!(metresPerUnit > 0) || !std::isfinite(metresPerUnit)) return;
    m_toSI = metresPerUnit; m_unitLabel = label;
    loadCurrent();
}
mesh::ConstraintPolicyOverride MeshConstraintSourcesWidget::policy(const QString &id) const
{
    return m_policies.value(id);
}
void MeshConstraintSourcesWidget::loadCurrent()
{
    if (m_loading) return;
    QScopedValueRollback<bool> loading(m_loading, true);
    const auto id = m_source->currentData().toString();
    const auto value = m_policies.value(id);
    auto defaults = m_defaults;
    bool authoredZ = true;
    for (const auto &source : m_sources)
        if (source.id == id) {
            authoredZ = source.authoredZ;
            if (source.preserveByDefault) defaults.mode = mesh::ConstraintSimplificationPolicy::Mode::Preserve;
            break;
        }
    m_mode->setItemText(0, defaults.mode == mesh::ConstraintSimplificationPolicy::Mode::Preserve
                             ? tr("Use default — preserve") : tr("Use default — simplify"));
    m_mode->setCurrentIndex(value.mode >= -1 && value.mode <= 1 ? value.mode + 1 : 0);
    m_mode->setEnabled(!id.isEmpty());
    m_reset->setEnabled(m_policies.contains(id));
    const std::optional<double> values[]{value.maxDeviation, value.maxZDeviation, value.maxTurnDegrees,
                                        value.maxAreaChangeFraction, value.minimumClearance};
    const double inherited[]{defaults.maxDeviation, defaults.maxZDeviation, defaults.maxTurnDegrees,
                              defaults.maxAreaChangeFraction, defaults.minimumClearance};
    const bool preserve = value.resolve(defaults).mode == mesh::ConstraintSimplificationPolicy::Mode::Preserve;
    for (int i = 0; i < m_fields.size(); ++i) {
        auto &field = m_fields[i];
        const double factor = field.scale == Scale::Length ? 1 / m_toSI : field.scale == Scale::Fraction ? 100 : 1;
        const double physical = values[i].value_or(inherited[i]);
        field.override->setChecked(values[i].has_value());
        field.override->setEnabled(!id.isEmpty() && !preserve && (i != 1 || authoredZ || values[i].has_value()));
        field.value->setSuffix(field.scale == Scale::Length ? " " + m_unitLabel
                                 : field.scale == Scale::Fraction ? " %" : QString::fromUtf8("°"));
        field.value->setValue(std::isfinite(physical) ? physical * factor : 0);
        field.displayed = field.value->value();
        field.value->setEnabled(field.override->isEnabled() && values[i].has_value());
    }
    const auto error = value.validate();
    m_status->setText(error.isEmpty() ? (preserve ? tr("This source retains its authored vertices.")
                                                  : tr("This source may remove redundant vertices within these limits."))
                                     : tr("Invalid saved source policy: %1. Correct its settings before meshing.").arg(error));
}
void MeshConstraintSourcesWidget::storeCurrent()
{
    if (m_loading || m_source->currentIndex() < 0) return;
    const QString id = m_source->currentData().toString();
    const auto previous = m_policies.value(id);
    const std::optional<double> prior[]{previous.maxDeviation, previous.maxZDeviation,
        previous.maxTurnDegrees, previous.maxAreaChangeFraction, previous.minimumClearance};
    mesh::ConstraintPolicyOverride value;
    value.mode = m_mode->currentIndex() - 1;
    std::optional<double> *destinations[]{&value.maxDeviation, &value.maxZDeviation, &value.maxTurnDegrees,
                                        &value.maxAreaChangeFraction, &value.minimumClearance};
    for (int i = 0; i < m_fields.size(); ++i)
        if (m_fields[i].override->isChecked()) {
            const auto scale = m_fields[i].scale;
            const double factor = scale == Scale::Length ? m_toSI : scale == Scale::Fraction ? .01 : 1;
            *destinations[i] = prior[i] && m_fields[i].value->value() == m_fields[i].displayed
                ? prior[i] : std::optional<double>(m_fields[i].value->value() * factor);
        }
    if (empty(value)) m_policies.remove(id);
    else m_policies.insert(id, value);
    m_reset->setEnabled(m_policies.contains(id));
    for (auto &field : m_fields) field.displayed = field.value->value();
    m_status->setText(value.validate().isEmpty() ? tr("Source settings updated.")
        : tr("Invalid source policy: %1").arg(value.validate()));
    emit policiesChanged();
}
QJsonArray MeshConstraintSourcesWidget::toJson() const
{
    QJsonArray rows;
    auto ids = m_policies.keys(); std::sort(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const auto &value = m_policies[id];
        QJsonObject row{{"id", id}, {"mode", value.mode}};
        const std::pair<const char *, std::optional<double>> fields[]{
            {"maximumXYM", value.maxDeviation}, {"maximumZM", value.maxZDeviation},
            {"maximumTurnDegrees", value.maxTurnDegrees}, {"maximumAreaFraction", value.maxAreaChangeFraction},
            {"minimumClearanceM", value.minimumClearance}};
        for (const auto &field : fields)
            if (field.second) row[field.first] = std::isfinite(*field.second)
                ? QJsonValue(*field.second) : QJsonValue("Invalid saved value");
        rows.append(row);
    }
    return rows;
}
void MeshConstraintSourcesWidget::fromJson(const QJsonArray &json)
{
    m_policies.clear();
    for (const auto &entry : json) {
        const auto row = entry.toObject();
        const auto id = row.value("id").toString();
        if (id.isEmpty()) continue;
        mesh::ConstraintPolicyOverride value;
        value.mode = row.value("mode").isUndefined() ? -1 : row.value("mode").toInt(2);
        const std::pair<const char *, std::optional<double> *> fields[]{
            {"maximumXYM", &value.maxDeviation}, {"maximumZM", &value.maxZDeviation},
            {"maximumTurnDegrees", &value.maxTurnDegrees}, {"maximumAreaFraction", &value.maxAreaChangeFraction},
            {"minimumClearanceM", &value.minimumClearance}};
        for (const auto &field : fields)
            if (row.contains(field.first) && !row.value(field.first).isNull())
                *field.second = row.value(field.first).isDouble() ? row.value(field.first).toDouble()
                    : std::numeric_limits<double>::quiet_NaN();
        if (!empty(value)) m_policies.insert(id, value);
    }
    rebuildSources();
}
