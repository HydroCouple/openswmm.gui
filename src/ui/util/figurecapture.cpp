/*!
 * \file   figurecapture.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Implementation of the manual figure capture engine (see header).
 */
#include "ui/util/figurecapture.h"

#include "ui/actionregistry.h"
#include "ui/theme/thememanager.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QWidget>

Q_LOGGING_CATEGORY(lcFigCap, "openswmm.figcap")

namespace openswmmvis::ui {

namespace {

/*! Actions that raise a NATIVE file/colour picker. Triggering one headless
 *  blocks forever with nobody to dismiss it, so the engine refuses them and
 *  the manifest must mark those figures lane:"human". */
const QSet<QString> &nativePickerActions_()
{
    static const QSet<QString> ids = {
        QStringLiteral("file.open"),        QStringLiteral("file.openProject"),
        QStringLiteral("file.save"),        QStringLiteral("file.saveAs"),
        QStringLiteral("file.exportMap"),   QStringLiteral("file.import"),
        QStringLiteral("file.print"),
    };
    return ids;
}

FigureLane laneFromString_(const QString &text)
{
    if (text.compare(QLatin1String("live"), Qt::CaseInsensitive) == 0)
        return FigureLane::Live;
    if (text.compare(QLatin1String("human"), Qt::CaseInsensitive) == 0)
        return FigureLane::Human;
    return FigureLane::Offscreen;
}

/*! "760x980" → QSize(760,980); anything else → an invalid QSize. */
QSize sizeFromString_(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return {};
    bool okW = false, okH = false;
    const int w = parts.at(0).trimmed().toInt(&okW);
    const int h = parts.at(1).trimmed().toInt(&okH);
    return (okW && okH && w > 0 && h > 0) ? QSize(w, h) : QSize();
}

/*! A descendant by objectName, else by class name. Class names are the only
    handle most inner panels have — they set no objectName — and they are the
    more stable of the two anyway. */
QWidget *descendant_(QWidget *root, const QString &which)
{
    if (QWidget *named = root->findChild<QWidget *>(which))
        return named;
    const auto all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        if (w->isVisibleTo(root)
            && which == QLatin1String(w->metaObject()->className()))
            return w;
    }
    return nullptr;
}

/*! Depth-first search for the row whose display text is \a which. */
QModelIndex findRow_(QAbstractItemModel *model, const QModelIndex &parent,
                     const QString &which)
{
    const int rows = model->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex idx = model->index(r, 0, parent);
        if (!idx.isValid())
            continue;
        if (idx.data(Qt::DisplayRole).toString().compare(
                which, Qt::CaseInsensitive) == 0)
            return idx;
        const QModelIndex hit = findRow_(model, idx, which);
        if (hit.isValid())
            return hit;
    }
    return {};
}

/*! Menu mnemonics ("&Model") must not defeat a plain-text page match. */
QString stripMnemonic_(const QString &text)
{
    QString out = text;
    out.remove(QLatin1Char('&'));
    return out.trimmed();
}

/*! \brief Cheap "did this grab carry any information" score.
 *
 *  The offscreen QPA returns an empty frame for scene-graph content, so a map
 *  or mesh figure silently comes back as a flat rectangle. Counting distinct
 *  colours over a coarse grid separates that from a real screenshot without
 *  scanning every pixel. A populated dialog runs to hundreds of colours; an
 *  empty frame is 1-2. */
bool looksBlank_(const QImage &img)
{
    if (img.isNull())
        return true;

    QSet<QRgb> seen;
    const int stepX = qMax(1, img.width()  / 64);
    const int stepY = qMax(1, img.height() / 64);
    for (int y = 0; y < img.height(); y += stepY) {
        for (int x = 0; x < img.width(); x += stepX) {
            seen.insert(img.pixel(x, y));
            if (seen.size() > 12)
                return false;   // clearly has content; stop early
        }
    }
    return true;
}

} // namespace

FigureCapture *FigureCapture::createIfRequested(QMainWindow *host,
                                                QObject     *parent)
{
    const QString manifest = qEnvironmentVariable("SWMMVIS_CAPTURE_MANIFEST");
    if (manifest.isEmpty() || !host)
        return nullptr;

    if (!QFileInfo::exists(manifest)) {
        qCWarning(lcFigCap) << "manifest not found:" << manifest;
        return nullptr;
    }
    return new FigureCapture(host, manifest, parent ? parent : host);
}

FigureCapture::FigureCapture(QMainWindow *host,
                             QString      manifestPath,
                             QObject     *parent)
    : QObject(parent)
    , mHost(host)
    , mManifestPath(std::move(manifestPath))
{
    mWatchdog = new QTimer(this);
    mWatchdog->setSingleShot(true);
    connect(mWatchdog, &QTimer::timeout, this, [this]() {
        // Whatever the row opened is still up: tear it down and record the
        // failure rather than wedging the whole run on one figure.
        dismissDialogs();
        FigureResult r;
        r.name      = mCurrent.name;
        r.status    = QStringLiteral("failed");
        r.detail    = QStringLiteral("watchdog: state not reached in %1 ms")
                          .arg(mWatchdogMs);
        r.elapsedMs = int(mSpecTimer.elapsed());
        finishSpec(r);
    });
}

FigureCapture::~FigureCapture() = default;

void FigureCapture::start()
{
    if (mStarted)
        return;
    mStarted = true;

    QString error;
    if (!loadManifest(&error)) {
        qCWarning(lcFigCap) << "manifest error:" << error;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    applyDefaults();

    qCInfo(lcFigCap) << "capture run:" << mQueue.size() << "figures →" << mOutDir;

    // Give SWMMVIS_OPEN_ON_STARTUP time to finish loading its model before the
    // first row runs; rows that need no model simply wait a beat longer.
    QTimer::singleShot(mStartupMs, this, &FigureCapture::processNext);
}

bool FigureCapture::loadManifest(QString *error)
{
    QFile f(mManifestPath);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(mManifestPath);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("%1 at offset %2")
                     .arg(parseError.errorString())
                     .arg(parseError.offset);
        return false;
    }
    const QJsonObject root = doc.object();
    mDefaults = root.value(QStringLiteral("defaults")).toObject();

    // Output dir: manifest value, else beside the manifest. Never a temp dir —
    // CLAUDE.md §4.1 wants every generated file reviewable in the tree.
    mOutDir = mDefaults.value(QStringLiteral("outDir")).toString();
    if (mOutDir.isEmpty())
        mOutDir = QFileInfo(mManifestPath).absolutePath()
                  + QStringLiteral("/manual_figures");
    QDir().mkpath(mOutDir);

    // An optional filter lets one manifest serve many launches: the driver
    // passes SWMMVIS_CAPTURE_ONLY=a.png,b.png to run just those rows.
    const QStringList only =
        qEnvironmentVariable("SWMMVIS_CAPTURE_ONLY")
            .split(QLatin1Char(','), Qt::SkipEmptyParts);

    const QJsonArray figures = root.value(QStringLiteral("figures")).toArray();
    for (const QJsonValue &v : figures) {
        const QJsonObject o = v.toObject();
        FigureSpec spec;
        spec.name = o.value(QStringLiteral("name")).toString();
        if (spec.name.isEmpty())
            continue;
        if (!only.isEmpty() && !only.contains(spec.name))
            continue;

        spec.action      = o.value(QStringLiteral("action")).toString();
        spec.widget      = o.value(QStringLiteral("widget")).toString();
        spec.wholeWindow = o.value(QStringLiteral("window")).toBool(false);
        spec.page        = o.value(QStringLiteral("page")).toString();
        spec.tab         = o.value(QStringLiteral("tab")).toString();
        spec.type        = o.value(QStringLiteral("type")).toString();
        spec.select      = o.value(QStringLiteral("select")).toString();
        spec.hostSelect  = o.value(QStringLiteral("hostSelect")).toString();
        spec.grab        = o.value(QStringLiteral("grab")).toString();
        spec.size     = sizeFromString_(o.value(QStringLiteral("size")).toString());
        spec.hostSize = sizeFromString_(o.value(QStringLiteral("hostSize")).toString());
        spec.lane     = laneFromString_(o.value(QStringLiteral("lane")).toString());
        spec.settleMs = o.value(QStringLiteral("settleMs")).toInt(0);
        mQueue.append(spec);
    }

    if (mQueue.isEmpty()) {
        *error = QStringLiteral("no figures selected from %1").arg(mManifestPath);
        return false;
    }
    return true;
}

void FigureCapture::applyDefaults()
{
    mDefaultSettleMs = mDefaults.value(QStringLiteral("settleMs")).toInt(600);
    mStartupMs       = mDefaults.value(QStringLiteral("startupMs")).toInt(2500);
    // A big model (Bellinge is 38 MB) is still loading when the manifest's
    // startup wait expires, and every row then finds an empty editor. The
    // launcher raises the wait for those runs rather than slowing every run.
    if (const int override =
            qEnvironmentVariable("SWMMVIS_CAPTURE_STARTUP_MS").toInt();
        override > 0)
        mStartupMs = override;
    mWatchdogMs      = mDefaults.value(QStringLiteral("watchdogMs")).toInt(20000);
    mMaxWidth        = mDefaults.value(QStringLiteral("maxWidth")).toInt(1400);
    mRenderScale     = mDefaults.value(QStringLiteral("renderScale")).toDouble(2.0);

    // The manual is captured on the light theme (docs/manual/README.md). Force
    // it explicitly: on the live lane the developer's OS appearance leaks in.
    const QString theme = mDefaults.value(QStringLiteral("theme")).toString();
    if (theme.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        ThemeManager::instance()->setMode(ThemeManager::Mode::Dark);
    else if (!theme.isEmpty())
        ThemeManager::instance()->setMode(ThemeManager::Mode::Light);

    // Offscreen the main window comes up at a useless default (canvas 192x114),
    // so whole-window figures need a deterministic size on every platform.
    const QSize win = sizeFromString_(
        mDefaults.value(QStringLiteral("window")).toString());
    if (win.isValid() && mHost)
        mHost->resize(win);
}

void FigureCapture::processNext()
{
    if (mQueue.isEmpty()) {
        finishRun();
        return;
    }

    mCurrent     = mQueue.takeFirst();
    mCurrentDone = false;
    mSpecTimer.start();

    if (mCurrent.lane == FigureLane::Human) {
        FigureResult r;
        r.name   = mCurrent.name;
        r.status = QStringLiteral("skipped");
        r.detail = QStringLiteral("lane:human — capture by hand");
        finishSpec(r);
        return;
    }

    mWatchdog->start(mWatchdogMs);
    captureSpec(mCurrent);
}

void FigureCapture::captureSpec(const FigureSpec &spec)
{
    const int settle = spec.settleMs > 0 ? spec.settleMs : mDefaultSettleMs;

    // --- whole main window -------------------------------------------------
    if (spec.wholeWindow) {
        QTimer::singleShot(settle, this, [this, spec]() {
            grabInto(mHost, spec);
        });
        return;
    }

    // --- a named widget already in the window (docks, panels, ribbon) ------
    if (!spec.widget.isEmpty()) {
        QTimer::singleShot(settle, this, [this, spec]() {
            QWidget *w = descendant_(mHost, spec.widget);
            if (!w) {
                FigureResult r;
                r.name      = spec.name;
                r.status    = QStringLiteral("failed");
                r.detail    = QStringLiteral("no widget named or classed '%1'")
                                  .arg(spec.widget);
                r.elapsedMs = int(mSpecTimer.elapsed());
                finishSpec(r);
                return;
            }
            grabInto(w, spec);
        });
        return;
    }

    // --- trigger an action, then grab the dialog it raises -----------------
    if (!spec.action.isEmpty()) {
        // Every styling command is scoped to the layer selected in the Layers
        // panel, and with nothing selected they raise a "select a layer first"
        // message box instead of the dialog. So the host selection has to be
        // made BEFORE the trigger — spec.select runs inside the dialog, which
        // is far too late.
        if (!spec.hostSelect.isEmpty() && !selectItem(mHost, spec.hostSelect)) {
            FigureResult r;
            r.name   = spec.name;
            r.status = QStringLiteral("failed");
            r.detail = QStringLiteral("no item matching '%1' in the main window")
                           .arg(spec.hostSelect);
            finishSpec(r);
            return;
        }

        if (nativePickerActions_().contains(spec.action)) {
            FigureResult r;
            r.name   = spec.name;
            r.status = QStringLiteral("failed");
            r.detail = QStringLiteral(
                           "action '%1' raises a native picker — mark lane:human")
                           .arg(spec.action);
            finishSpec(r);
            return;
        }

        // Catalog id first; then the QAction's objectName. Not every command
        // is in the ActionCatalog (Initial Quality, for one), and those are
        // still perfectly capturable.
        QAction *act = ActionRegistry::instance()->action(spec.action);
        if (!act)
            act = mHost->findChild<QAction *>(spec.action);
        if (!act) {
            FigureResult r;
            r.name   = spec.name;
            r.status = QStringLiteral("failed");
            r.detail = QStringLiteral("no action with catalog id or objectName '%1'")
                           .arg(spec.action);
            finishSpec(r);
            return;
        }

        // The grab is armed BEFORE the trigger: a modal dialog blocks inside
        // exec(), and this timer keeps firing in that nested event loop, so it
        // is what gets us back in to grab and close it.
        QTimer::singleShot(settle, this, [this, spec]() {
            QWidget *dlg = activeDialog();
            if (!dlg) {
                FigureResult r;
                r.name      = spec.name;
                r.status    = QStringLiteral("failed");
                r.detail    = QStringLiteral("action '%1' raised no dialog")
                                  .arg(spec.action);
                r.elapsedMs = int(mSpecTimer.elapsed());
                finishSpec(r);
                return;
            }
            grabInto(dlg, spec);
        });

        act->trigger();
        return;
    }

    FigureResult r;
    r.name   = spec.name;
    r.status = QStringLiteral("failed");
    r.detail = QStringLiteral("row names no action, widget or window target");
    finishSpec(r);
}

void FigureCapture::grabInto(QWidget *target, const FigureSpec &spec)
{
    FigureResult r;
    r.name = spec.name;

    if (!target) {
        r.status = QStringLiteral("failed");
        r.detail = QStringLiteral("target vanished before the grab");
        finishSpec(r);
        return;
    }

    // A ribbon row lays itself out to the window width: too narrow and a group
    // caption is clipped ("Climate" came out as "limat"). Widen the window
    // first, then let the target take its natural size from it.
    if (spec.hostSize.isValid() && mHost) {
        mHost->resize(spec.hostSize);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (spec.size.isValid())
        target->resize(spec.size);

    // Fall back to the host: a tab that drives the target often lives outside
    // it — the ribbon's tab strip is a separate toolbar from the ribbon pages
    // it shows, and a dock's tab bar belongs to the main window.
    if (!spec.page.isEmpty()
        && !selectPage(target, spec.page) && !selectPage(mHost, spec.page)) {
        r.status    = QStringLiteral("failed");
        r.detail    = QStringLiteral("no page matching '%1'").arg(spec.page);
        r.elapsedMs = int(mSpecTimer.elapsed());
        dismissOpenedBy(spec);
        finishSpec(r);
        return;
    }

    // A sidebar page often carries its own tab strip: let the page switch lay
    // out first, then pick the tab inside whatever it revealed.
    if (!spec.tab.isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!selectPage(target, spec.tab) && !selectPage(mHost, spec.tab)) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no tab matching '%1'").arg(spec.tab);
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
    }

    if (!spec.select.isEmpty() && !selectItem(target, spec.select)) {
        r.status    = QStringLiteral("failed");
        r.detail    = QStringLiteral("no item matching '%1' to select").arg(spec.select);
        r.elapsedMs = int(mSpecTimer.elapsed());
        dismissOpenedBy(spec);
        finishSpec(r);
        return;
    }

    // Several figures show a filtered view — a command palette narrowed to a
    // few matches, an object browser filtered by name. Drive the first visible
    // line edit rather than leaving the figure contradicting its caption.
    if (!spec.type.isEmpty()) {
        QLineEdit *edit = nullptr;
        const auto edits = target->findChildren<QLineEdit *>();
        for (QLineEdit *e : edits) {
            if (e->isVisibleTo(target) && e->isEnabled() && !e->isReadOnly()) {
                edit = e;
                break;
            }
        }
        if (!edit) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no editable line edit to type '%1' into")
                              .arg(spec.type);
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
        edit->setFocus(Qt::OtherFocusReason);
        edit->setText(spec.type);          // emits textChanged: filters apply
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Several figures are one panel inside a dialog, not the dialog — the
    // classification editor, the kind tree. Everything above still drives the
    // dialog; only the rectangle that gets rendered narrows.
    if (!spec.grab.isEmpty()) {
        QWidget *inner = descendant_(target, spec.grab);
        if (!inner) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no descendant named or classed '%1'")
                              .arg(spec.grab);
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
        target = inner;
    }

    // Let the resize / page switch lay out before the pixels are read.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Render at mRenderScale rather than calling grab(): grab() honours the
    // widget's device pixel ratio, which is 1 under the offscreen QPA, and the
    // manual wants 2x captures. Painting into a DPR-scaled pixmap re-runs the
    // paint pipeline at device resolution, so text and vector chrome are
    // genuinely sharper — not an upscale.
    const QSize logical = target->size();
    QPixmap pm(logical * mRenderScale);
    pm.setDevicePixelRatio(mRenderScale);
    pm.fill(Qt::transparent);
    target->render(&pm, QPoint(), QRegion(),
                   QWidget::DrawWindowBackground | QWidget::DrawChildren);

    QImage img = pm.toImage();
    img.setDevicePixelRatio(1.0);   // bake the scale in; store real pixels

    if (mMaxWidth > 0 && img.width() > mMaxWidth)
        img = img.scaledToWidth(mMaxWidth, Qt::SmoothTransformation);

    r.blank  = looksBlank_(img);
    r.width  = img.width();
    r.height = img.height();

    const QString path = mOutDir + QLatin1Char('/') + spec.name;
    if (!img.save(path, "PNG")) {
        r.status = QStringLiteral("failed");
        r.detail = QStringLiteral("could not write %1").arg(path);
    } else {
        r.bytes  = QFileInfo(path).size();
        r.status = QStringLiteral("ok");
        if (r.blank) {
            r.detail = QStringLiteral(
                "blank grab — scene-graph content needs lane:live on a display");
        }
    }
    r.elapsedMs = int(mSpecTimer.elapsed());

    // Close whatever this row opened BEFORE advancing, so the next row starts
    // from the same clean window state.
    dismissOpenedBy(spec);
    finishSpec(r);
}

QWidget *FigureCapture::activeDialog() const
{
    if (QWidget *modal = QApplication::activeModalWidget())
        return modal;

    // Non-modal: the most recently shown visible dialog that is not the host.
    QWidget *found = nullptr;
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (w == mHost || !w->isVisible())
            continue;
        if (qobject_cast<QDialog *>(w))
            found = w;
    }
    return found;
}

bool FigureCapture::selectPage(QWidget *target, const QString &page) const
{
    const QString wanted = stripMnemonic_(page);

    // 1. A tab widget anywhere inside the target.
    const auto tabs = target->findChildren<QTabWidget *>();
    for (QTabWidget *tw : tabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (stripMnemonic_(tw->tabText(i)).compare(
                    wanted, Qt::CaseInsensitive) == 0) {
                tw->setCurrentIndex(i);
                return true;
            }
        }
    }

    // 2. A bare tab bar (tabs driving something other than a QTabWidget).
    const auto bars = target->findChildren<QTabBar *>();
    for (QTabBar *bar : bars) {
        for (int i = 0; i < bar->count(); ++i) {
            if (stripMnemonic_(bar->tabText(i)).compare(
                    wanted, Qt::CaseInsensitive) == 0) {
                bar->setCurrentIndex(i);
                return true;
            }
        }
    }

    // 3. The sidebar idiom: a list of category names driving a stacked widget.
    //    Selecting the row is enough — the dialog wires the stack itself.
    const auto lists = target->findChildren<QListWidget *>();
    for (QListWidget *lw : lists) {
        for (int i = 0; i < lw->count(); ++i) {
            if (stripMnemonic_(lw->item(i)->text()).compare(
                    wanted, Qt::CaseInsensitive) == 0) {
                lw->setCurrentRow(i);
                return true;
            }
        }
    }

    // 4. A tree of categories (Preferences).
    const auto trees = target->findChildren<QTreeWidget *>();
    for (QTreeWidget *tree : trees) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (stripMnemonic_(item->text(0)).compare(
                    wanted, Qt::CaseInsensitive) == 0) {
                tree->setCurrentItem(item);
                return true;
            }
        }
    }

    return false;
}

bool FigureCapture::selectItem(QWidget *target, const QString &which) const
{
    // A list-and-detail editor opens with its list populated but nothing
    // current, so the detail pane is blank and the figure contradicts its
    // caption ("the series list; point grid and chart" over an empty grid).
    // One pass over every item view covers QListWidget, QListView, QTreeWidget
    // and QTableView alike.
    const bool wantFirst =
        which.compare(QLatin1String("first"), Qt::CaseInsensitive) == 0;

    const auto views = target->findChildren<QAbstractItemView *>();
    for (QAbstractItemView *view : views) {
        if (!view->isVisibleTo(target) || !view->model())
            continue;
        QAbstractItemModel *model = view->model();
        const int rows = model->rowCount(view->rootIndex());
        if (rows <= 0)
            continue;

        if (wantFirst) {
            const QModelIndex idx = model->index(0, 0, view->rootIndex());
            if (!idx.isValid())
                continue;
            view->setCurrentIndex(idx);
            return true;
        }
        // Depth-first: the Layers panel files every layer under a category
        // node, so the row a figure wants is never at the top level there.
        const QModelIndex idx = findRow_(model, view->rootIndex(), which);
        if (idx.isValid()) {
            if (auto *tree = qobject_cast<QTreeView *>(view))
                for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
                    tree->expand(p);
            view->setCurrentIndex(idx);
            return true;
        }
    }
    return false;
}

void FigureCapture::dismissOpenedBy(const FigureSpec &spec)
{
    // Only an action row opens anything. A window or widget row must leave the
    // UI as it found it: closing a dialog the row did not open once rejected
    // the startup licence agreement, which the app reads as "declined" and
    // quits on — the capture took itself down after two figures.
    if (!spec.action.isEmpty())
        dismissDialogs();
}

void FigureCapture::dismissDialogs()
{
    // reject() rather than accept(): a figure must never write to the model.
    for (int guard = 0; guard < 8; ++guard) {
        QWidget *w = activeDialog();
        if (!w)
            break;
        qCInfo(lcFigCap) << "    dismiss" << w->metaObject()->className()
                         << w->objectName();
        if (auto *dlg = qobject_cast<QDialog *>(w))
            dlg->reject();
        else
            w->close();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

void FigureCapture::finishSpec(const FigureResult &result)
{
    if (mCurrentDone)
        return;             // a watchdog and a real completion can race
    mCurrentDone = true;
    mWatchdog->stop();

    mResults.append(result);
    qCInfo(lcFigCap).nospace()
        << "  " << result.status << "  " << result.name
        << "  " << result.width << "x" << result.height
        << (result.blank ? "  BLANK" : "")
        << (result.detail.isEmpty() ? QString()
                                    : QStringLiteral("  (%1)").arg(result.detail));

    QTimer::singleShot(0, this, &FigureCapture::processNext);
}

void FigureCapture::finishRun()
{
    int ok = 0, failed = 0, skipped = 0, blank = 0;
    QJsonArray rows;
    for (const FigureResult &r : std::as_const(mResults)) {
        if (r.status == QLatin1String("ok"))           ++ok;
        else if (r.status == QLatin1String("skipped")) ++skipped;
        else                                           ++failed;
        if (r.blank) ++blank;

        QJsonObject o;
        o[QStringLiteral("name")]      = r.name;
        o[QStringLiteral("status")]    = r.status;
        o[QStringLiteral("width")]     = r.width;
        o[QStringLiteral("height")]    = r.height;
        o[QStringLiteral("bytes")]     = double(r.bytes);
        o[QStringLiteral("blank")]     = r.blank;
        o[QStringLiteral("elapsedMs")] = r.elapsedMs;
        if (!r.detail.isEmpty())
            o[QStringLiteral("detail")] = r.detail;
        rows.append(o);
    }

    QJsonObject summary;
    summary[QStringLiteral("platform")] = QGuiApplication::platformName();
    summary[QStringLiteral("ok")]       = ok;
    summary[QStringLiteral("failed")]   = failed;
    summary[QStringLiteral("skipped")]  = skipped;
    summary[QStringLiteral("blank")]    = blank;

    QJsonObject root;
    root[QStringLiteral("manifest")] = mManifestPath;
    root[QStringLiteral("summary")]  = summary;
    root[QStringLiteral("figures")]  = rows;

    const QString reportPath = mOutDir + QStringLiteral("/run.json");
    QFile out(reportPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    qCInfo(lcFigCap) << "capture done — ok" << ok << "failed" << failed
                     << "skipped" << skipped << "blank" << blank
                     << "→" << reportPath;

    // Exit code carries the failure count so a driver script can gate on it.
    // Without this the app would sit at its event loop and have to be killed.
    QTimer::singleShot(0, qApp, [failed]() {
        qApp->exit(failed > 0 ? 1 : 0);
    });
}

} // namespace openswmmvis::ui
