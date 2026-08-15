/*!
 * \file   uiscrollhelpers.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared helper for the "content scrolls, dialog shrinks" pattern.
 *
 *         Properties / styling panels build their controls into a plain
 *         QVBoxLayout, so when the dock or dialog is dragged below the content's
 *         natural size Qt squeezes the children (zero-width combos, clipped
 *         labels). Wrapping the content in a frameless, resizable QScrollArea
 *         lets the content hold its minimum size and scroll instead — matching
 *         layerstylingdock.cpp and swmm2dresultsstylepanel.cpp, which already
 *         do this. One helper, reused, rather than re-deriving the lambda.
 */

#ifndef UI_SCROLL_HELPERS_H
#define UI_SCROLL_HELPERS_H

#include <QFrame>
#include <QScrollArea>
#include <QWidget>

namespace OpenSWMM::Ui
{

/*! Minimum widths for controls that otherwise collapse in a narrow panel.
 *  Shared so spin boxes / combo boxes stay readable once the content scrolls.
 *  Values match swmm2dresultsstylepanel.cpp, the existing reference. */
inline constexpr int kSpinMinWidthPx  = 110;
inline constexpr int kComboMinWidthPx = 140;

/*!
 * \brief Wrap \p content in a frameless, resizable QScrollArea.
 * \param content  The fully-built content widget to host (takes ownership).
 * \param parent   Optional parent for the scroll area.
 * \return The scroll area; insert it where \p content would have been added.
 *
 * The scroll area resizes \p content to the viewport width and shows a
 * scrollbar when the viewport is smaller than the content's minimum — so the
 * dialog/dock may shrink freely while the content stays at a readable size.
 */
inline QScrollArea *wrapInScrollArea(QWidget *content, QWidget *parent = nullptr)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

} // namespace OpenSWMM::Ui

#endif // UI_SCROLL_HELPERS_H
