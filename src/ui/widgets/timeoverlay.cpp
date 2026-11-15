/**
 * @file      timeoverlay.cpp
 * @brief     Box on the map that shows the animation date and time.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/widgets/timeoverlay.h"

#include "map/mapcanvas.h"
#include "render/mapboxchrome.h"
#include "render/timeoverlaystyle.h"
#include "ui/properties/propertyrows.h"

#include <qpropertymodel.h>

#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui
{

using OpenSWMM::Render::TimeOverlayStyle;

TimeOverlay::TimeOverlay(MapCanvas *canvas)
    : QWidget(canvas), m_canvas(canvas), m_style(new TimeOverlayStyle(this))
{
    setObjectName(QStringLiteral("mapTimeOverlay"));
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setCursor(Qt::OpenHandCursor);
    setAccessibleName(tr("Map date and time"));

    connect(m_style, &TimeOverlayStyle::changed, this, [this]() {
        relayout();
        update();
    });
    connect(m_style, &TimeOverlayStyle::shownChanged, this, [this](bool on) {
        setVisible(on);
        if (on)
            raise();
    });
    if (m_canvas)
        m_canvas->installEventFilter(this);

    hide();
    relayout();
}

TimeOverlay::~TimeOverlay() = default;

TimeOverlay *TimeOverlay::forCanvas(MapCanvas *canvas)
{
    if (!canvas)
        return nullptr;
    if (auto *box = canvas->findChild<TimeOverlay *>(QString(), Qt::FindDirectChildrenOnly))
        return box;
    return new TimeOverlay(canvas);
}

void TimeOverlay::setDateTime(const QDateTime &dt)
{
    if (dt == m_dateTime)
        return;
    m_dateTime = dt;
    relayout();
    update();
}

QString TimeOverlay::text() const
{
    const QString t = m_style->textFor(m_dateTime);
    return t.isEmpty() ? tr("No animation time") : t;
}

void TimeOverlay::relayout()
{
    const QString t = text();
    setAccessibleDescription(t);

    // Size the box for the widest digit in every digit place, so it does not change width
    // from one frame to the next while the time plays.
    const QFontMetrics fm(m_style->font());
    QChar widest = QLatin1Char('0');
    for (char c = '0'; c <= '9'; ++c)
        if (fm.horizontalAdvance(QLatin1Char(c)) > fm.horizontalAdvance(widest))
            widest = QLatin1Char(c);
    QString sized = t;
    for (QChar &c : sized)
        if (c.isDigit())
            c = widest;
    const int pad = std::max(0, m_style->padding());
    const int frame =
        m_style->showFrame() ? int(std::ceil(std::max<qreal>(0.0, m_style->frameWidth()))) : 0;
    const int w =
        std::max(fm.horizontalAdvance(sized), fm.boundingRect(sized).width()) + 2 * (pad + frame);
    const int h = fm.height() + 2 * (pad + frame);
    resize(w, h);

    if (!m_canvas)
        return;
    // Always on the map, so it can be dragged back.
    move(OpenSWMM::Render::anchoredMapBoxPosition(size(), m_canvas->size(), *m_style));
}

void TimeOverlay::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setOpacity(std::clamp<qreal>(m_style->opacity(), 0.0, 1.0));
    // Same box as the legend: rounded rectangle, filled per the background mode, then framed.
    OpenSWMM::Render::paintMapBoxChrome(p, QRectF(rect()), *m_style);

    p.setFont(m_style->font());
    p.setPen(m_style->textColor());
    p.drawText(rect(), Qt::AlignCenter, text());
}

void TimeOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Selecting the box shows its style in the Properties panel.
    if (m_canvas)
        m_canvas->selectOverlayStyle(m_style, tr("Date and Time"));
    m_dragging = true;
    m_dragStartOffset = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void TimeOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    QPoint to = mapToParent(event->pos()) - m_dragStartOffset;
    if (m_canvas) {
        to.setX(std::clamp(to.x(), 0, std::max(0, m_canvas->width() - width())));
        to.setY(std::clamp(to.y(), 0, std::max(0, m_canvas->height() - height())));
    }
    // The style places the box, so the position is saved with the project.
    m_style->setFreePosition(to);
    m_style->setAnchor(TimeOverlayStyle::Anchor::Free);
    event->accept();
}

void TimeOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimeOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    connect(menu.addAction(tr("Properties…")), &QAction::triggered, this,
            &TimeOverlay::openPropertiesDialog);
    menu.addSeparator();
    connect(menu.addAction(tr("Hide date and time")), &QAction::triggered, m_style,
            [this]() { m_style->setShown(false); });
    menu.exec(event->globalPos());
    event->accept();
}

bool TimeOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void TimeOverlay::openPropertiesDialog()
{
    auto *dlg = new QDialog(this);
    dlg->setObjectName(QStringLiteral("TimeOverlayPropertiesDialog"));
    dlg->setWindowTitle(tr("Date and Time Properties"));
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->resize(420, 440);

    // Edits apply live; Esc puts back what the box looked like when the dialog opened.
    const QJsonObject snapshot = m_style->toJson();
    auto *layout = new QVBoxLayout(dlg);
    auto *tree = new QTreeView(dlg);
    tree->setAlternatingRowColors(true);
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setRootIsDecorated(false);
    auto *model = new QPropertyModel(m_style, dlg);
    tree->setModel(model);
    tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree->header()->setStretchLastSection(false);
    tree->expandAll();
    // The font row shows its parts (family, size, bold, …) when selected.
    openswmmvis::showFontPartsOnSelection(tree);
    openswmmvis::collapseFontRows(tree);
    tree->resizeColumnToContents(0);
    // Edits made elsewhere (the Properties panel, dragging the box) show here too.
    connect(
        m_style, &TimeOverlayStyle::changed, tree,
        [model]() { openswmmvis::refreshPropertyRows(model); }, Qt::QueuedConnection);
    tree->setToolTip(tr("The format uses Qt date/time codes, e.g. MM/dd/yyyy hh:mm:ss; text in "
                        "single quotes is shown as written."));
    layout->addWidget(tree, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Close, dlg);
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    buttons->button(QDialogButtonBox::Reset)
        ->setToolTip(tr("Restore all fields to their built-in defaults"));
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::accept); // Close keeps edits.
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, dlg, [this, model]() {
        m_style->resetToDefaults();
        model->refreshValues();
    });
    connect(dlg, &QDialog::rejected, this, [this, snapshot]() { m_style->fromJson(snapshot); });
    layout->addWidget(buttons);
    dlg->show();
}

} // namespace openswmmvis::ui
