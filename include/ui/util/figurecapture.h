/*!
 * \file   figurecapture.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Manifest-driven screenshot capture for the user manual's figures.
 *
 * The manual (docs/manual/) declares every screenshot as \figtodo{file,caption}
 * and turns into \fig{file,caption} once the PNG exists in
 * docs/manual/images/. This class produces those PNGs without a human at the
 * keyboard: SWMMVIS_CAPTURE_MANIFEST=<abs .json> names a list of figures, each
 * describing the UI state to reach and the widget to grab.
 *
 * Generalises the three one-off SWMMVIS_SNAPSHOT_* dev hooks in swmmvis.cpp
 * (which stay, being documented in appendices/a04_performance.md) into one
 * table so a whole chapter is captured per launch — 326 one-shot launches would
 * be ~45 min of pure startup, grouped runs are ~2 min.
 *
 * Two run modes, one manifest. QT_QPA_PLATFORM=offscreen handles widget-only
 * figures (dialogs, docks, panels) with no display; anything containing
 * scene-graph content (2D mesh, 2D results) reads back BLANK offscreen and must
 * run on the live cocoa platform. Rows are not classified by hand: every grab is
 * scored and a blank one is reported so it can be re-shot on the live lane.
 *
 * Deliberately public-API only (ActionRegistry, ThemeManager, QWidget) so it
 * needs no access to SWMMVis internals. Models are supplied by the launcher
 * through SWMMVIS_OPEN_ON_STARTUP, one launch per model.
 */
#ifndef OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H
#define OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

class QMainWindow;
class QTimer;
class QWidget;

namespace openswmmvis::ui {

/*! \brief Which platform a figure can be captured on. */
enum class FigureLane {
    Offscreen,  //!< widget-only; no display needed (the default)
    Live,       //!< needs real scene-graph pixels — cocoa on a real display
    Human       //!< native menus / file pickers / Finder — engine skips it
};

/*! \brief One row of the manifest: a figure and how to reach it. */
struct FigureSpec {
    QString    name;                //!< "18_routing_hydraulics.png"
    QString    action;              //!< ActionCatalog id to trigger (opens a dialog)
    QString    widget;              //!< objectName to grab, resolved under the host
    bool       wholeWindow = false; //!< grab the main window itself
    QString    page;                //!< tab / list-row text to select in the target
    QSize      size;                //!< resize the target before grabbing
    FigureLane lane        = FigureLane::Offscreen;
    int        settleMs    = 0;     //!< extra settle before the grab (0 = default)
};

/*! \brief Result of attempting one row; serialised into run.json. */
struct FigureResult {
    QString name;
    QString status;             //!< "ok" | "skipped" | "failed"
    QString detail;             //!< failure reason, or the skip rationale
    int     width      = 0;
    int     height     = 0;
    qint64  bytes      = 0;
    bool    blank      = false; //!< grab carried (almost) no information
    int     elapsedMs  = 0;
};

/*!
 * \brief Drives a manifest of figures to PNG, then quits the application.
 *
 * Lifetime: created by createIfRequested() from the main window's constructor
 * and parented to it; a no-op (nullptr) when the env var is unset, so normal
 * runs pay nothing.
 */
class FigureCapture : public QObject
{
    Q_OBJECT

public:
    /*! \brief Build a capture run when SWMMVIS_CAPTURE_MANIFEST names a
     *         readable manifest, else nullptr.
     *
     *  \param host   the main window; widget lookups resolve under it.
     *  \param parent owner (normally \a host). */
    static FigureCapture *createIfRequested(QMainWindow *host, QObject *parent);

    ~FigureCapture() override;

    /*! \brief Begin the queue. Safe to call once; later calls are ignored.
     *
     *  Waits out \c defaults.startupMs first so a model handed in through
     *  SWMMVIS_OPEN_ON_STARTUP has finished loading. */
    void start();

private:
    FigureCapture(QMainWindow *host, QString manifestPath, QObject *parent);

    bool loadManifest(QString *error);
    void applyDefaults();
    void processNext();
    void finishSpec(const FigureResult &result);
    void finishRun();

    /*! \brief Reach the state \a spec describes and grab it. */
    void captureSpec(const FigureSpec &spec);
    void grabInto(QWidget *target, const FigureSpec &spec);

    /*! \brief Top-most visible dialog that is not the host, or nullptr. */
    QWidget *activeDialog() const;
    /*! \brief Select \a page inside \a target (tab bar, list + stack, tree). */
    bool selectPage(QWidget *target, const QString &page) const;
    /*! \brief Close whatever the row opened, without touching the model. */
    void dismissDialogs();

    QMainWindow       *mHost = nullptr;
    QString            mManifestPath;
    QString            mOutDir;
    QJsonObject        mDefaults;
    QList<FigureSpec>  mQueue;
    QList<FigureResult> mResults;

    FigureSpec     mCurrent;
    bool           mCurrentDone  = false;  //!< guards double-advance
    bool           mStarted      = false;
    QElapsedTimer  mSpecTimer;
    QTimer        *mWatchdog     = nullptr;

    int  mDefaultSettleMs = 600;
    int  mStartupMs       = 2500;
    int  mWatchdogMs      = 20000;
    int  mMaxWidth        = 1400;
    qreal mRenderScale    = 2.0;  //!< offscreen DPR is 1; render at 2x for the manual
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H
