#ifndef OPENSWMM_GROUPED_ATTRIBUTE_HEADER_H
#define OPENSWMM_GROUPED_ATTRIBUTE_HEADER_H

#include "ui/properties/attributegroups.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>

// A separate band leaves the ordinary column labels (and CSV headers) intact.
class GroupedAttributeHeader : public QHeaderView
{
    int bandHeight() const { return fontMetrics().height() + 10; }
    bool grouped() const {
        if (!model()) return false;
        for (int i = 0; i < count(); ++i)
            if (!model()->headerData(i, orientation(), openswmmvis::AttributeGroupRole).toString().isEmpty()) return true;
        return false;
    }
    QString group(int logical) const {
        QString value = model()->headerData(logical, orientation(), openswmmvis::AttributeGroupRole).toString();
        return value.isEmpty() ? tr("Model Attributes") : value.section(" / ", 0, 0);
    }
protected:
    QSize sectionSizeFromContents(int logical) const override {
        QSize size = QHeaderView::sectionSizeFromContents(logical);
        if (grouped()) size.rheight() += bandHeight();
        return size;
    }
    void paintSection(QPainter *painter, const QRect &rect, int logical) const override {
        if (!grouped()) { QHeaderView::paintSection(painter, rect, logical); return; }
        QRect label = rect;
        label.setTop(label.top() + bandHeight());
        QHeaderView::paintSection(painter, label, logical);
    }
    void paintEvent(QPaintEvent *event) override {
        QHeaderView::paintEvent(event);
        if (!grouped()) return;
        QPainter painter(viewport());
        int start = 0, end = 0;
        bool active = false;
        QString title;
        auto paintBand = [&] {
            if (!active) return;
            QStyleOptionHeader option;
            initStyleOption(&option);
            option.rect = QRect(start, 0, end - start, bandHeight());
            option.text = fontMetrics().elidedText(title, Qt::ElideRight, qMax(0, end - start - 12));
            option.textAlignment = Qt::AlignCenter;
            option.sortIndicator = QStyleOptionHeader::None;
            style()->drawControl(QStyle::CE_Header, &option, &painter, this);
        };
        for (int visual = 0; visual < count(); ++visual) {
            int logical = logicalIndex(visual);
            if (isSectionHidden(logical)) continue;
            int x = sectionViewportPosition(logical);
            const QString next = group(logical);
            if (active && next != title) { paintBand(); active = false; }
            if (!active) { start = x; title = next; active = true; }
            end = x + sectionSize(logical);
        }
        paintBand();
    }
public:
    explicit GroupedAttributeHeader(QWidget *parent = nullptr) : QHeaderView(Qt::Horizontal, parent) {}
};
#endif
