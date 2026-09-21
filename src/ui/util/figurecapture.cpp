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
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
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
        if (!w->isVisibleTo(root))
            continue;
        // className() is namespace-qualified for the app's own widgets
        // ("openswmmvis::ui::ClassificationEditor"). Match the trailing
        // segment too so a manifest can name the class the way a reader does.
        const QString cls = QString::fromLatin1(w->metaObject()->className());
        if (cls == which || cls.endsWith(QLatin1String("::") + which))
            return w;
    }
    return nullptr;
}

/*! "Meshes (1)" -> "Meshes". The Layers panel suffixes every category and
    kind row with its count, which changes with the model; a manifest should
    name the row, not the arithmetic. */
QString stripCount_(const QString &text)
{
    if (!text.endsWith(QLatin1Char(')')))
        return text;
    const int open = text.lastIndexOf(QLatin1Char('('));
    if (open <= 0)
        return text;
    const QStringView inner =
        QStringView(text).sliced(open + 1, text.size() - open - 2);
    if (inner.isEmpty())
        return text;
    for (const QChar c : inner)
        if (!c.isDigit())
            return text;
    return text.first(open).trimmed();
}

/*! The app's own widget classes under \a root, for a failure message — Qt's
    own (QWidget, QLabel, …) are noise when a figure is hunting for a panel. */
QString offeredClasses_(QWidget *root)
{
    QStringList out;
    const auto all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        if (!w->isVisibleTo(root))
            continue;
        const QString cls = QString::fromLatin1(w->metaObject()->className());
        if (cls.startsWith(QLatin1Char('Q')) || out.contains(cls))
            continue;
        out << cls;
    }
    return out.join(QStringLiteral(", "));
}

/*! Every row text under \a root, "Parent/Child" style, for a failure message.
    A row name a figure cannot guess costs a whole capture round trip, so the
    failure says what was actually on offer. */
QString offeredRows_(QWidget *root, const QModelIndex &parent = {},
                     QAbstractItemModel *model = nullptr)
{
    QStringList out;
    if (!model) {
        const auto views = root->findChildren<QAbstractItemView *>();
        for (QAbstractItemView *view : views) {
            if (!view->isVisibleTo(root) || !view->model())
                continue;
            const QString rows =
                offeredRows_(root, view->rootIndex(), view->model());
            if (!rows.isEmpty())
                out << rows;
        }
        return out.join(QStringLiteral("; "));
    }

    const int rows = model->rowCount(parent);
    for (int r = 0; r < rows && out.size() < 24; ++r) {
        const QModelIndex idx = model->index(r, 0, parent);
        if (!idx.isValid())
            continue;
        const QString text = idx.data(Qt::DisplayRole).toString();
        const QString kids = offeredRows_(root, idx, model);
        out << (kids.isEmpty() ? text
                               : QStringLiteral("%1/[%2]").arg(text, kids));
    }
    return out.join(QStringLiteral(", "));
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
        const QString text = idx.data(Qt::DisplayRole).toString();
        if (text.compare(which, Qt::CaseInsensitive) == 0
            || stripCount_(text).compare(which, Qt::CaseInsensitive) == 0)
            return idx;
        const QModelIndex hit = findRow_(model, idx, which);
        if (hit.isValid())
            return hit;
    }
    return {};
}

QString stripMnemonic_(const QString &text);

/*! Tab, list, tree and combo labels under \a root, for a failure message. */
QString offeredPages_(QWidget *root)
{
    QStringList out;
    const auto bars = root->findChildren<QTabBar *>();
    for (QTabBar *bar : bars)
        for (int i = 0; i < bar->count(); ++i)
            out << QStringLiteral("tab:") + stripMnemonic_(bar->tabText(i));
    const auto lists = root->findChildren<QListWidget *>();
    for (QListWidget *lw : lists)
        for (int i = 0; i < lw->count(); ++i)
            out << QStringLiteral("list:") + stripMnemonic_(lw->item(i)->text());
    const auto combos = root->findChildren<QComboBox *>();
    for (QComboBox *combo : combos)
        for (int i = 0; i < combo->count(); ++i)
            out << QStringLiteral("%1combo:%2")
                       .arg(combo->isVisibleTo(root) ? QString() : QStringLiteral("hidden-"),
                            stripMnemonic_(combo->itemText(i)));
    return out.join(QStringLiteral(", "));
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
        spec.typeInto    = o.value(QStringLiteral("typeInto")).toString();
        spec.select      = o.value(QStringLiteral("select")).toString();
        spec.hostSelect  = o.value(QStringLiteral("hostSelect")).toString();
        spec.hostSelectIn = o.value(QStringLiteral("hostSelectIn")).toString();
        const QJsonValue clickVal = o.value(QStringLiteral("click"));
        if (clickVal.isArray()) {
            const QJsonArray arr = clickVal.toArray();
            for (const QJsonValue &v : arr)
                spec.clicks << v.toString();
        } else if (!clickVal.toString().isEmpty()) {
            spec.clicks << clickVal.toString();
        }
        spec.column      = o.value(QStringLiteral("column")).toString();
        spec.editCell    = o.value(QStringLiteral("editCell")).toString();
        spec.grab        = o.value(QStringLiteral("grab")).toString();
        spec.maxWidth    = o.value(QStringLiteral("maxWidth")).toInt(0);
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


bool FigureCapture::applyHostSelect(const FigureSpec &spec)
{
    if (spec.hostSelect.isEmpty())
        return true;

    // Scope the search when asked: "J1" matches a row in the hidden Attribute
    // Table as readily as in the Object Browser, and the first view to answer
    // wins otherwise.
    QWidget *scope = mHost;
    if (!spec.hostSelectIn.isEmpty())
        scope = descendant_(mHost, spec.hostSelectIn);
    if (scope && selectItem(scope, spec.hostSelect))
        return true;

    FigureResult r;
    r.name   = spec.name;
    r.status = QStringLiteral("failed");
    r.detail = scope
        ? QStringLiteral("no item matching '%1' in %2 — rows offered: %3")
              .arg(spec.hostSelect,
                   spec.hostSelectIn.isEmpty() ? QStringLiteral("the main window")
                                               : spec.hostSelectIn,
                   offeredRows_(scope))
        : QStringLiteral("no panel named or classed '%1' to select in")
              .arg(spec.hostSelectIn);
    r.elapsedMs = int(mSpecTimer.elapsed());
    finishSpec(r);
    return false;
}

void FigureCapture::captureSpec(const FigureSpec &spec)
{
    const int settle = spec.settleMs > 0 ? spec.settleMs : mDefaultSettleMs;

    // An ACTION row must select before the action fires: the styling commands
    // are scoped to the layer selected in the Layers panel. A WIDGET row is
    // the other way round — its "page" switches the Attribute Table's
    // category, which resets the model, so selecting a row first selects in
    // the wrong category. That one is applied in grabInto(), after the page.
    if (spec.widget.isEmpty() && !spec.wholeWindow && !applyHostSelect(spec))
        return;

    // --- whole main window -------------------------------------------------
    if (spec.wholeWindow) {
        QTimer::singleShot(settle, this, [this, spec]() {
            grabInto(mHost, spec);
        });
        return;
    }

    // --- a named widget already in the window (docks, panels, ribbon) ------
    if (!spec.widget.isEmpty()) {
        // An action alongside a widget is a PRECONDITION, not the figure: a
        // dock that starts hidden has to be toggled on before it can be
        // grabbed. The widget stays the target either way.
        if (!spec.action.isEmpty()) {
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
            if (!act->isCheckable() || !act->isChecked())
                act->trigger();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        QTimer::singleShot(settle, this, [this, spec]() {
            QWidget *w = descendant_(mHost, spec.widget);
            if (!w) {
                FigureResult r;
                r.name      = spec.name;
                r.status    = QStringLiteral("failed");
                r.detail    = QStringLiteral("no widget named or classed '%1'"
                                             " — classes present: %2")
                                  .arg(spec.widget, offeredClasses_(mHost));
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
        r.detail    = QStringLiteral("no page matching '%1' — pages offered: %2")
                          .arg(spec.page, offeredPages_(target));
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
            r.detail    = QStringLiteral("no tab matching '%1' — pages offered: %2")
                              .arg(spec.tab, offeredPages_(target));
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
    }

    // The other half of the rule above: a widget or whole-window row selects
    // AFTER its page has switched.
    if ((!spec.widget.isEmpty() || spec.wholeWindow) && !applyHostSelect(spec))
        return;

    // A compound property (External Inflows, Cross Section, LID Usage) is
    // edited through a delegate-built "Edit…" button that only exists while
    // the cell is in edit mode. Open that editor so a later click can press
    // it — the alternative entry point is a right-click menu, which is
    // native on macOS and therefore human-lane.
    if (!spec.editCell.isEmpty()) {
        // A category switch RESETS the table model and rebuilds its column
        // schema, and that lands on a later turn of the event loop. Looking
        // for the column immediately found a model with no rows at all.
        {
            QElapsedTimer rebuilt;
            rebuilt.start();
            const int wait = spec.settleMs > 0 ? spec.settleMs : mDefaultSettleMs;
            while (rebuilt.elapsed() < wait)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        }
        bool opened = false;
        QStringList headers;
        const auto views = target->findChildren<QAbstractItemView *>();
        for (QAbstractItemView *view : views) {
            QAbstractItemModel *model = view->model();
            if (!view->isVisibleTo(target) || !model || model->rowCount() <= 0)
                continue;
            for (int col = 0; col < model->columnCount(); ++col) {
                const QString head =
                    model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
                if (head.isEmpty())
                    continue;
                headers << head;
                if (head.compare(spec.editCell, Qt::CaseInsensitive) != 0)
                    continue;
                // Honour the selected row: hostSelect picks the object the
                // figure is about, and opening row 0 regardless produced an
                // inflows page reading "0 on this node (model total: 4)".
                const int row = view->currentIndex().isValid()
                                    ? view->currentIndex().row() : 0;
                const QModelIndex idx = model->index(row, col);
                if (!idx.isValid())
                    continue;
                view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                view->setCurrentIndex(idx);
                view->edit(idx);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                opened = true;
                break;
            }
            if (opened)
                break;
        }
        if (!opened) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no column headed '%1' to edit in %2 — headers: %3")
                              .arg(spec.editCell,
                                   QString::fromLatin1(target->metaObject()->className()),
                                   headers.join(QStringLiteral(", ")));
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
    }

    // Some figures live one button-press further in: the label expression
    // builder opens from the Labels tab, the ramp editor from a ramp picker.
    // Arm the grab BEFORE the click for the same reason the action path does
    // — a modal dialog blocks inside exec(), and this timer is what gets us
    // back in. Everything above has already run against the outer dialog, so
    // the continuation carries only what still applies to the new one.
    if (!spec.clicks.isEmpty()) {
        // Every press but the last is a precondition, not the figure: the
        // expression builder's button is disabled until "Show labels" is on.
        // Only the last press is expected to raise a dialog.
        QAbstractButton *btn = nullptr;
        for (int i = 0; i < spec.clicks.size(); ++i) {
            const QString wanted = spec.clicks.at(i);
            QStringList offered;
            btn = nullptr;
            QAbstractButton *loose = nullptr;
            const auto buttons = target->findChildren<QAbstractButton *>();
            for (QAbstractButton *b : buttons) {
                if (!b->isVisibleTo(target) || !b->isEnabled()
                    || b->text().isEmpty())
                    continue;
                const QString label = stripMnemonic_(b->text());
                offered << label;
                if (label.compare(wanted, Qt::CaseInsensitive) == 0) {
                    btn = b;
                    break;
                }
                // A delegate button embeds the cell's own summary in its
                // label ("(none) - Edit..."), which a manifest cannot know
                // per object. Fall back to a contains match.
                if (!loose && label.contains(wanted, Qt::CaseInsensitive))
                    loose = b;
            }
            if (!btn)
                btn = loose;
            if (!btn) {
                r.status    = QStringLiteral("failed");
                r.detail    = QStringLiteral("no button labelled '%1' — buttons: %2")
                                  .arg(wanted, offered.join(QStringLiteral(", ")));
                r.elapsedMs = int(mSpecTimer.elapsed());
                dismissOpenedBy(spec);
                finishSpec(r);
                return;
            }
            if (i + 1 == spec.clicks.size())
                break;                      // the last one is armed below
            btn->click();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        FigureSpec rest = spec;
        rest.clicks.clear();
        rest.page.clear();
        rest.tab.clear();
        rest.editCell.clear();   // already served: it is what raised the dialog
        rest.column.clear();
        rest.hostSize = QSize();
        QWidget *before = activeDialog();
        QTimer::singleShot(spec.settleMs > 0 ? spec.settleMs : mDefaultSettleMs,
                           this, [this, rest, before]() {
            QWidget *dlg = activeDialog();
            if (!dlg || dlg == before) {
                FigureResult f;
                f.name      = rest.name;
                f.status    = QStringLiteral("failed");
                f.detail    = QStringLiteral("the click raised no dialog");
                f.elapsedMs = int(mSpecTimer.elapsed());
                dismissDialogs();
                finishSpec(f);
                return;
            }
            grabInto(dlg, rest);
        });
        btn->click();
        return;
    }

    // Several figures show a filtered view — a command palette narrowed to a
    // few matches, an object browser filtered by name. Drive the first visible
    // text field rather than leaving the figure contradicting its caption.
    if (!spec.type.isEmpty()) {
        // Filtered views debounce: the Object Browser waits on a QTimer before
        // it narrows and expands. A single processEvents() therefore grabs the
        // UNFILTERED tree under a caption promising a filtered one. Pump the
        // loop for the settle instead, so the timer gets its chance to fire.
        const int typeSettle = spec.settleMs > 0 ? spec.settleMs : mDefaultSettleMs;
        auto settleAfterTyping = [typeSettle]() {
            QElapsedTimer typed;
            typed.start();
            while (typed.elapsed() < typeSettle)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        };

        // "the first editable field" is a guess, and it was the WRONG one in
        // the report viewer: the Search box comes before the section filter,
        // so the figure searched the report text while its caption promised a
        // filtered section list. typeInto names the field by objectName or by
        // part of its placeholder.
        bool typedInto = false;
        QStringList fields;
        const auto edits = target->findChildren<QLineEdit *>();
        for (QLineEdit *e : edits) {
            if (!e->isVisibleTo(target) || !e->isEnabled() || e->isReadOnly())
                continue;
            fields << (e->placeholderText().isEmpty() ? e->objectName()
                                                      : e->placeholderText());
            if (!spec.typeInto.isEmpty()
                && !e->objectName().contains(spec.typeInto, Qt::CaseInsensitive)
                && !e->placeholderText().contains(spec.typeInto, Qt::CaseInsensitive))
                continue;
            e->setFocus(Qt::OtherFocusReason);
            e->setText(spec.type);        // emits textChanged: filters apply
            settleAfterTyping();
            typedInto = true;
            break;
        }

        if (!typedInto && !spec.typeInto.isEmpty()) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no field matching '%1' — fields: %2")
                              .arg(spec.typeInto, fields.join(QStringLiteral(", ")));
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }

        // Not every text field is one line. The label expression builder's
        // template is a QPlainTextEdit, and with no fallback the figure came
        // back with an empty template over an "(empty — nothing would be
        // drawn)" preview, under a caption promising both.
        if (!typedInto) {
            const auto blocks = target->findChildren<QPlainTextEdit *>();
            for (QPlainTextEdit *b : blocks) {
                if (!b->isVisibleTo(target) || !b->isEnabled() || b->isReadOnly())
                    continue;
                b->setFocus(Qt::OtherFocusReason);
                b->setPlainText(spec.type);
                settleAfterTyping();
                typedInto = true;
                break;
            }
        }

        if (!typedInto) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no editable text field to type '%1' into")
                              .arg(spec.type);
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
    }

    if (!spec.select.isEmpty() && !selectItem(target, spec.select)) {
        r.status    = QStringLiteral("failed");
        r.detail    = QStringLiteral("no item matching '%1' to select"
                                    " — rows offered: %2")
                          .arg(spec.select, offeredRows_(target));
        r.elapsedMs = int(mSpecTimer.elapsed());
        dismissOpenedBy(spec);
        finishSpec(r);
        return;
    }

    // A results table puts its simulated columns to the RIGHT of the model
    // attributes, past the viewport, so a figure of "the dynamics block"
    // otherwise shows only Name / Type / From / To. Scroll to the named
    // column instead of guessing a pixel offset.
    if (!spec.column.isEmpty()) {
        bool scrolled = false;
        QStringList headers;
        const auto views = target->findChildren<QAbstractItemView *>();
        for (QAbstractItemView *view : views) {
            QAbstractItemModel *model = view->model();
            if (!view->isVisibleTo(target) || !model || model->rowCount() <= 0)
                continue;
            for (int c = 0; c < model->columnCount(); ++c) {
                const QString head =
                    model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
                if (head.isEmpty())
                    continue;
                headers << head;
                if (head.compare(spec.column, Qt::CaseInsensitive) != 0)
                    continue;
                const QModelIndex idx = model->index(0, c);
                if (!idx.isValid())
                    continue;
                view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                scrolled = true;
                break;
            }
            if (scrolled)
                break;
        }
        if (!scrolled) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no column headed '%1' — headers: %2")
                              .arg(spec.column, headers.join(QStringLiteral(", ")));
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
    }

    // Several figures are one panel inside a dialog, not the dialog — the
    // classification editor, the kind tree. Everything above still drives the
    // dialog; only the rectangle that gets rendered narrows.
    if (!spec.grab.isEmpty()) {
        QWidget *inner = descendant_(target, spec.grab);
        if (!inner) {
            r.status    = QStringLiteral("failed");
            r.detail    = QStringLiteral("no descendant named or classed '%1'"
                                         " — classes present: %2")
                              .arg(spec.grab, offeredClasses_(target));
            r.elapsedMs = int(mSpecTimer.elapsed());
            dismissOpenedBy(spec);
            finishSpec(r);
            return;
        }
        target = inner;
    }

    // Every step above can relayout the target, and a dock's layout claws
    // back the size set at the top — the Properties panel came out 278 px
    // wide instead of 420. Re-assert it last, once the content is final.
    if (spec.size.isValid())
        target->resize(spec.size);

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

    // A per-row clamp, because the audit's size cap is per FILE: a dense
    // figure (the CRS dialog's WKT block) can breach 400 KB at the width every
    // other figure is comfortable at.
    const int cap = spec.maxWidth > 0 ? spec.maxWidth : mMaxWidth;
    if (cap > 0 && img.width() > cap)
        img = img.scaledToWidth(cap, Qt::SmoothTransformation);

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

    // 5. A combo that drives the page, LAST so it never pre-empts a real tab
    //    strip. The Attribute Table's category is a combo, not a tab bar, and
    //    it reacts to currentIndexChanged — so setting the index is enough.
    const auto combos = target->findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        if (!combo->isVisibleTo(target))
            continue;
        for (int i = 0; i < combo->count(); ++i) {
            // These carry counts too ("Conduits (11)"), same as the Layers
            // panel's rows — a manifest names the category, not the tally.
            const QString label = stripMnemonic_(combo->itemText(i));
            if (label.compare(wanted, Qt::CaseInsensitive) == 0
                || stripCount_(label).compare(wanted, Qt::CaseInsensitive) == 0) {
                combo->setCurrentIndex(i);
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

        // setCurrentIndex alone emits currentChanged, NOT selectionChanged,
        // and the panels that matter listen to the latter: the Object Browser
        // pushes into the SelectionManager from its view's selectionChanged,
        // and the Properties panel reads the manager. Selecting the row too
        // is also simply what a user's click does.
        auto pick = [view](const QModelIndex &idx) {
            view->setCurrentIndex(idx);
            if (QItemSelectionModel *sel = view->selectionModel())
                sel->select(idx, QItemSelectionModel::ClearAndSelect
                                     | QItemSelectionModel::Rows);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };

        if (wantFirst) {
            const QModelIndex idx = model->index(0, 0, view->rootIndex());
            if (!idx.isValid())
                continue;
            pick(idx);
            return true;
        }
        // Depth-first: the Layers panel files every layer under a category
        // node, so the row a figure wants is never at the top level there.
        // The whole string is tried first, THEN read as a path, because a row
        // name may legitimately contain a slash — every projected CRS is
        // spelled "NAD83(2011) / UTM zone 17N", and splitting it looked for a
        // "UTM zone 17N" nested under a "NAD83(2011)".
        QModelIndex idx = findRow_(model, view->rootIndex(), which);
        if (!idx.isValid() && which.contains(QLatin1Char('/'))) {
            // "Meshes/2d_complete_example.inp", for the models whose mesh
            // layer and SWMM layer carry the same name.
            idx = view->rootIndex();
            for (const QString &segment : which.split(QLatin1Char('/'))) {
                idx = findRow_(model, idx, segment.trimmed());
                if (!idx.isValid())
                    break;
            }
        }
        if (idx.isValid() && idx != view->rootIndex()) {
            if (auto *tree = qobject_cast<QTreeView *>(view))
                for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
                    tree->expand(p);
            pick(idx);
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
