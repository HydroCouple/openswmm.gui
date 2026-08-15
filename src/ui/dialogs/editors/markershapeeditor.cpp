/*!
 * \file   markershapeeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/editors/markershapeeditor.h"

#include "render/markershape.h"

#include <QBrush>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QMetaEnum>
#include <QPainter>
#include <QPen>
#include <QPixmap>

namespace openswmmvis::ui {

namespace {

// Render a single MarkerShape into a QIcon. The pixmap is small enough
// to sit in a combo-box row but big enough to read the silhouette; the
// shape itself is drawn into an inset square so antialiasing doesn't
// clip against the icon's edge.
QIcon makeShapeIcon(OpenSWMM::Render::MarkerShape shape, int pxSize = 18)
{
    QPixmap pix(pxSize, pxSize);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QBrush fill(QColor(80, 130, 200));
    QPen outline(QColor(40, 40, 40));
    outline.setWidthF(1.0);
    OpenSWMM::Render::drawMarkerShape(
        &p, shape,
        QPointF(pxSize * 0.5, pxSize * 0.5),
        pxSize - 4.0,
        fill, outline);
    return QIcon(pix);
}

} // namespace

MarkerShapeEditor::MarkerShapeEditor(QWidget *parent)
    : QBasePropertyItemEditor(parent)
{
    m_combo = new QComboBox(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_combo);
    setFocusProxy(m_combo);

    // Walk the Q_ENUM_NS so display names track the enum 1:1. If MOC
    // somehow misses the registration we fall back to a hand-coded list
    // so the editor still works.
    const QMetaObject &mo = OpenSWMM::Render::staticMetaObject;
    const int idx = mo.indexOfEnumerator("MarkerShape");
    if (idx >= 0) {
        const QMetaEnum me = mo.enumerator(idx);
        for (int i = 0; i < me.keyCount(); ++i) {
            const auto shape = static_cast<OpenSWMM::Render::MarkerShape>(me.value(i));
            m_combo->addItem(makeShapeIcon(shape),
                             QString::fromLatin1(me.key(i)),
                             QVariant::fromValue(shape));
        }
    } else {
        using OpenSWMM::Render::MarkerShape;
        const std::pair<MarkerShape, const char *> fallback[] = {
            {MarkerShape::Circle,              "Circle"},
            {MarkerShape::Square,              "Square"},
            {MarkerShape::Triangle,            "Triangle"},
            {MarkerShape::Diamond,             "Diamond"},
            {MarkerShape::Star,                "Star"},
            {MarkerShape::Cross,               "Cross"},
            {MarkerShape::Plus,                "Plus"},
            {MarkerShape::XCross,              "XCross"},
            {MarkerShape::Pentagon,            "Pentagon"},
            {MarkerShape::Hexagon,             "Hexagon"},
            {MarkerShape::Arrow,               "Arrow"},
            {MarkerShape::EquilateralTriangle, "EquilateralTriangle"},
            {MarkerShape::HalfCircle,          "HalfCircle"},
        };
        for (const auto &[shape, name] : fallback)
            m_combo->addItem(makeShapeIcon(shape),
                             QString::fromLatin1(name),
                             QVariant::fromValue(shape));
    }

    // Commit on every selection change so the canvas updates live.
    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit valueChanged(this); });
}

MarkerShapeEditor::~MarkerShapeEditor() = default;

void MarkerShapeEditor::setValue(const QVariant &value)
{
    if (!m_combo) return;
    OpenSWMM::Render::MarkerShape current{};
    bool resolved = false;
    if (value.canConvert<OpenSWMM::Render::MarkerShape>()) {
        current  = value.value<OpenSWMM::Render::MarkerShape>();
        resolved = true;
    } else {
        bool ok = false;
        const int asInt = value.toInt(&ok);
        if (ok) {
            current  = static_cast<OpenSWMM::Render::MarkerShape>(asInt);
            resolved = true;
        }
    }
    if (!resolved) return;
    for (int i = 0; i < m_combo->count(); ++i) {
        if (m_combo->itemData(i).value<OpenSWMM::Render::MarkerShape>() == current) {
            m_combo->setCurrentIndex(i);
            break;
        }
    }
}

QVariant MarkerShapeEditor::getValue() const
{
    if (!m_combo) return {};
    return m_combo->itemData(m_combo->currentIndex());
}

} // namespace openswmmvis::ui
