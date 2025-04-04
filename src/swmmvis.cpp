/*!
 * \file   swmmvis.cpp
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


#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QComboBox>
#include <QToolButton>
#include <QSlider>
#include <QDateTimeEdit>
#include <QProgressBar>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QMetaEnum>
#include <QFileInfo>

#include "swmmvis.h"
#include "ui_swmmvis.h"
#include "version.h"

/*!
 * \brief SWMMVis constructor for the SWMMVis class
 * \param parent QWidget pointer to the parent widget
 */
SWMMVis::SWMMVis(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::SWMMVis),
    mCheckBoxLevelOffsetMode(nullptr),
    mToolButtonCoordinateReferenceSystem(nullptr),
    mLineEditCoordinates(nullptr),
    mComboBoxMapScale(nullptr),
    mSliderAnimationTime(nullptr),
    mProgressBar(nullptr),
    mDateTimeEditAnimationTime(nullptr),
	mLogMessagesModel(nullptr),
	mProject(nullptr)
{
    ui->setupUi(this);

    // set up layer view dock widget

	// set up toolbars
	this->initializeToolBars();

    // set up status bar
    this->initializeStatusBar();

    //set up welcome screen
    this->initializeWelcomeScreen();

	// set up dock widgets
	this->initializeDockWidgets();

    // initialize menus
	this->initializeMenus();

	// initialize settings
	this->initializeSettings();

}

/*!
 * \brief SWMMVis destructor for the SWMMVis class
 */
SWMMVis::~SWMMVis()
{
    delete ui;
}

void SWMMVis::onLogMessage(const QString& message, SWMMVisLogMessage::LogMessageType messageType)
{

    QString name = QString::fromStdString(std::string(QMetaEnum::fromType<SWMMVisLogMessage::LogMessageType>().valueToKey(messageType)));

    QStandardItem* itemType = new QStandardItem(name);
    QStandardItem* itemTime = new QStandardItem(QDateTime::currentDateTime().toString("MM/dd/yyyy hh:mm:ss"));
	QStandardItem* itemMessage = new QStandardItem(message);

	this->mLogMessagesModel->appendRow(QList<QStandardItem*>() << itemTime << itemType << itemMessage);
	this->ui->treeViewMessageLogs->scrollToBottom();

}

/*!
 * \brief initializeWelcomeScreen initializes the welcome screen
 */
void SWMMVis::initializeWelcomeScreen()
{
    this->clearPreviousWelcomeScreenElements();
}

/*!
 * \brief initializeToolBars initializes the tool bars
 */
void SWMMVis::initializeToolBars()
{
    this->initializeAnimationToolBar();
}

/*!
 * \brief initializeFileMenu initializes the file menu
 */
void SWMMVis::initializeAnimationToolBar()
{
    this->mSliderAnimationTime = new QSlider(Qt::Orientation::Horizontal, this);
	this->mSliderAnimationTime->setSingleStep(1);
	this->mSliderAnimationTime->setPageStep(10);
	this->mSliderAnimationTime->setTickInterval(10);
	this->mSliderAnimationTime->setTickPosition(QSlider::TickPosition::TicksBelow);
	this->mSliderAnimationTime->setToolTip("Animation Time");
	this->mSliderAnimationTime->setStatusTip("Animation Time");
    this->mSliderAnimationTime->setMinimumWidth(300);

	this->mDateTimeEditAnimationTime = new QDateTimeEdit(this);
    this->mDateTimeEditAnimationTime->setDisplayFormat("MM/dd/yyyy hh:mm");
    this->mDateTimeEditAnimationTime->setCalendarPopup(true);
	this->mDateTimeEditAnimationTime->setToolTip("Animation Time");
	this->mDateTimeEditAnimationTime->setStatusTip("Animation Time");

	this->ui->toolBarAnimation->insertWidget(this->ui->actionSkipForward, this->mSliderAnimationTime);
	this->ui->toolBarAnimation->insertWidget(this->ui->actionSkipForward, this->mDateTimeEditAnimationTime);

}

/*!
 * \brief initializeStatusBar initializes the status bar
 */
void SWMMVis::initializeStatusBar()
{

    // Progress Bar
    QFrame* separator = new QFrame(ui->statusBar);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    this->mProgressBar = new QProgressBar(ui->statusBar);
    this->mProgressBar->setMinimum(0);
    this->mProgressBar->setMaximum(0);
    this->mProgressBar->setValue(0);
    this->mProgressBar->setToolTip("Progress");
    this->mProgressBar->setStatusTip("Progress");
    this->mProgressBar->setMaximumWidth(100);
	this->mProgressBar->setVisible(false);
    this->ui->statusBar->addPermanentWidget(this->mProgressBar);
    
    //this->mProgressBar->setHidden(true);
    this->ui->statusBar->addPermanentWidget(separator);


    //Offset Mode
    QLabel *labelOffSetMode = new QLabel("Offset Mode: Depth", ui->statusBar);

    this->mCheckBoxLevelOffsetMode = new QCheckBox(ui->statusBar);
    this->mCheckBoxLevelOffsetMode->setText("Elevation");
    this->mCheckBoxLevelOffsetMode->setStatusTip("Offset Mode");
    this->mCheckBoxLevelOffsetMode->setToolTip("Offset Mode");
    this->mCheckBoxLevelOffsetMode->setStyleSheet(
        "QCheckBox::indicator:checked {image: url(:/swmmvis/ToggleOn);}"
        "QCheckBox::indicator:unchecked {image: url(:/swmmvis/ToggleOff);}"
        );
    
    this->ui->statusBar->addPermanentWidget(labelOffSetMode);
    this->ui->statusBar->addPermanentWidget(this->mCheckBoxLevelOffsetMode);

    // Separators
    QFrame* separator1 = new QFrame(ui->statusBar);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);

    QFrame* separator2 = new QFrame(ui->statusBar);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);

    QFrame* separator3 = new QFrame(ui->statusBar);
    separator3->setFrameShape(QFrame::VLine);
    separator3->setFrameShadow(QFrame::Sunken);
    
    // Coordinates
    QLabel *labelCoordinates = new QLabel("Coordinates:", ui->statusBar);
    this->mLineEditCoordinates = new QLineEdit(ui->statusBar);
    this->mLineEditCoordinates->setText("0,0");
    this->mLineEditCoordinates->setStatusTip("Coordinates");
    this->mLineEditCoordinates->setToolTip("Coordinates");
    this->mLineEditCoordinates->setReadOnly(true);
    this->mLineEditCoordinates->setMaximumWidth(250);

    this->ui->statusBar->addPermanentWidget(separator1);
    this->ui->statusBar->addPermanentWidget(labelCoordinates);
    this->ui->statusBar->addPermanentWidget(this->mLineEditCoordinates);

    // Map Scale
    QLabel *labelMapScale = new QLabel("Map Scale:", ui->statusBar);
    this->mComboBoxMapScale = new QComboBox(ui->statusBar);
    this->mComboBoxMapScale->addItem("1:1");
    this->mComboBoxMapScale->setStatusTip("Map Scale");
    this->mComboBoxMapScale->setToolTip("Map Scale");
    this->mComboBoxMapScale->setMinimumWidth(150);
    this->ui->statusBar->addPermanentWidget(separator2);
    this->ui->statusBar->addPermanentWidget(labelMapScale);
    this->ui->statusBar->addPermanentWidget(this->mComboBoxMapScale);

    // Coordinate Reference System
    QLabel *labelCoordinateReferenceSysetm = new QLabel("Coordinate Reference System:", ui->statusBar);
    this->mToolButtonCoordinateReferenceSystem = new QToolButton(ui->statusBar);
    this->mToolButtonCoordinateReferenceSystem->setIcon(QIcon(":/swmmvis/Globe"));
    this->mToolButtonCoordinateReferenceSystem->setText("EPSG:4326");
    this->mToolButtonCoordinateReferenceSystem->setStatusTip("Coordinate Reference System");
    this->mToolButtonCoordinateReferenceSystem->setToolTip("Coordinate Reference System");
    this->mToolButtonCoordinateReferenceSystem->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextBesideIcon);

    //this->ui->statusBar->addPermanentWidget(widget);
    this->ui->statusBar->addPermanentWidget(separator3);
    this->ui->statusBar->addPermanentWidget(labelCoordinateReferenceSysetm);
    this->ui->statusBar->addPermanentWidget(this->mToolButtonCoordinateReferenceSystem);

}

/*!
 * \brief initializeDockWidgets initializes the dock widgets
 */
void SWMMVis::initializeDockWidgets()
{
    this->initializeLayersDockWidget();
	this->initializeMessageLogDockWidget();
}

/*!
 * \brief initializeLayersDockWidget initializes the layers dock widget
 */
void SWMMVis::initializeLayersDockWidget()
{

}

/*!
 * \brief initializeMessageLogDockWidget initializes the message log dock widget
 */
void SWMMVis::initializeMessageLogDockWidget()
{
	this->mLogMessagesModel = new QStandardItemModel(0, 3, this);
    this->mLogMessagesModel->setColumnCount(3);
	this->mLogMessagesModel->setHeaderData(0, Qt::Horizontal, "Time", Qt::DisplayRole);
	this->mLogMessagesModel->setHeaderData(1, Qt::Horizontal, "Type", Qt::DisplayRole);
	this->mLogMessagesModel->setHeaderData(2, Qt::Horizontal, "Message", Qt::DisplayRole);
    this->ui->treeViewMessageLogs->setModel(this->mLogMessagesModel);
}

/*!
 * \brief initializeMenus initializes the menus
 */
void SWMMVis::initializeMenus()
{
	connect(ui->actionNew, &QAction::triggered, this, &SWMMVis::onNewProject);
	connect(ui->actionSave, &QAction::triggered, this, &SWMMVis::onSaveProject);
	connect(ui->actionAbout, &QAction::triggered, this, &SWMMVis::onAbout);
}

/*!
 * \brief initializeSettings initializes the settings
 */
void SWMMVis::initializeSettings()
{
    onLogMessage("Reading settings");
	onSetProgressBarBusy(true);

    mSettings.beginGroup("SWMMVis::MainWindow");
    {
        //!HydroCoupleComposer GUI
        this->restoreState(mSettings.value("SWMMVis::WindowState", saveState()).toByteArray());
        this->setWindowState((Qt::WindowState)mSettings.value("SWMMVis::WindowStateEnum", (int)this->windowState()).toInt());
        this->setGeometry(mSettings.value("SWMMVis::Geometry", geometry()).toRect());

        //!recent files
        this->mRecentFiles = mSettings.value("SWMMVis::RecentFiles", mRecentFiles).toStringList();

        onRecentFilesSizeChanged();

    }
    mSettings.endGroup();

	onRecentFilesSizeChanged();

    //!last path opened
    onLogMessage("Finished reading settings");
	onSetProgressBarBusy(false);
}

/*!
 * \brief loadSettings loads the settings
 */
void SWMMVis::saveSettings()
{

}

/*!
 * \brief clearPreviousWelcomeScreenElements clears the previous welcome screen elements
 */ 
void SWMMVis::clearPreviousWelcomeScreenElements()
{
    QList<QCommandLinkButton *> allButtons =
        ui->frameRecentFiles->findChildren<QCommandLinkButton*>();

    qDeleteAll(allButtons);
    allButtons.clear();
}

/*!
 * \brief onOpenProject opens a project file
 */
void SWMMVis::onNewProject()
{

}

/*!
 * \brief onSaveProject save a project to a file
 */
void SWMMVis::onSaveProject()
{

}

/*!
 * \brief onRecentFilesSizeChanged handles the recent files size change
 */
void SWMMVis::onRecentFilesSizeChanged()
{
    while (mRecentFiles.size() > 20)
    {
        mRecentFiles.removeLast();
    }

	this->ui->menuOpenRecent->clear();


    if (mRecentFiles.count())
    {
		this->ui->menuOpenRecent->setEnabled(true);
        this->ui->commandLinkButtonClearRecentFiles->setEnabled(true);

		for (int i = 0; i < mRecentFiles.count(); i++)
		{
			QAction* action = new QAction(this->ui->menuOpenRecent);


			QFileInfo file(mRecentFiles[i]);
			QString fullPath(file.absoluteFilePath());
			//        QString text = tr("&%1. %2").arg(i + 1).arg(file.absoluteFilePath());
			action->setText(file.filePath());
			action->setToolTip(fullPath);
			action->setStatusTip(fullPath);
			action->setWhatsThis(fullPath);
			action->setVisible(true);
			action->setData(fullPath);

		}
	}
	else
    { 

		this->ui->menuOpenRecent->setEnabled(false);
		this->ui->commandLinkButtonClearRecentFiles->setEnabled(false);
    }
   
}

/*!
 * \brief onShowWelcomeScreen shows the welcome screen
 */
void SWMMVis::onShowWelcomeScreen()
{

}

/*!
 * \brief onClose handles the close event
 */
void SWMMVis::onClose()
{

}

/*!
 * \brief onSetProgressBarBusy sets the progress bar to busy
 */
void SWMMVis::onSetProgressBarBusy(bool busy)
{
	this->mProgressBar->setMaximum(0);
	this->mProgressBar->setMinimum(0);
	this->mProgressBar->setValue(0);
	this->mProgressBar->setHidden(!busy);
}


void SWMMVis::onAbout()
{

    QString buildDateTime = QString("%1T%2").arg(__DATE__).arg(__TIME__);
    QString version = QString("%1.%2.%3").arg(SWMM_VERSION_MAJOR).arg(SWMM_VERSION_MINOR).arg(SWMM_VERSION_PATCH);

    QMessageBox::about(this, "SWMMVis",
        "<html>"
        "<head>"
        "<title>Component Information</title>"
        "</head>"
        "<body>"
        "<img alt=\"icon\" src=':/swmmvis/AddSWMMOutput' width=\"100\" align=\"left\" />"
        "<h3 align=\"center\">SWMM " + version + "</h3>"
        "<hr>"
        "<p>Build Date: " + buildDateTime + "</p>"
        "<p align=\"center\">This program and its associated libraries are provided AS IS with NO WARRANTY OF ANY KIND, "
        "INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.</p>"
        "<p align=\"justify\"><a href=\"mailto:swmm@epa.gov?Subject=SWMMVis Composer\">swmm@epa.gov</a></p>"
        "<p align=\"justify\"><a href=\"https://www.epa.gov/water-research/storm-water-management-model-swmm\">https://www.epa.gov/water-research/storm-water-management-model-swmm</a></p>"
        "</body>"
        "</html>");

}
