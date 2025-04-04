/*!
 * \file   swmmvis.h
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2024
 * \pre
 * \bug
 * \warning
 * \todo
 */
#ifndef SWMMVIS_H
#define SWMMVIS_H

#include <QMainWindow>
#include <QSettings>

#include "swmmvislogmessage.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SWMMVis; }
QT_END_NAMESPACE

class QCheckBox;
class QLabel;
class QToolButton;
class QComboBox;
class QLineEdit;
class QSlider;
class QDateTimeEdit;
class QProgressBar;
class QStandardItemModel;
class SWMMVisProject;

/*!
 * \brief The SWMMVis class
 */ 
class SWMMVis : public QMainWindow
{


    Q_OBJECT

    
public:
   /*!
    * \brief SWMMVis constructor for the SWMMVis class
    * \param parent QWidget pointer to the parent widget
    */
    SWMMVis(QWidget *parent = nullptr);

    /*!
     * \brief SWMMVis destructor for the SWMMVis class
     */
    virtual ~SWMMVis();

 
public slots:
	void onLogMessage(const QString &message, SWMMVisLogMessage::LogMessageType messageType = SWMMVisLogMessage::LogMessageType::Information);

private:
  
   /*!
    * \brief initializeWelcomeScreen initializes the welcome screen
    */
    void initializeWelcomeScreen();


    /*! 
	 * \brief initializeToolBars initializes the tool bars
	 */
    void initializeToolBars();


    /*!
    * \brief initializeFileMenu initializes the file menu
	*/
    void initializeAnimationToolBar();

    /*!
     * \brief initializeStatusBar initializes the status bar
     */
    void initializeStatusBar();


    /*!
	 * \brief initializeDockWidgets initializes the dock widgets
	 */
    void initializeDockWidgets();


    /*!
	 * \brief initializeLayersDockWidget initializes the layers dock widget
	 */
    void initializeLayersDockWidget();


	/*!
     * \brief initializeMessageLogDockWidget initializes the message log dock widget
	*/
	void initializeMessageLogDockWidget();


	/*!
		 * \brief initializeMenus initializes the menus
			 */
	void initializeMenus();

    /*!
     * \brief initializeSettings initializes the settings
     */ 
    void initializeSettings();


    /*!
	 * \brief loadSettings loads the settings
	 */
    void saveSettings();


    /*!
     * \brief initializeRecentFilesMenu initializes the recent files menu
     */
    void clearPreviousWelcomeScreenElements();


private slots:
    /*!
     * \brief onOpenProject opens a project file
     */
    void onNewProject();
    
    /*!
     * \brief onSaveProject save a project to a file
     */
    void onSaveProject();
    
    /*!
     * \brief onRecentFilesSizeChanged handles the recent files size change
     */
    void onRecentFilesSizeChanged();

    /*!
     * \brief onShowWelcomeScreen shows the welcome screen
     */
    void onShowWelcomeScreen();
    
    /*!
     * \brief onClose handles the close event
     */
    void onClose();


    /*!
    * \brief onSetProgressBarBusy sets the progress bar to busy
     */
    void onSetProgressBarBusy(bool busy);



	/*!
	* \brief onAbout handles the about event
	*/
	void onAbout();



private:
    Ui::SWMMVis *ui;
    QStringList mRecentFiles;
    bool mShowSplashScreenOnStartUp;
    bool mShowWelcomeScreenOnStartUp;

    // Ui elements
    QCheckBox *mCheckBoxLevelOffsetMode;
    QToolButton *mToolButtonCoordinateReferenceSystem;
    QLineEdit *mLineEditCoordinates;
    QComboBox *mComboBoxMapScale;
    QSlider *mSliderAnimationTime;
    QProgressBar *mProgressBar;
    QDateTimeEdit *mDateTimeEditAnimationTime;
	QSettings mSettings;
	QStandardItemModel *mLogMessagesModel;
	SWMMVisProject *mProject;
};
#endif // SWMMVIS_H
