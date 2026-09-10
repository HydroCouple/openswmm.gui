/*!
 * \file   filespage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → Files / Output / Plugins.
 *
 * The largest page, and already inner-tabbed before this restructure: report
 * contents, the [FILES] interface-file section, output/report paths, the
 * writer plug-in combos, the [PLUGINS] table, the [PROCESS_COMPONENTS] table
 * and the hot-start save schedule. Ported as-is — T2 is a class split, not a
 * layout change.
 */
#ifndef FILESPAGE_H
#define FILESPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableView;
class QVBoxLayout;

// These carry the member types verbatim from the monolith, so the page needs
// their full declarations rather than forward declarations.
#include "ui/dialogs/climatologydialog.h"      // openswmmvis::ui::RelativePathPicker
#include "ui/dialogs/hotstartsavesmodel.h"     // HotstartSavesModel + its delegate
#include "ui/dialogs/pathbrowsedelegate.h"
#include "ui/dialogs/pluginstablemodel.h"
#include "ui/dialogs/processcomponentsmodel.h"
#include "ui/dialogs/simulationoptionsdialog.h"  // ProcessComponentsModel

namespace openswmmvis::ui
{

class FilesPage : public SimOptionsPage
{
    Q_OBJECT

public:
    explicit FilesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;
    bool validate(QString *warn) override;

    void applyCapabilities() override;

private slots:
    void browseForReportFile();
    void browseForOutputFile();
    void onHotstartSaveAddRow();
    void onHotstartSaveBrowseRow();
    void onHotstartSaveRemoveRow();
    void onHotstartSaveMoveRowUp();
    void onHotstartSaveMoveRowDown();
    void onSingleContainerToggled(bool on);

private:
    void buildUi();
    void tagWidgets();
    void buildReportContentsGroup(QVBoxLayout *parentLayout, QWidget *parent);
    void moveHotstartSaveRow(int from, int to);
    void updateSingleContainerEnabled();

    void readReportContentsFromEngine();
    int  writeReportContentsToEngine();
    void readPluginsFromEngine();
    int  writePluginsToEngine();
    void readProcessComponentsFromEngine();
    int  writeProcessComponentsToEngine();
    void readWriterCombosFromEngine();
    int  writeWriterCombosToEngine();
    void readOutputPathsFromSettings();
    void writeOutputPathsToSettings();
    void readFilesSectionFromEngine();
    int  writeFilesSectionToEngine();
    bool validateFilesTab(QString *warn);

    QPushButton                *m_componentsAddBtn    = nullptr;
    ProcessComponentIdDelegate *m_componentsIdDel     = nullptr;
    ProcessComponentsModel     *m_componentsModel     = nullptr;
    PathBrowseDelegate         *m_componentsPathDel   = nullptr;
    QPushButton                *m_componentsRemoveBtn = nullptr;
    QTableView                 *m_componentsView      = nullptr;
    QPushButton    *m_hotstartSavesAddBtn    = nullptr;
    QPushButton    *m_hotstartSavesBrowseBtn = nullptr;
    QPushButton    *m_hotstartSavesDownBtn   = nullptr;
    HotstartSavesDateTimeDelegate    *m_hotstartSavesDtDel    = nullptr;
    HotstartSavesModel               *m_hotstartSavesModel    = nullptr;
    PathBrowseDelegate               *m_hotstartSavesPathDel  = nullptr;
    QPushButton    *m_hotstartSavesRemoveBtn = nullptr;
    QPushButton    *m_hotstartSavesUpBtn     = nullptr;
    QTableView                       *m_hotstartSavesView     = nullptr;
    openswmmvis::ui::RelativePathPicker *m_hotstartUseEdit   = nullptr;
    openswmmvis::ui::RelativePathPicker *m_inflowsPathEdit   = nullptr;
    QComboBox      *m_inputWriterCombo  = nullptr;
    openswmmvis::ui::RelativePathPicker *m_outflowsPathEdit  = nullptr;
    QLineEdit      *m_outputFilePathEdit = nullptr;
    QComboBox      *m_outputWriterCombo = nullptr;
    QPushButton        *m_pluginsAddBtn     = nullptr;
    PluginsTableModel  *m_pluginsModel      = nullptr;
    PathBrowseDelegate *m_pluginsPathDel    = nullptr;
    QPushButton        *m_pluginsRemoveBtn  = nullptr;
    QTableView         *m_pluginsView       = nullptr;
    QComboBox                           *m_rainfallModeCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_rainfallPathEdit  = nullptr;
    QComboBox                           *m_rdiiModeCombo     = nullptr;
    openswmmvis::ui::RelativePathPicker *m_rdiiPathEdit      = nullptr;
    QLineEdit      *m_reportFilePathEdit = nullptr;
    QComboBox      *m_reportWriterCombo = nullptr;
    QCheckBox      *m_rptAveragesBox    = nullptr;
    QCheckBox      *m_rptContinuityBox  = nullptr;
    QCheckBox      *m_rptControlsBox    = nullptr;
    QCheckBox      *m_rptDisabledBox    = nullptr;
    QCheckBox      *m_rptFlowstatsBox   = nullptr;
    QCheckBox      *m_rptInputBox       = nullptr;
    QRadioButton   *m_rptLinkAllRadio   = nullptr;
    QLineEdit      *m_rptLinkListEdit   = nullptr;
    QRadioButton   *m_rptLinkNoneRadio  = nullptr;
    QRadioButton   *m_rptLinkSomeRadio  = nullptr;
    QRadioButton   *m_rptNodeAllRadio   = nullptr;
    QLineEdit      *m_rptNodeListEdit   = nullptr;
    QRadioButton   *m_rptNodeNoneRadio  = nullptr;
    QRadioButton   *m_rptNodeSomeRadio  = nullptr;
    QRadioButton   *m_rptSubcatchAllRadio  = nullptr;
    QLineEdit      *m_rptSubcatchListEdit  = nullptr;
    QRadioButton   *m_rptSubcatchNoneRadio = nullptr;
    QRadioButton   *m_rptSubcatchSomeRadio = nullptr;
    QComboBox                           *m_runoffModeCombo   = nullptr;
    openswmmvis::ui::RelativePathPicker *m_runoffPathEdit    = nullptr;
    QCheckBox      *m_signedHeadsCheck  = nullptr;
    QCheckBox      *m_singleContainerBox = nullptr;
    class QGroupBox *m_writersGroup = nullptr;  ///< Tab 7 — writer / container group.
};

} // namespace openswmmvis::ui

#endif // FILESPAGE_H
