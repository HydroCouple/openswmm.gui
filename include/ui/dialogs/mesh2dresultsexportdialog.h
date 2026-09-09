/*!
 * \file   mesh2dresultsexportdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Collects the options for "Export 2D Results…" on the Analysis ribbon:
 * output format and path, which variables, which time steps (with an every-Nth
 * stride for a long run), whether to include the run maxima, and — for a raster
 * — the pixel size, the interpolation method and the dry mask.
 *
 * The dialog is purely a collector (CLAUDE.md §5.1): it holds no results and
 * writes no files. The caller reads \ref options() and hands them to
 * openswmmvis::io::exportMesh2DResults with its own progress dialog, so the
 * same export is reachable headless and is tested without a widget.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H

#include "io/mesh2dresultsexport.h"

#include <QDateTime>
#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace openswmmvis::ui {

/*! \brief What the dialog needs to know about the run it is exporting.
 *
 *  Deliberately plain data rather than the results layer: the dialog then has
 *  no dependency on the layer or render stack, and its test can build a
 *  72-step run in three lines. */
struct Mesh2DExportDialogInputs
{
    QList<QDateTime> times;             ///< one per frame; invalid entries render as an index
    int              currentIndex = -1; ///< the frame the animation is on ("Current")
    double           suggestedCellSize = 0.0;   ///< model units; seeds the raster cell size
    double           extentWidth = 0.0;         ///< mesh bounding box, model units
    double           extentHeight = 0.0;        ///< so the dialog can size the raster
    bool             hasVelocity = false;       ///< false disables the velocity variables
    QString          lengthUnit = QStringLiteral("m");   ///< spin-box suffix
    QString          defaultDir;        ///< where the file chooser opens
    QString          defaultBaseName;   ///< suggested file stem (the run's name)
};

class Mesh2DResultsExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Mesh2DResultsExportDialog(Mesh2DExportDialogInputs inputs,
                                       QWidget *parent = nullptr);
    ~Mesh2DResultsExportDialog() override;

    /*! \brief The options the user chose. Valid once exec() returns Accepted;
     *  readable before that (the tests drive the widgets directly). */
    openswmmvis::io::Mesh2DExportOptions options() const;

private slots:
    void onBrowse();
    void onFormatChanged();
    void onSelectAll();
    void onSelectNone();
    void onSelectCurrent();
    void onApplyStride();
    void revalidate();

private:
    void buildUi();
    openswmmvis::io::Mesh2DExportFormat currentFormat() const;
    std::vector<int> selectedSteps() const;
    unsigned selectedVariables() const;
    /*! \brief Empty when the choices are exportable, else why not. */
    QString validationMessage() const;

    Mesh2DExportDialogInputs m_in;

    QComboBox        *m_format = nullptr;
    QLineEdit        *m_path = nullptr;
    QPushButton      *m_browse = nullptr;
    QCheckBox        *m_depth = nullptr;
    QCheckBox        *m_head = nullptr;
    QCheckBox        *m_vx = nullptr;
    QCheckBox        *m_vy = nullptr;
    QCheckBox        *m_vmag = nullptr;
    QCheckBox        *m_includeMax = nullptr;
    QListWidget      *m_steps = nullptr;
    QPushButton      *m_all = nullptr;
    QPushButton      *m_none = nullptr;
    QPushButton      *m_current = nullptr;
    QSpinBox         *m_stride = nullptr;
    QPushButton      *m_applyStride = nullptr;
    QLabel           *m_selection = nullptr;
    QGroupBox        *m_rasterGroup = nullptr;
    QDoubleSpinBox   *m_cellSize = nullptr;
    QLabel           *m_gridInfo = nullptr;
    QComboBox        *m_interp = nullptr;
    QSpinBox         *m_neighbours = nullptr;
    QCheckBox        *m_maskDry = nullptr;
    QLabel           *m_validation = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H
